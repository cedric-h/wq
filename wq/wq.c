// vim: sw=2 ts=2 expandtab smartindent
#include <stdio.h>

#define t_begin(str) env->trace_begin((str), sizeof(str))
#define t_end() env->trace_end()

#include "wq.h"
#include "font.h"

/* TODO: worry about this eventually
 * https://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian/2782742#2782742
 */
 
/* theoretically part of our contract with our environment but i was lazy */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
/* TODO: write our own allocator for LogNode *
 * ... maybe just a ring buffer of chars?
 *
 * thought about this some more (too much), conclusion:
 * ring buffer of message struct containing 100 chars and ts */ 

/* rolling our own math makes it easier to smol WASM, :> */

#define MATH_PI  3.141592653589793
#define MATH_TAU 6.283185307179586

static float inline ease_out_quad(float x) { return 1 - (1 - x) * (1 - x); }

static float inline lerp(float v0, float v1, float t) { return (1 - t) * v0 + t * v1; }
static float inline inv_lerp(float min, float max, float p) { return (p - min) / (max - min); }
static float inline lerp_rads(float a, float b, float t) {
  float difference = fmodf(b - a, MATH_PI*2.0f),
        distance = fmodf(2.0f * difference, MATH_PI*2.0f) - difference;
  return a + distance * t;
}
static float inline fabsf(float f) { return f < 0 ? -f : f; }
static inline float signf(float f) { return f < 0 ? -1 : 1; }
#ifdef CUSTOM_MATH
static inline float fmodf(float f, float n) { return f - (float)(n * (int)(f/n)); }

/* thx nakst bby uwu */
// https://gitlab.com/nakst/essence/-/blob/master/shared/math.cpp
typedef union { float f; uint32_t i; } ConvertFloatInteger;
#define F(x) (((ConvertFloatInteger) { .i = (x) }).f)

static inline float _ArcTangentFloat(float x) {
	// Calculates arctan(x) for x in [0, 0.5].

	float x2 = x * x;

	return x * (F(0x3F7FFFF8) + x2 * (F(0xBEAAA53C) + x2 * (F(0x3E4BC990) + x2 * (F(0xBE084A60) + x2 * F(0x3D8864B0)))));
}

static inline float _EsCRTatanf(float x) {
	int negate = 0;

	if (x < 0) { 
		x = -x; 
		negate = 1; 
	}

	int reciprocalTaken = 0;

	if (x > 1) {
		x = 1 / x;
		reciprocalTaken = 1;
	}

	float y;

	if (x < 0.5f) {
		y = _ArcTangentFloat(x);
	} else {
		y = 0.463647609000806116f + _ArcTangentFloat((2 * x - 1) / (2 + x));
	}

	if (reciprocalTaken) {
		y = MATH_PI / 2 - y;
	}
	
	return negate ? -y : y;
}

float atan2f(float y, float x) {
	if (x == 0) return y > 0 ? MATH_PI / 2 : -MATH_PI / 2;
	else if (x > 0) return _EsCRTatanf(y / x);
	else if (y >= 0) return MATH_PI + _EsCRTatanf(y / x);
	else return -MATH_PI + _EsCRTatanf(y / x);
}

// Remez' algorithm approximation
// https://stackoverflow.com/questions/23837916/a-faster-but-less-accurate-fsin-for-intel-asm
static inline float sinf(float _x) {
  float x = fmodf(fabsf(_x), MATH_TAU) - MATH_PI;
  if (_x > 0) x = -x;

  register float xx = x*x;
  register float s =
    x + (x * xx) * (-0.16612511580269618f +
        xx * (8.0394356072977748e-3f +
          xx * -1.49414020045938777495e-4f));
  return s;
}
static inline float cosf(float x) {
  return sinf(x + MATH_PI/2);
}

