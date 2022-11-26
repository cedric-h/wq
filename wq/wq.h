// vim: sw=2 ts=2 expandtab smartindent
#include <stdint.h>

#ifdef __LINUX__
  #define EXPORT 
#else
  #include <windows.h>
  #define EXPORT __declspec(dllexport)
#endif

typedef enum {
  WqVk_W     = 17,
  WqVk_S     = 31,
  WqVk_A     = 30,
  WqVk_D     = 32,
  WqVk_Esc   = 1,
  WqVk_Tilde = 41,
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

} Env;

typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } size;
} PixelDesc;

EXPORT void _wq_render(Env *env, PixelDesc *pd);
EXPORT void _wq_update(Env *env);
EXPORT void _wq_input (Env *env, char vk, int down);

#ifdef WQ_HOST_ENV // obviously dylib doesn't need to know
#include "wq/dylib.h"

typedef struct {
  void (*wq_render)(Env *env, PixelDesc *pd);
  void (*wq_update)(Env *env);
  void (*wq_input )(Env *env, char vk, int down);
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

