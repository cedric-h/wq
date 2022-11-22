// vim: sw=2 ts=2 expandtab smartindent

#include <stdint.h>

typedef struct {
  struct {
    uint8_t *buf;
    int len;
  } stash;
} Env;

typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } size;
} PixelDesc;

void _wq_render(Env *env, PixelDesc *pd);
void _wq_update(Env *env);
void _wq_input (Env *env, char vk);

#ifdef WQ_HOST_ENV // obviously dylib doesn't need to know
#include "wq/dylib.h"

typedef struct {
  void (*wq_render)(Env *env, PixelDesc *pd);
  void (*wq_update)(Env *env);
  void (*wq_input )(Env *env, char vk);
} wq_DylibHook;

static wq_DylibHook wq_dylib_hook_init(void) {
  dylib_open();

  wq_DylibHook ret = {0};

  ret.wq_render = dylib_get("_wq_render");
  ret.wq_input  = dylib_get("_wq_input" );
  ret.wq_update = dylib_get("_wq_update");

  return ret;
}

#else

/* good honest christian decls, no satanic DLL trickery */
#define wq_render _wq_render
#define wq_update _wq_update
#define wq_input  _wq_input

#endif