static inline float sqrtf(float z) {
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
static inline float mag(float x, float y) { return sqrtf(x*x + y*y); }
static inline void norm(float *x, float *y) {
  float m = mag(*x, *y);
  if (m > 0.0f)
    *x /= m,
    *y /= m;
}

typedef struct {
  struct { float x, y; } grip, tip, circ;
  float radius;
} LineHitsCircle;
static int line_hits_circle(const LineHitsCircle *lhc) {
  float v_x = lhc->tip.x - lhc->grip.x,
        v_y = lhc->tip.y - lhc->grip.y;
  float line_len = mag(v_x, v_y);
  norm(&v_x, &v_y);

  float u_x = lhc->circ.x - lhc->grip.x,
        u_y = lhc->circ.y - lhc->grip.y;

  /* sidecar is point where ray gets closest */
  float uv_dot = u_x*v_x + u_y*v_y;
  float sidecar_x = uv_dot * v_x;
  float sidecar_y = uv_dot * v_y;
  if (mag(sidecar_x, sidecar_y) > line_len) return 0;

  /* this is the closest the ray gets to the circle */
  float bridge_x = u_x - sidecar_x;
  float bridge_y = u_y - sidecar_y;
  float closest = mag(bridge_x, bridge_y);

  return closest <= lhc->radius;
}


/* forward decls */
static void log(Env *e, char *p);


/* --- map --- */
#define map_tile_world_size (90)
#define map_world_offset_x (150)
#define map_world_offset_y (200)
char map[] = 
  "wwwwwwwwwwwwwwwwwwwwwww\n"
  "w....w.w|w.w|w.w|w....w\n"
  "w.....................w\n"
  "w....w|w.w|w.w|w.w....w\n"
  "wwwwwwwwwwwwwwwwwwwwwww\n";

static int map_cols(void) {
  return sizeof("wwwwwwwwwwwwwwwwwwwwwww");

  // int cols = 0;
  // while (map[cols] && map[cols] != '\n') cols++;
  // return cols + 1; /* newline */
}
static inline int map_rows(void) { return sizeof(map) / map_cols(); }

static inline int map_flip_y(int y) { return map_rows() - 1 - y; }

static inline int map_x_from_world(float x) { return            (x + map_world_offset_x) / map_tile_world_size ; }
static inline int map_y_from_world(float y) { return map_flip_y((y + map_world_offset_y) / map_tile_world_size); }

static inline float map_x_to_world(float x) { return            x *map_tile_world_size - map_world_offset_x; }
static inline float map_y_to_world(float y) { return map_flip_y(y)*map_tile_world_size - map_world_offset_y; }

/* in domain 0..1 */

static inline char map_index(int mx, int my) { return map[my*map_cols() + mx]; }
static inline int map_in_bounds(int mx, int my) {
  if (mx < 0 || mx >= map_cols()) return 0;
  if (my < 0 || my >= map_rows()) return 0;
  return 1;
}
/* --- map --- */


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
typedef struct {
  LogNode *prev;
  double ts;
} LogNodeHeader;
struct LogNode {
  LogNodeHeader header;
  char data[];
};

typedef enum {
  EntKind_Empty,
  EntKind_Limbo,
  EntKind_Player,
  EntKind_Fireball,
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

typedef struct {
  uint32_t fb_hit_ticks[10];
} TutorialState;

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
  int log_open;
  double frametime_ring_buffer[256];
  int frametime_ring_index, frametime_ring_len;
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

    TutorialState tutorial;
  } host;

} State;

static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len > sizeof(State)) {
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);

    /* --- init state --- */
    for (int i = 0; i < 256; i++) state(env)->frametime_ring_buffer[i] = 1/60;

    state(env)->host.next_ent += 25; /* i need some scratch ents for my mad hax */
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
  LogNode *ln = calloc(sizeof(LogNodeHeader) + len, 1);
  ln->header.prev = state(env)->log;
  ln->header.ts = env->ts();
  state(env)->log = ln;

  /* copy in payload after header */
  memcpy((uint8_t *)ln + sizeof(LogNodeHeader), p, len);
}
/* --- log --- */


