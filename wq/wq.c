// vim: sw=2 ts=2 expandtab smartindent
#include <stdio.h>

#include "wq.h"
#include "font.h"

#ifdef __LINUX__
  #define EXPORT 
#else
  #include <windows.h>
  #define EXPORT __declspec(dllexport)
#endif

/* theoretically part of our contract with our environment but i was lazy */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);


/* forward decls */
static void log(Env *e, char *p);


/* --- persistent State layout --- */
typedef struct {
  int active;
  Addr addr;
} Client;
#define CLIENTS_MAX (1 << 8)

typedef struct LogNode LogNode;
struct LogNode {
  LogNode *prev;
  char data[];
};

typedef struct {
  LogNode *log;
  struct {
    int am_connected, tries, am_host;

    Client clients[CLIENTS_MAX];
  } net;
} State;

static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len > sizeof(State)) {
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);

    /* --- init state --- */
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
  } cfg;
} Rcx;

/* plot pixel */
Rcx *_rcx = 0;
static void rcx_p(int x, int y, uint32_t p) {
  x += (_rcx->raw_size.x - _rcx->size.x)/2;
  y += (_rcx->raw_size.y - _rcx->size.y)/2;
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
      if (fontdata_read(x/scale, y/scale, c))
        rcx_p(px + x, py + y, 0xFFFFFFFF); // 0xFFFF0000);
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

  /* log */
  {
    int scale = _rcx->cfg.text_size = (_rcx->size.y/20/8);
    int x = 0, y = _rcx->size.y - 2*8*scale;
    for (LogNode *ln = state(env)->log; ln; ln = ln->prev) {
      rcx_str_cursor(&x, &y, ln->data);
      y -= 8*scale;
      x = 0;
    }
  }

  // if (state(env)->net.am_host) {
  //   rcx_str(0, 1*28, "hosting");

  //   /* see how many players we are hosting */
  //   int nclient = 0;
  //   {
  //     Client *clients = state(env)->net.clients;
  //     for (int i = 0; i < CLIENTS_MAX; i++)
  //       nclient += clients[i].active;
  //   }

  //   char c = '1' + (nclient-1);
  //   rcx_str(0, 2*28, &c);
  // }
}
/* --- rcx --- */


/* --- input --- */
EXPORT void wq_input(Env *env, char c) {
  State *s = state(env);

  s->net.am_connected = s->net.tries = 0;

  log(env, "your mom is gay probably");
}
/* --- input --- */


/* --- net --- */
static Client *clients_find(Env *env, uint32_t addr_hash) {
  Client *clients = state(env)->net.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].active && clients[i].addr.hash == addr_hash)
      return clients + i;
  return NULL;
}

static Client *clients_storage(Env *env) {
  Client *clients = state(env)->net.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (!clients[i].active)
      return clients + i;
  log(env, "too many clients! dropping one");
  return NULL;
}

static void host_recv(Env *env, Addr *addr, uint8_t *buf, int len) {
  if (!clients_find(env, addr->hash))
    *clients_storage(env) = (Client) {
      .active = 1,
      .addr = *addr,
    };

  /* echo! */
  log(env, "(host) echoing back bytes");
  // printf("(host) echoing back %d bytes: \"%s\"\n", buf, len);
  env->send(addr, buf, len);

}

static void clnt_recv(Env *env, uint8_t *buf, int len) {
  state(env)->net.am_connected = 1;
}

EXPORT void wq_update(Env *env) {
  State *s = state(env);

  /* better get yourself connected */
  if (!s->net.am_connected) {
    log(env, "making connection attempt");
    // printf("made %d connection attempts, sending \"hi\"\n", s->net.tries);
    env->send_to_host("hi", 3);
    s->net.tries++;

    if (!s->net.am_host && s->net.tries >= 10) {
      /* Tried 10 times to reach server ...
       * still no luck ... so I'll serve myself! */
      s->net.am_host = 1;
      log(env, "gonna try to host ...");
    }
  }

  /* keep polling until both recvs return 0 */
  int more = 1;
  while (more) {
    uint8_t buf[1 << 8] = {0};
    int len = sizeof(buf);
    Addr addr = {0};

    more = 0;

    len = sizeof(buf);
    if (s->net.am_host && env->host_recv(&addr, buf, &len))
      host_recv(env, &addr, buf, len), more = 1;

    len = sizeof(buf);
    if (env->clnt_recv(buf, &len))
      clnt_recv(env, buf, len), more = 1;
  }
}
/* --- net --- */
