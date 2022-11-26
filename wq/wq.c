// vim: sw=2 ts=2 expandtab smartindent
#include <stdio.h>

#include "wq.h"
#include "font.h"

/* TODO: worry about this eventually
 * https://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian/2782742#2782742
 */
 
/* theoretically part of our contract with our environment but i was lazy */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
/* TODO: write our own allocator for LogNode * ... maybe just a ring buffer of chars? */ 

/* rolling our own math makes it easier to smol WASM, :> */

#define MATH_PI  3.141592653589793
#define MATH_TAU 6.283185307179586

static float lerp(float v0, float v1, float t) { return (1 - t) * v0 + t * v1; }
#ifdef CUSTOM_MATH
static float fmodf(float f, float n) { return f - (float)(n * (int)(f/n)); }
static float fabsf(float f) { return f < 0 ? -f : f; }
static float signf(float f) { return f < 0 ? -1 : 1; }

// Remez' algorithm approximation
// https://stackoverflow.com/questions/23837916/a-faster-but-less-accurate-fsin-for-intel-asm
static float sinf(float _x) {
  float x = fmodf(fabsf(_x), MATH_TAU) - MATH_PI;
  if (_x > 0) x = -x;

  register float xx = x*x;
  register float s =
    x + (x * xx) * (-0.16612511580269618f +
        xx * (8.0394356072977748e-3f +
          xx * -1.49414020045938777495e-4f));
  return s;
}
static float cosf(float x) {
  return sinf(x + MATH_PI/2);
}

static float sqrtf(float z) {
	union { float f; uint32_t i; } val = {z};	/* Convert type, preserving bit pattern */
	val.i -= 1 << 23;	/* Subtract 2^m. */
	val.i >>= 1;		/* Divide by 2. */
	val.i += 1 << 29;	/* Add ((b + 1) / 2) * 2^m. */

	return val.f;		/* Interpret again as float */
}
#else
#define log print_log
#endif

/* vector fns (only things that don't generalize to N=1) */
static float mag(float x, float y) { return sqrtf(x*x + y*y); }
static void norm(float *x, float *y) {
  float m = mag(*x, *y);
  if (m > 0.0f)
    *x /= m,
    *y /= m;
}


/* forward decls */
static void log(Env *e, char *p);


/* --- persistent State layout --- */
typedef uint32_t EntId;
typedef struct {
  int active;
  Addr addr;

  struct { float x, y; } vel;

  EntId pc; /* player character's Ent */
} Client;
#define CLIENTS_MAX (1 << 8)

typedef struct LogNode LogNode;
struct LogNode {
  LogNode *prev;
  char data[];
};

typedef enum {
  EntKind_Empty,
  EntKind_Thing,
} EntKind;

typedef struct {
  EntKind kind;
  float x, y;
} HostEnt;

typedef struct {
  EntKind kind;
  int you;
  float x, y;
} ClntEnt;

/* client ents are basically just a networking abstraction
 * to track a moving thing and maybe make it look good
 *
 * host ents are the server-side mirror of this, but there,
 * they double as gameplay abstractions for reusing e.g. collision detection
 *
 * not sure if that is actually useful, we may use them exclusively for the networking */
#define WQ_ENTS_MAX (1 << 10)
typedef struct {
  /* --- dbg --- */
  LogNode *log;
  double frametime_ring_buffer[256];
  int frametime_ring_index;
  /* --- dbg --- */

  struct {
    uint32_t last_known_tick;

    int am_connected, tries;
    char keysdown[WqVk_MAX];
    ClntEnt ents[WQ_ENTS_MAX];
    struct { float x, y; } cam;
  } clnt;

  int am_host;
  struct {
    uint32_t tick_count;

    double last, dt_acc;
    Client clients[CLIENTS_MAX];

    HostEnt ents[WQ_ENTS_MAX];
    int next_ent;
  } host;

} State;

static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len > sizeof(State)) {
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);

    /* --- init state --- */
    for (int i = 0; i < 256; i++) state(env)->frametime_ring_buffer[i] = 1/60;

    state(env)->host.next_ent++; /* i need one for my mad hax */
    log(env, "sup nerd");
    /* --- init state --- */
  }
  return (State *)env->stash.buf;
}
/* --- persistent State layout --- */


/* --- log --- */
static void log(Env *env, char *p) {
  int len = strlen(p);

  /* maintain linked list */
  LogNode *ln = calloc(sizeof(LogNode *) + len, 1);
  ln->prev = state(env)->log;
  state(env)->log = ln;

  /* copy in payload */
  memcpy((uint8_t *)ln + sizeof(LogNode *), p, len);
}
/* --- log --- */


