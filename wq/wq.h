// vim: sw=2 ts=2 expandtab smartindent

#include <stdint.h>
void _wq_render(uint32_t *pixels, int stride, int size_x, int size_y);
void _wq_input(char vk);

// typedef struct {
//   uint8_t *buf;
//   int len;
// } EnvStash;
// void      _wq_env_stash_request(EnvStash *stash);
// EnvStash *_wq_env_stash_provide(void);


#ifdef WQ_HOST_ENV // obviously dylib doesn't need to know
#include "wq/dylib.h"

typedef struct {
  void (*wq_render)(uint32_t *pixels, int stride, int size_x, int size_y);
  void (*wq_input)(char vk);
} wq_DylibHook;

static wq_DylibHook wq_dylib_hook_init(void) {
  dylib_open();

  wq_DylibHook ret = {0};

  ret.wq_render = dylib_get("_wq_render");
  ret.wq_input = dylib_get("_wq_input");

  return ret;
}

#else

/* good honest christian decls, no satanic DLL trickery */
#define wq_render _wq_render
#define wq_input  _wq_input

#endif

