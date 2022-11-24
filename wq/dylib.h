// vim: sw=2 ts=2 expandtab smartindent

static void  dylib_open(void);
static void *dylib_get(char *path);

#ifdef __LINUX__
#include <dlfcn.h>
static void *_dylib_dl = 0;

static void  dylib_open(void) {
  _dylib_dl = dlopen("./wq.so", RTLD_LAZY);
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
  char err[1 << 12];
  FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err, sizeof(err)-1, NULL);
  printf("%s\n", err);//just for the safe case
  puts(err);
}

static void  dylib_open(void) {
  if (_dylib_dl) FreeLibrary(_dylib_dl);

  char my_dll_path[1 << 8] = {0};
  snprintf(my_dll_path, sizeof(my_dll_path), "./wq-%d.dll", GetCurrentProcessId());

  CopyFile("./wq.dll", my_dll_path, 0);

  _dylib_dl = LoadLibraryA(my_dll_path);
  if (_dylib_dl == 0) errmsg(), puts("need dll"), exit(EXIT_FAILURE);
}

static void *dylib_get(char *path) {
  void *ret = GetProcAddress(_dylib_dl, path);
  if (ret == 0) errmsg(), exit(EXIT_FAILURE);
  return ret;
}

#endif