/* --- rcx --- */
typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } raw_size;
  struct { int x, y; } size;
  struct {
    int text_size;
    uint32_t text_color;
  } cfg;
} Rcx;

/* plot pixel */
Rcx *_rcx = 0;
static void rcx_p(int x, int y, uint32_t p) {
  /* put it on the centered, 16:9 "canvas" */
  x += (_rcx->raw_size.x - _rcx->size.x)/2;
  y += (_rcx->raw_size.y - _rcx->size.y)/2;

  if (x < 0 || x >= _rcx->raw_size.x) return;
  if (y < 0 || y >= _rcx->raw_size.y) return;

  /* y is up, motherfucker */
  y = _rcx->raw_size.y - y - 1;

  _rcx->pixels[y*_rcx->stride + x] = p;
}

/* x, y in domain 0..8, c is char */
static int fontdata_read(int x, int y, char c) {
  uint8_t bits = fontdata[c*8 + y];
  return (bits >> (7-x)) & 1;
}

static void rcx_char(int px, int py, int scale, char c) {
  for (int y = 0; y < 8*scale; y++) {
    for (int x = 0; x < 8*scale; x++)
      if (fontdata_read(x/scale, 7 - y/scale, c))
        rcx_p(px + x, py + y, _rcx->cfg.text_color);
  }
}

static void rcx_str_cursor(int *x, int *y, char *str) {
  int scale = _rcx->cfg.text_size;
  for (; *str; *str++) {
    if (*str == '\n')
      *y += 8*scale;
    else
      rcx_char(*x += 8*scale, *y, scale, *str);
  }
}

static void rcx_str(int x, int y, char *str) {
  rcx_str_cursor(&x, &y, str);
}

EXPORT void wq_render(Env *env, PixelDesc *pd) {
  double start_ts = env->ts();

  Rcx __rcx = { .pixels = pd->pixels, .stride = pd->stride, .raw_size = { pd->size.x, pd->size.y } };
  _rcx = &__rcx;

  /* what's the biggest we can be and preserve 16x9? */
  {
    float scale = (pd->size.x/16.f < pd->size.y/9.f) ? pd->size.x/16.f : pd->size.y/9.f;
    _rcx->size.x = 16 * scale;
    _rcx->size.y =  9 * scale;
  }

  /* clear to black */
  memset(pd->pixels, 0, pd->size.x * pd->size.y * sizeof(uint32_t));

  /* checkerboard */
  for (int y = 0; y < _rcx->size.y; y++)
    for (int x = 0; x < _rcx->size.x; x++)
      rcx_p(x, y, (y/4%2 == x/4%2) ? 0x22222222 : 0x33333333);

  /* some net stats */
#if 0
  if (state(env)->am_host) {
    rcx_str(0, 1*28, "hosting");

    /* see how many players we are hosting */
    int nclient = 0;
    {
      Client *clients = state(env)->host.clients;
      for (int i = 0; i < CLIENTS_MAX; i++)
        nclient += clients[i].active;
    }

    char c = '1' + (nclient-1);
    rcx_str(0, 2*28, &c);
  }
#endif

  /* --- we in world space now biatches --- */

  State *s = state(env);

  /* a quest of self-discovery */
  ClntEnt *you = NULL;
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    ClntEnt *e = s->clnt.ents + i;
    if (e->you) { you = e; break; }
  }

  /* move cam to you */
  if (you) {
    s->clnt.cam.x = lerp(s->clnt.cam.x, you->x - _rcx->size.x/2, 0.08f);
    s->clnt.cam.y = lerp(s->clnt.cam.y, you->y - _rcx->size.y/2, 0.08f);
  }
  _rcx->cfg.text_color = 0xFFFFFFFF;


  rcx_char(
    s->clnt.cam.x, // - s->clnt.cam.x,
    s->clnt.cam.y, // - s->clnt.cam.y,
    4, 'c'
  );

  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    ClntEnt *e = s->clnt.ents + i;

    rcx_char(
      e->x - s->clnt.cam.x,
      e->y - s->clnt.cam.y,
      4, (you == e) ? 'u' : 'p'
    );
  }

  /* draw map */
  {
    char map[] = 
      "wwwwwwwwwwwwwwwwwwwwwww\n"
      "w....w.w|w.w|w.w|w....w\n"
      "w.....................w\n"
      "w....w|w.w|w.w|w.w....w\n"
      "wwwwwwwwwwwwwwwwwwwwwww\n";

    int cols = 0;
    while (map[cols] && map[cols] != '\n') cols++;
    cols++; /* newline */
    int rows = sizeof(map) / cols;

    int min_x = s->clnt.cam.x;
    int max_x = s->clnt.cam.x + _rcx->size.x;
    int min_y = s->clnt.cam.y;
    int max_y = s->clnt.cam.y + _rcx->size.y;
    for (int y = min_y; y < max_y; y++)
      for (int x = min_x; x < max_x; x++) {
        float tx =            (float)(x + 350.0f) / 90.0f;
        float ty = rows - 1 - (float)(y +  50.0f) / 90.0f;
        int i = (int)ty*cols + (int)tx;

        if (tx < 0 || tx >= cols) continue;
        if (ty < 0 || ty >= rows) continue;
        if (map[i] == '.') continue;

        uint32_t color = 0xFF0000FF;
        if (map[i] == '|') {
          float cx = fmodf((float)(x + 350.0f), 90.0f);
          float cy = fmodf((float)(y +  50.0f), 90.0f);
          if (mag(90.0f/2 - cx, 90.0f/2 - cy) < 15)
            color = 0xFFFF00FF;
          else
            continue;
        }

        rcx_p(x - min_x, y - min_y, color);
      }
  }

  /* log */
  {
    _rcx->cfg.text_color = 0xBBBBBBBB;

    // int scale = _rcx->cfg.text_size = (_rcx->size.y/20/8);
    int scale = _rcx->cfg.text_size = 1;
    int x = 0, y = 1*8*scale;
    for (LogNode *ln = state(env)->log; ln; ln = ln->prev) {
      rcx_str_cursor(&x, &y, ln->data);
      y += 8*scale;
      x = 0;
    }
  }


  /* display frame time */
  {
    s->frametime_ring_index = (s->frametime_ring_index + 1) % 256;
    s->frametime_ring_buffer[s->frametime_ring_index] = env->ts() - start_ts;

    double average = 0.0;
    for (int i = 0; i < 256; i++) average += s->frametime_ring_buffer[i];
    average /= 256.0;

    char buf[16] = {0};
    snprintf(buf, 16, "%.1fms", 1000*average);

    int scale = 8*(_rcx->cfg.text_size = 1);
    rcx_str(_rcx->size.x - scale*5,
            _rcx->size.y - scale*1, buf);
  }
}
/* --- rcx --- */


