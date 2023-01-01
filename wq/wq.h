// vim: sw=2 ts=2 expandtab smartindent
#include <stdint.h>
#include <stddef.h>

#ifdef __LINUX__
  #define EXPORT 
#else
  #ifdef __wasm__
    #define WASM_EXPORT __attribute__((visibility("default")))
    #define EXPORT WASM_EXPORT
    #define memset __builtin_memset
    #define memcpy __builtin_memcpy
  #else
    #include <windows.h>
    #define EXPORT __declspec(dllexport)
  #endif
#endif

typedef enum {
  WqVk_W     = 17,
  WqVk_T     = 20,
  WqVk_S     = 31,
  WqVk_A     = 30,
  WqVk_D     = 32,
  WqVk_Esc   = 1,
  WqVk_Tilde = 41,
  WqVk_Space = 57,
  WqVk_MAX,
} WqVk;

/* --- net --- */
typedef struct {
  uint32_t hash;

  /* only env should touch these */
  uint8_t _store[128];
  int _store_len;
} Addr;

#if 0 /* EXAMPLE: */
int am_host = 0;
int connected = 0;
int tries = 10;

static void clnt_recv(uint8_t *buf, int len) {
  connected = 1;
  printf("received %d bytes: \"%s\"\n", len, buf);
  if (am_host) puts("(from myself!)");
}

static void host_recv(Addr *addr, uint8_t *buf, int len) {
  /* echo! */
  printf("(host) echoing back %d bytes: \"%s\"\n", len, buf);
  env_send(addr, buf, len);
}

int main(void) {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);

  for (int i = 0; i < 100; i++) {
    /* better get yourself connected */
    if (!connected) {
      printf("made %d connection attempts, sending \"hi\"\n", tries);
      env_send_to_host("hi", 3);
      tries++;

      if (!am_host && tries >= 10) {
        /* Tried 10 times to reach server ...
         * still no luck ... so I'll serve myself! */
        am_host = 1;
        puts("gonna try to host ...");
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
      if (am_host && env_host_recv(&addr, buf, &len))
        puts("host recv"), host_recv(&addr, buf, len), more = 1;

      len = sizeof(buf);
      if (env_clnt_recv(buf, &len))
        puts("clnt recv"), clnt_recv(buf, len), more = 1;
    }

    fflush(stdout);
    Sleep(10);
  }
  if (connected) puts("woot!");
  else           puts("meh");
}
#endif

/* --- net --- */

typedef struct {
  struct {
    uint8_t *buf;
    int len;
  } stash;

  struct { int x, y; } mouse;
  struct { int x, y; } win_size;

  /* NOTE: will fail if nobody's hosting */
  int (*send_to_host)(uint8_t *buf, int len);
  /* try to receive a message from the server, as a client */
  int (*clnt_recv)(uint8_t *buf, int *len);

  /* try to receive a message from a client, as the server.
   * if you start calling this you will become the server. (if possible) */
  int (*host_recv)(Addr *addr, uint8_t *buf, int *len);
  /* send to an addr. useful if you are hosting */
  int (*send)(Addr *addr, uint8_t *buf, int len);

  /* returns time since bootup in seconds */
  double (*ts)(void);

  void (*trace_begin)(char *str, size_t size);
  void (*trace_end)(void);

  void (*dbg_sys_run)(char *cmd, char *buf, int *buf_len);
  void (*dbg_dylib_reload)(void);

} Env;

#ifdef __wasm__
static void _wq_render   (Env *env, uint32_t *pixels, int stride);
static void _wq_update   (Env *env);
static void _wq_keyboard (Env *env, char vk, int down);
static void _wq_mousebtn (Env *env, int down);
static void _wq_chartyped(Env *env, char c);
#else
EXPORT void _wq_render   (Env *env, uint32_t *pixels, int stride);
EXPORT void _wq_update   (Env *env);
EXPORT void _wq_keyboard (Env *env, char vk, int down);
EXPORT void _wq_mousebtn (Env *env, int down);
EXPORT void _wq_chartyped(Env *env, char c);
#endif

#ifdef WQ_HOST_ENV // obviously dylib doesn't need to know
#include "wq/dylib.h"

typedef struct {
  void (*wq_render   )(Env *env, uint32_t *pixels, int stride);
  void (*wq_update   )(Env *env);
  void (*wq_keyboard )(Env *env, char vk, int down);
  void (*wq_mousebtn )(Env *env, int down);
  void (*wq_chartyped)(Env *env, char c);
} wq_DylibHook;

