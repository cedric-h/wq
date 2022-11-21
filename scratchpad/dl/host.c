// vim: sw=2 ts=2 expandtab smartindent

#include <stdio.h>
#include <stdlib.h>

typedef void (*HelloWorldFn)(char *);

static void  dylib_open(void);
static void *dylib_get(char *path);

#ifdef __LINUX__
#include <dlfcn.h>
static void *_dylib_dl = 0;

static void  dylib_open(void) {
  _dylib_dl = dlopen("./dl.so", RTLD_LAZY);
  if (!_dylib_dl) {
    fprintf(stderr, "%s\n", dlerror());
    exit(EXIT_FAILURE);
  }
}

static void *dylib_get(char *path) {
  void *ret = dlsym(dl, "hello_world");
  if (ret == 0) {
    fprintf(stderr, "dylib_get: %s\n", dlerror());
    exit(EXIT_FAILURE);
  }
  return ret;
}

#else
#include <Windows.h>
static void *_dylib_dl = 0;

static void errmsg(void) {
  char err[256];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, 255, NULL);
  printf("%s\n", err);//just for the safe case
  puts(err);
}

static void  dylib_open(void) {
  _dylib_dl = LoadLibraryA("./dl.dll");
  if (_dylib_dl == 0) errmsg(), exit(EXIT_FAILURE);
}

static void *dylib_get(char *path) {
  void *ret = GetProcAddress(_dylib_dl, path);
  if (ret == 0) errmsg(), exit(EXIT_FAILURE);
  return ret;
}

#endif

int main(void) {
  puts("here");

  dylib_open();

  HelloWorldFn h_w = (HelloWorldFn) dylib_get("hello_world");

  char buf[1 << 12] = {0};
  h_w(buf);
  puts(buf);
}