/* --- net --- */

static Client *clients_find(Env *env, uint32_t addr_hash) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].active && clients[i].addr.hash == addr_hash)
      return clients + i;
  return NULL;
}

static Client *clients_storage(Env *env) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (!clients[i].active)
      return clients + i;
  log(env, "too many clients! dropping one");
  return NULL;
}

static EntId host_ent_next(Env *env) {
  return state(env)->host.next_ent++;
}
static HostEnt *host_ent_get(Env *env, EntId id) {
  return state(env)->host.ents + id;
}

/* to client */
typedef enum {
  ToClntMsgKind_Ping,
  ToClntMsgKind_EntUpd,
} ToClntMsgKind;

typedef struct {
  ToClntMsgKind kind;
  struct { int id; uint32_t tick; ClntEnt ent; } ent_upd;
} ToClntMsg;

/* to host */
typedef enum {
  ToHostMsgKind_Ping,
  ToHostMsgKind_Move,
} ToHostMsgKind;

typedef struct {
  ToHostMsgKind kind;
  struct { float x, y; } move;
} ToHostMsg;

static void host_tick(Env *env) {
  State *s = state(env);

  uint32_t tick = s->host.tick_count++;

  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;

    /* apply Client velocity */
    host_ent_get(env, c->pc)->x += c->vel.x * 6.0f;
    host_ent_get(env, c->pc)->y += c->vel.y * 6.0f;

    ToClntMsg msg = {
      .kind = ToClntMsgKind_EntUpd,
      .ent_upd = {
        .id = 0,
        .tick = tick,
        .ent = {
          .kind = EntKind_Thing, 
          .x = cosf(env->ts()) * 50,
          .y = sinf(env->ts()) * 50,
        }
      }
    };
    env->send(&c->addr, (void *)&msg, sizeof(msg));

    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      HostEnt *e = state(env)->host.ents + i;
      if (e->kind == EntKind_Empty) continue;

      ToClntMsg msg = {
        .kind = ToClntMsgKind_EntUpd,
        .ent_upd = {
          .id = i,
          .tick = tick,
          .ent = {
            .kind = EntKind_Thing, 
            .you = c->pc == i,
            .x = e->x,
            .y = e->y,
          }
        }
      };
      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
  }
}