/* --- rcx --- */
typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } raw_size;
  struct { int x, y; } size;
  struct {
    int text_size, newline_dir;
    uint32_t text_color;
    float alpha;
    float mat[4];
    struct { float x, y; } origin;
  } cfg;
} Rcx;

static inline uint32_t rcx_color(int r, int g, int b) {
  return (r << 0) & 0x000000FF |
         (g << 4) & 0x0000FF00 |
         (b << 8) & 0x00FF0000 ;
}
static inline void rcx_decompose(uint32_t c, int *r, int *g, int *b) {
  *r = (c & 0x000000FF) >> 0;
  *g = (c & 0x0000FF00) >> 4;
  *b = (c & 0x00FF0000) >> 8;
}

Rcx *_rcx = 0;
static inline void rcx_transform(float *x, float *y) {
  float _x = *x,
        _y = *y;
  *x = _rcx->cfg.mat[0]*_x + _rcx->cfg.mat[2]*_y - _rcx->cfg.origin.x;
  *y = _rcx->cfg.mat[1]*_x + _rcx->cfg.mat[3]*_y - _rcx->cfg.origin.y;
}

/* plot pixel */
static void rcx_p(int x, int y, uint32_t p) {
  /* transform in floatspace roudning to pixel ints */
  float _x = x,
        _y = y;
  rcx_transform(&_x, &_y);
  x = _x;
  y = _y;

  /* put it on the centered, 16:9 "canvas" */
  x += (_rcx->raw_size.x - _rcx->size.x)/2;
  y += (_rcx->raw_size.y - _rcx->size.y)/2;

  if (x < 0 || x >= _rcx->raw_size.x) return;
  if (y < 0 || y >= _rcx->raw_size.y) return;

  /* y is up, yall */
  y = _rcx->raw_size.y - y - 1;

  int i = y*_rcx->stride + x;
  if (_rcx->cfg.alpha < 1.0f) {
    int r0, g0, b0, r1, g1, b1;
    rcx_decompose(_rcx->pixels[i], &r0, &g0, &b0);
    rcx_decompose(              p, &r1, &g1, &b1);

    _rcx->pixels[i] = rcx_color(
      lerp(r0, r1, _rcx->cfg.alpha),
      lerp(g0, g1, _rcx->cfg.alpha),
      lerp(b0, b1, _rcx->cfg.alpha)
    );
  } else
    _rcx->pixels[i] = p;
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
  int start = *x;
  int scale = _rcx->cfg.text_size;
  for (; *str; *str++) {
    if (*str == '\n')
      *y += 8*scale * _rcx->cfg.newline_dir,
      *x = start;
    else
      rcx_char(*x += 8*scale, *y, scale, *str);
  }
}

static void rcx_str(int x, int y, char *str) {
  rcx_str_cursor(&x, &y, str);
}