static wq_DylibHook wq_dylib_hook_init(void) {
  dylib_open();

  wq_DylibHook ret = {0};

  ret.wq_render    = dylib_get("_wq_render");
  ret.wq_update    = dylib_get("_wq_update");
  ret.wq_keyboard  = dylib_get("_wq_keyboard");
  ret.wq_mousebtn  = dylib_get("_wq_mousebtn");
  ret.wq_chartyped = dylib_get("_wq_chartyped");

  return ret;
}

#else

  #ifdef __wasm__
    extern unsigned char __heap_base;
    
    /* NOTE: will fail if nobody's hosting */
    extern int env_send_to_host(uint8_t *buf, int len);
    /* try to receive a message from the server, as a client */
    extern int env_clnt_recv(uint8_t *buf, int *len);

    /* try to receive a message from a client, as the server.
     * if you start calling this you will become the server. (if possible) */
    extern int env_host_recv(Addr *addr, uint8_t *buf, int *len);
    /* send to an addr. useful if you are hosting */
    extern int env_send(Addr *addr, uint8_t *buf, int len);

    /* returns time since bootup in seconds */
    extern double env_ts(void);

    extern void env_trace_begin(char *str, size_t size);
    extern void env_trace_end(void);

    extern void env_dbg_sys_run(char *cmd, char *buf, int *buf_len);
    extern void env_dbg_dylib_reload(void);

    #define PAGE_SIZE (1 << 16)
    Env __env = {
      .send_to_host     = env_send_to_host    ,
      .clnt_recv        = env_clnt_recv       ,

      .host_recv        = env_host_recv       ,
      .send             = env_send            ,

      .ts               = env_ts              ,

      .trace_begin      = env_trace_begin     ,
      .trace_end        = env_trace_end       ,

      .dbg_sys_run      = env_dbg_sys_run     ,
      .dbg_dylib_reload = env_dbg_dylib_reload,
    };
    Env *_env = &__env;
    uint8_t *__stash_buf = 0;
    uint32_t *pixels = 0;
    uint32_t stride = 0;

#define INIT_HACK(State)                                               \
    WASM_EXPORT uint32_t *init(int width, int height, int host) {      \
      _env->win_size.x = width;                                        \
      _env->win_size.y = height;                                       \
      stride = width;                                                  \
      pixels = (uint32_t *)&__heap_base;                               \
                                                                       \
      /* grow pages if necessary */                                    \
      size_t pixel_bytes = width * height * 4;                         \
      size_t state_bytes = sizeof(State);                              \
      size_t pages_needed = (pixel_bytes + state_bytes)/PAGE_SIZE;     \
      /* two for stack, one to round up */                             \
      int delta = pages_needed - __builtin_wasm_memory_size(0) + 3;    \
      if (delta > 0) __builtin_wasm_memory_grow(0, delta);             \
                                                                       \
      __stash_buf = &__heap_base + pixel_bytes;                        \
      /* no pixels, no client */                                       \
      ((State *)__stash_buf)->no_clnt = !(width && height);            \
      ((State *)__stash_buf)->am_host = host;                          \
      return pixels;                                                   \
    }

    EXPORT void set_mouse  (int x, int y) {
      _env->mouse.x = x;
      _env->mouse.y = y;
    }

    EXPORT void wq_render  (void) {
      _wq_render  (_env, pixels, stride);

      int npixels = _env->win_size.x*_env->win_size.y;
      uint8_t *p = (uint8_t *)pixels;
      for (int i = 0; i < npixels; i++)
        p[i*4 + 3] = 0xFF;
    }
    EXPORT void wq_update  (void) {
      _wq_update  (_env);
    }
    EXPORT void wq_keyboard(char vk, int down) {
      _wq_keyboard(_env, vk, down);
    }
    EXPORT void wq_mousebtn(int down) {
      _wq_mousebtn(_env, down);
    }
    #undef EXPORT
    #define EXPORT static
  #else
#define INIT_HACK(State) ;
  #endif

  /* good honest christian decls, no satanic DLL trickery */
  #define wq_render    _wq_render  
  #define wq_update    _wq_update  
  #define wq_keyboard  _wq_keyboard
  #define wq_mousebtn  _wq_mousebtn
  #define wq_chartyped _wq_chartyped

#endif