/* broadcast if it's time to */
static void host_poll(Env *env) {
  State *s = state(env);

  double now = env->ts();
  if (s->host.last > 0) {
    /* accumulate delta time */
    double dt = now - s->host.last;
    s->host.dt_acc += dt;

    double FREQ = 1.0 / 20.0;
    while (s->host.dt_acc > FREQ)
      s->host.dt_acc -= FREQ,
      host_tick(env);
  }
  s->host.last = now;
}

static void host_recv(Env *env, Addr *addr, uint8_t *buf, int len) {
  Client *client = clients_find(env, addr->hash);

  if (client == NULL) {
    client = clients_storage(env);

    EntId pc = host_ent_next(env);
    *host_ent_get(env, pc) = (HostEnt) {
      .kind = EntKind_Thing,
      .x = 50,
      .y = 50,
    };
    *client = (Client) {
      .active = 1,
      .addr = *addr,
      .pc = pc,
    };
  }

  ToHostMsg *msg = (ToHostMsg *)buf;

  if (msg->kind == ToHostMsgKind_Ping) {
    /* echo! */
    log(env, "(host) echoing back Ping!");
    ToClntMsg msg = { .kind = ToClntMsgKind_Ping };
    env->send(addr, (void *)&msg, sizeof(msg));
  }

  if (msg->kind == ToHostMsgKind_Move) {
    client->vel.x = msg->move.x;
    client->vel.y = msg->move.y;
    norm(&client->vel.x, &client->vel.y);
  }
}

static void clnt_recv(Env *env, uint8_t *buf, int len) {
  State *s = state(env);

  /* gotta be connected to the host to receive a message, so */
  s->clnt.am_connected = 1;

  ToClntMsg *msg = (ToClntMsg *)buf;
  if (msg->kind == ToClntMsgKind_Ping)
    log(env, "got host Ping!");
  if (msg->kind == ToClntMsgKind_EntUpd) {
    uint32_t tick = msg->ent_upd.tick;

    if (tick >= s->clnt.last_known_tick) {
      s->clnt.last_known_tick = tick;
      s->clnt.ents[msg->ent_upd.id] = msg->ent_upd.ent;
    }
  }
}

EXPORT void wq_update(Env *env) {
  State *s = state(env);

  /* better get yourself connected */
  if (!s->clnt.am_connected) {
    log(env, "making connection attempt");
    // printf("made %d connection attempts, sending \"hi\"\n", s->clnt.tries);
    ToHostMsg msg = { .kind = ToHostMsgKind_Ping };
    env->send_to_host((void *)&msg, sizeof(msg));
    s->clnt.tries++;

    if (!s->am_host && s->clnt.tries >= 10) {
      /* Tried 10 times to reach server ...
       * still no luck ... so I'll serve myself! */
      s->am_host = 1;
      log(env, "gonna try to host ...");
    }
  }

  /* keystate has changed, recompute heading and update server */
  if (s->clnt.am_connected) {
    ToHostMsg msg = {
      .kind = ToHostMsgKind_Move,
    };

    if (s->clnt.keysdown[WqVk_W]) msg.move.y += 1;
    if (s->clnt.keysdown[WqVk_S]) msg.move.y -= 1;
    if (s->clnt.keysdown[WqVk_A]) msg.move.x -= 1;
    if (s->clnt.keysdown[WqVk_D]) msg.move.x += 1;

    env->send_to_host((void *)&msg, sizeof(msg));
  }

  /* server gotta do server stuff */
  if (s->am_host) host_poll(env);

  /* keep polling until both recvs return 0 */
  int more = 1;
  while (more) {
    uint8_t buf[1 << 8] = {0};
    int len = sizeof(buf);
    Addr addr = {0};

    more = 0;

    len = sizeof(buf);
    if (s->am_host && env->host_recv(&addr, buf, &len))
      host_recv(env, &addr, buf, len), more = 1;

    len = sizeof(buf);
    if (env->clnt_recv(buf, &len))
      clnt_recv(env, buf, len), more = 1;
  }
}
/* --- net --- */

/* --- input --- */
EXPORT void wq_input(Env *env, char c, int down) {
  State *s = state(env);

  if (c == WqVk_Esc  ) {
    log(env, "restarting everything");

    free(env->stash.buf);
    env->stash.buf = NULL;
    state(env);
  }

  if (c == WqVk_Tilde) log(env, "Tilde");

  s->clnt.keysdown[c] = down;

}
/* --- input --- */