EXPORT void wq_render(Env *env, PixelDesc *pd) {
  t_begin(__FUNCTION__);

  double start_ts = env->ts();

  Rcx __rcx = { .pixels = pd->pixels, .stride = pd->stride, .raw_size = { pd->size.x, pd->size.y } };
  _rcx = &__rcx;

  /* you probably want this to be an identity matrix */
  memcpy(_rcx->cfg.mat, (float []) { 1, 0, 0, 1 }, sizeof(float[4]));
  _rcx->cfg.newline_dir = _rcx->cfg.alpha = 1;

  /* what's the biggest we can be and preserve 16x9? */
  {
    float scale = (pd->size.x/16.f < pd->size.y/9.f) ? pd->size.x/16.f : pd->size.y/9.f;
    _rcx->size.x = 16 * scale;
    _rcx->size.y =  9 * scale;
  }

  {
    t_begin("bg");

    /* clear to black */
    memset(pd->pixels, 0, pd->size.x * pd->size.y * sizeof(uint32_t));

    /* checkerboard */
    for (int y = 0; y < _rcx->size.y; y++)
      for (int x = 0; x < _rcx->size.x; x++)
        rcx_p(x, y, (y/4%2 == x/4%2) ? 0x22222222 : 0x33333333);

    t_end();
  }

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

  /* --- world space? --- */

  /* a quest of self-discovery */
  ClntEnt *you = NULL;
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    ClntEnt *e = state(env)->clnt.ents + i;
    if (e->you) { you = e; break; }
  }

  if (you) {
    State *s = state(env);

    /* where to put sword? */
    typedef enum {
      KF_Rotates = (1 << 0),
      KF_Moves   = (1 << 1),
      KF_Damages = (1 << 2),
    } KF_Flag;
    typedef struct {
      float duration;
      KF_Flag flags;
      float rot;
      float x, y;
    } KeyFrame;

    float rot = atan2f(you->y, you->x) - MATH_PI*0.45f;
    float rest_rot = rot;
    float swing = -0.5f;
    KeyFrame keys[] = {
      {0.2174f,              KF_Rotates | KF_Moves, rot-swing * 1.f },
      {0.2304f,              KF_Rotates           , rot-swing * 2.f },
      {0.0870f, KF_Damages | KF_Rotates           , rot+swing * 2.f },
      {0.2478f,              KF_Rotates           , rot+swing * 3.f },
      {0.2174f,              KF_Rotates | KF_Moves,        rest_rot },
    };
    // int hand = 0;
    // int rest = sizeof(keys)/sizeof(keys[0]) - 1;
    // kf[hand].x = 0.0f;
    // kf[hand].y = 0.0f;
    // kf[rest].x = 0.0f;
    // kf[rest].y = 0.0f;

    float out_x = 0;
    float out_y = 0;
    float out_rot = rest_rot;
    int out_dmg = 0;

    double time = fmodf(env->ts() / 1.7f, 1.0f);
    for (KeyFrame *f = keys;
        (f - keys) < sizeof(keys)/sizeof(keys[0]);
        f++
    ) {
      if (time > f->duration) {
        time -= f->duration;
        if (f->flags & KF_Rotates) out_rot = f->rot;
        if (f->flags & KF_Moves) out_x = f->x,
                                 out_y = f->y;
        continue;
      };

      float t = time / f->duration;
      if (f->flags & KF_Rotates) out_rot = lerp_rads(out_rot, f->rot, t);
      if (f->flags & KF_Moves) out_x = lerp(out_x, f->x, t),
                               out_y = lerp(out_y, f->y, t);
      if (f->flags & KF_Damages) out_dmg = 1;
      break;
    }

    /* okay put sword there */
    _rcx->cfg.origin.x = s->clnt.cam.x - out_x;
    _rcx->cfg.origin.y = s->clnt.cam.y - out_y;

    float r = out_rot;
    memcpy(_rcx->cfg.mat, (float []) {
       cosf(r)     , sinf(r),
      -sinf(r)*2.0f, cosf(r)*2.0f
    }, sizeof(float[4]));

    _rcx->cfg.text_color = 0xFFFFFFFF;
    rcx_char(
       0.0f - 5*8/2,
       0.0f - 5*8*0.2f,
      5, '!'
    );

    if (out_dmg) {
      LineHitsCircle lhc = {
        .tip.y = 5*8 * 0.75f,

        .grip.x = out_x,
        .grip.y = out_y,

        .circ.x = you->x,
        .circ.y = you->y,
        .radius = 5*8/2,
      };
      _rcx->cfg.origin.x = 0.0f;
      _rcx->cfg.origin.y = 0.0f;
      rcx_transform(&lhc.tip.x, &lhc.tip.y);

      if (line_hits_circle(&lhc))
        log(env, "ouch!");
    }

    /* hrmmgh would be cool if there was a default cfg you could just plop on */
    memcpy(_rcx->cfg.mat, (float []) { 1, 0, 0, 1 }, sizeof(float[4]));
    _rcx->cfg.origin.x = 0.0f;
    _rcx->cfg.origin.y = 0.0f;
  }

  {
    t_begin("ents");

    State *s = state(env);

    /* move cam to you */
    if (you) {
      s->clnt.cam.x = lerp(s->clnt.cam.x, you->x - _rcx->size.x/2, 0.08f);
      s->clnt.cam.y = lerp(s->clnt.cam.y, you->y - _rcx->size.y/2, 0.08f);
    }
    _rcx->cfg.text_color = 0xFFFFFFFF;

    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      ClntEnt *e = s->clnt.ents + i;
      if (e->kind == EntKind_Empty) continue;

      char c = '?';
      if (e->kind == EntKind_Player)   c = (you == e) ? 'u' : 'p';
      if (e->kind == EntKind_Fireball) c = 'o';

      rcx_char(
        e->x - s->clnt.cam.x - 4*8/2,
        e->y - s->clnt.cam.y - 4*8/2,
        4, c
      );
    }

    t_end();
  }

  /* draw map */
  {
    t_begin("map");
    State *s = state(env);


    int min_x = s->clnt.cam.x;
    int max_x = s->clnt.cam.x + _rcx->size.x;
    int min_y = s->clnt.cam.y;
    int max_y = s->clnt.cam.y + _rcx->size.y;
    for (int y = min_y; y < max_y; y++)
      for (int x = min_x; x < max_x; x++) {
        int tx = map_x_from_world(x);
        int ty = map_y_from_world(y);

        if (!map_in_bounds(tx, ty)) continue;
        if (map_index(tx, ty) == '.') continue;

        uint32_t color = 0xAA4444AA;
        if (map_index(tx, ty) == '|') {
          float cx = fabsf((x - map_x_to_world(tx)) / map_tile_world_size);
          float cy = fabsf((y - map_y_to_world(ty)) / map_tile_world_size);

          if (mag(0.5f - cx, 0.5f - cy) < 0.20f)
            color = 0xFF4444FF;
          else
            continue;
        }

        rcx_p(x - min_x, y - min_y, color);
      }

    t_end();
  }

  /* log */
  {
    t_begin("log");
    _rcx->cfg.text_color = 0xBBBBBBBB;

    // int scale = _rcx->cfg.text_size = (_rcx->size.y/20/8);
    int scale = _rcx->cfg.text_size = 1;
    int x = 0, y = 1*8*scale;
    int i = 0;
    for (LogNode *ln = state(env)->log; ln; ln = ln->header.prev) {
      if (!state(env)->log_open) {
        _rcx->cfg.alpha = 3 - (env->ts() - ln->header.ts);
        _rcx->cfg.alpha /= 3;
        if (_rcx->cfg.alpha < 0) _rcx->cfg.alpha = 0;
        _rcx->cfg.alpha = ease_out_quad(_rcx->cfg.alpha);

        float max = 6;
        if (i < max) _rcx->cfg.alpha += ease_out_quad(1.0f - (float)i/max);
        if (_rcx->cfg.alpha > 1) _rcx->cfg.alpha = 1;
      }

      rcx_str_cursor(&x, &y, ln->data);
      y += 8*scale;
      x = 0;

      i++;
    }
    _rcx->cfg.alpha = 1;

    t_end();
  }


  /* display frame time */
  {
    State *s = state(env);

    t_begin("frametime counter");
    s->frametime_ring_index = (s->frametime_ring_index + 1) % 256;
    s->frametime_ring_len += s->frametime_ring_len < 256;
    s->frametime_ring_buffer[s->frametime_ring_index] = env->ts() - start_ts;

    double average = 0.0;
    for (int i = 0; i < 256; i++) average += s->frametime_ring_buffer[i];
    average /= s->frametime_ring_len;

    char buf[16] = {0};
    snprintf(buf, 16, "%.1fms\n%dx%d", 1000*average, _rcx->size.x, _rcx->size.y);

    _rcx->cfg.newline_dir = -1;
    int scale = 8*(_rcx->cfg.text_size = 1);
    rcx_str(_rcx->size.x - scale*5,
            _rcx->size.y - scale*1, buf);

    t_end();
  }

  t_end();
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

