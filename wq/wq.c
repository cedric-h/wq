// vim: sw=2 ts=2 expandtab smartindent
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

typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } raw_size;
  struct { int x, y; } size;
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
        rcx_p(px + x, py + y, 0xFFFF0000);
  }
}

static void rcx_str(int x, int y, char *str) {
  int scale = 4;
  do { rcx_char(x += 8*scale, y, scale, *str++); } while(*str);
}

typedef struct {
  char buf[1 << 9];
  int buf_i;
} State;
static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len > sizeof(State)) {
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);
  }
  return (State *)env->stash.buf;
}

EXPORT void wq_render(Env *env, PixelDesc *pd) {
  Rcx __rcx = { .pixels = pd->pixels, .stride = pd->stride, .raw_size = { pd->size.x, pd->size.y } };
  _rcx = &__rcx;

  /* what's the biggest we can be and preserve 16x9? */
  float scale = (pd->size.x/16.f < pd->size.y/9.f) ? pd->size.x/16.f : pd->size.y/9.f;
  _rcx->size.x = 16 * scale;
  _rcx->size.y =  9 * scale;

  /* clear to black */
  memset(pd->pixels, 0, pd->size.x * pd->size.y * sizeof(uint32_t));

  for (int y = 0; y < _rcx->size.y; y++)
    for (int x = 0; x < _rcx->size.x; x++)
      rcx_p(x, y, (y/4%2 == x/4%2) ? 0x22222222 : 0x33333333);

  rcx_str(0, 0, state(env)->buf);
}

EXPORT void wq_input(Env *env, char c) {
  State *ste = state(env);
  ste->buf[ste->buf_i++] = c;
}

EXPORT void wq_update(Env *env) {
}
