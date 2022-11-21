// vim: sw=2 ts=2 expandtab smartindent
//
#ifdef __LINUX__
#define EXPORT 
#else
#include <windows.h>
#define EXPORT __declspec(dllexport)
#endif

EXPORT void hello_world(char *buf) {
  *buf++ = 'h';
  *buf++ = 'e';
  *buf++ = 'l';
  *buf++ = 'l';
  *buf++ = 'o';
  *buf++ = ',';
  *buf++ = ' ';
  *buf++ = 'w';
  *buf++ = 'o';
  *buf++ = 'r';
  *buf++ = 'l';
  *buf++ = 'd';
  *buf++ = '!';
}