static int host_ent_ent_collision(Env *env, HostEnt *p, EntId *out) {

  /* quadratic perf goes weeee */
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    HostEnt *e = state(env)->host.ents + i;
    if (e->kind == EntKind_Empty) continue;
    if (e == p) continue;

    float dx = e->x - p->x;
    float dy = e->y - p->y;
    if (mag(dx, dy) < 8*4) {
      *out = i;
      return 1;
    }
  }

  return 0;
}

static int host_ent_tile_collision(Env *env, HostEnt *e) {
  float ex = e->x;
  float ey = e->y;

  // int tx = map_x_from_world(ex);
  // int ty = map_y_from_world(ey);
  // if (map_in_bounds(tx, ty) && map_index(tx, ty) == '.') return;

  struct { int x, y; } offsets[] = {
    { 0, 0},
    { 1, 0},
    {-1, 0},
    { 0, 1},
    { 0,-1},
    { 1, 1},
    {-1, 1},
    {-1,-1},
    { 1,-1},
  };

  /* find closest empty tile */
  int empty_tx = -1, empty_ty = -1;
  float best_dist = 1e9;
  for (int i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {

    /* are you in a solid tile? */
    int tx = map_x_from_world(ex) + offsets[i].x;
    int ty = map_y_from_world(ey) + offsets[i].y;
    if (!map_in_bounds(tx, ty)) continue;
    if (map_index(tx, ty) == 'w') continue;

    /* find center of tile */
    int cx = map_x_to_world(tx) + map_tile_world_size/2;
    int cy = map_y_to_world(ty) + map_tile_world_size/2;

    /* from center to Ent */
    float dx = ex - cx;
    float dy = ey - cy;

    float dist = mag(dx, dy);

    if (dist < best_dist) {
      best_dist = dist;
      empty_tx = tx;
      empty_ty = ty;
    }
  }

  if (!map_in_bounds(empty_tx, empty_ty))
    e->x = e->y = 0.0f;
  else {
    /* find center of tile */
    int cx = map_x_to_world(empty_tx) + map_tile_world_size/2;
    int cy = map_y_to_world(empty_ty) + map_tile_world_size/2;

    /* unit vector from center to Ent */
    float dx = ex - cx;
    float dy = ey - cy;

    int hit = 0;
    float he =  map_tile_world_size/2;
    if (fabsf(dx) > he) hit = 1, e->x = cx + he * signf(dx);
    if (fabsf(dy) > he) hit = 1, e->y = cy + he * signf(dy);
    return hit;
  }
  return 0;
}

/* how many ticks in a second? */
#define TICK_SECOND (20)

static void host_tick(Env *env) {
  State *s = state(env);

  uint32_t tick = s->host.tick_count;
  double sec = (double)tick / (double)TICK_SECOND;

  int scratch_id = 0;

  /* lil lost spinny dude */
  *host_ent_get(env, scratch_id++) = (HostEnt) {
    .kind = EntKind_Player, 
    .x = cosf(env->ts()) * 50,
    .y = sinf(env->ts()) * 50,
  };

  /* move players */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;

    /* apply Client velocity */
    host_ent_get(env, c->pc)->x += c->vel.x * 6.0f;
    host_ent_get(env, c->pc)->y += c->vel.y * 6.0f;
    host_ent_tile_collision(env, host_ent_get(env, c->pc));
  }

  int shooter_min_x = 1e9, shooter_max_x = 0;
  for (int ty = 0; ty < map_rows(); ty++)
    for (int tx = 0; tx < map_cols(); tx++) {
      if (map_index(tx, ty) != '|') continue;

      if (tx < shooter_min_x) shooter_min_x = tx;
      if (tx > shooter_max_x) shooter_max_x = tx;
    }

  /* tutorial bullets */
  TutorialState *ts = &s->host.tutorial;
  int fb_i = 0;
  for (int ty = 0; ty < map_rows(); ty++)
    for (int tx = 0; tx < map_cols(); tx++) {
      if (map_index(tx, ty) != '|') continue;

      /* origin is center of the tile */
      float ox = map_x_to_world(tx) + map_tile_world_size/2;
      float oy = map_y_to_world(ty) + map_tile_world_size/2;

      /* shoot towards row 2 on Y axis */
      float dx = (map_x_to_world(tx) + map_tile_world_size/2) - ox;
      float dy = (map_y_to_world( 2) + map_tile_world_size/2) - oy;
      norm(&dx, &dy);
      dx *= 3.0f * map_tile_world_size;
      dy *= 3.0f * map_tile_world_size;

      /* how far along is bullet on its journey? */
      float difficulty = inv_lerp(shooter_min_x, shooter_max_x, tx);
      double cycle = sec / lerp(1.0f, 0.8f, difficulty);
      float t = fmodf(cycle, 1.0f);

      /* yeet it into enthood */
      HostEnt *fb = host_ent_get(env, scratch_id++);
      *fb = (HostEnt) {
        .kind = EntKind_Limbo,
        .x = ox + t*dx,
        .y = oy + t*dy,
      };

      int alive = ts->fb_hit_ticks[fb_i] != (int)cycle;
      if (alive) {
        fb->kind = EntKind_Fireball;

        /* hit tile = ded */
        if (host_ent_tile_collision(env, fb))
          ts->fb_hit_ticks[fb_i] = (int)cycle;

        EntId hit_id = -1;
        while (host_ent_ent_collision(env, fb, &hit_id)) {
          ts->fb_hit_ticks[fb_i] = (int)cycle;

          HostEnt *hit = host_ent_get(env, hit_id);
          if (hit->kind == EntKind_Player)
            hit->x = hit->y = 0.0f;
        }
      }

      fb_i++;
    }

  ToClntMsg msg = {
    .kind = ToClntMsgKind_EntUpd,
    .ent_upd = { .tick = tick }
  };

  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;
    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      HostEnt *e = state(env)->host.ents + i;
      if (e->kind == EntKind_Empty) continue;

      msg.ent_upd.id = i;
      msg.ent_upd.ent = (ClntEnt) {
        .kind = e->kind, 
        .you = c->pc == i,
        .x = e->x,
        .y = e->y,
      };

      if (e->kind == EntKind_Limbo)
        msg.ent_upd.ent.kind = EntKind_Empty;

      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
  }

  s->host.tick_count++;
}

/* broadcast if it's time to */
static void host_poll(Env *env) {
  State *s = state(env);

  double now = env->ts();
  if (s->host.last > 0) {
    /* accumulate delta time */
    double dt = now - s->host.last;
    s->host.dt_acc += dt;

    double FREQ = 1.0 / TICK_SECOND;
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
      .kind = EntKind_Player,
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

  if (c == WqVk_Tilde) {
    s->log_open = down;

    log(env, "recompiling ...");
    char buf[1024*2*2*2] = {0};
    int len = sizeof(buf);
    env->dbg_sys_run("cl /nologo /Zi /LD /O2 ../wq/wq.c /link /out:wq.dll", buf, &len);

    // env->dbg_sys_run(
    //   "tcc -shared -DCUSTOM_MATH=1 -o wq.dll ../wq/wq.c",
    //   buf, &len
    // );

    buf[sizeof(buf)-1] = 0; /* just in case */
    log(env, buf);
    log(env, "recompilation done!");

    env->dbg_dylib_reload();
  }


  s->clnt.keysdown[c] = down;

}
/* --- input --- */


