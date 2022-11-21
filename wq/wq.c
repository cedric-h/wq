// vim: sw=2 ts=2 expandtab smartindent
#include "wq.h"
#include "font.h"

#ifdef __LINUX__
#define EXPORT 
#else
#include <windows.h>
#define EXPORT __declspec(dllexport)
#endif

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
        rcx_p(px + x, py + y, 0xFFFFFFFF);
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
static State state = { .buf = "WASDwasd", .buf_i = sizeof("WASDwasd")-1 };

EXPORT void wq_render(uint32_t *pixels, int stride, int size_x, int size_y) {
  Rcx __rcx = { .pixels = pixels, .stride = stride, .raw_size = { size_x, size_y } };
  _rcx = &__rcx;

  /* what's the biggest we can be and preserve 16x9? */
  float scale = (size_x/16.f < size_y/9.f) ? size_x/16.f : size_y/9.f;
  _rcx->size.x = 16 * scale;
  _rcx->size.y =  9 * scale;

  /* clear to black */
  memset(pixels, 0, size_x * size_y * sizeof(uint32_t));

  for (int y = 0; y < _rcx->size.y; y++)
    for (int x = 0; x < _rcx->size.x; x++)
      rcx_p(x, y, (y/4%2 == x/4%2) ? 0x22222222 : 0x33333333);

  rcx_str(0, 0, state.buf);
}

EXPORT void wq_input(char c) {
  state.buf[state.buf_i++] = c;
}
