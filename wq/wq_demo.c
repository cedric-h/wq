// vim: sw=2 ts=2 expandtab smartindent

#include "wq.h"

EXPORT void wq_keyboard(Env *env, char c, int down) {
}

EXPORT void wq_mousebtn(Env *env, int down) {
}

EXPORT void wq_update(Env *env) {
}

EXPORT void wq_render(Env *env, uint32_t *pixels, int stride) {
  /* clear to white */
  memset(pixels, 255, env->win_size.x * env->win_size.y * sizeof(uint32_t));

  for (int y = 0; y < env->win_size.y; y++)
    for (int x = 0; x < env->win_size.x; x++) {
      int big = 1 << 12;
      pixels[y*stride + x] = ((y + big)/4%2 == (x + big)/4%2) ? 0xFF5F4F2F : 0xFF6F5F3F;
    }
}
