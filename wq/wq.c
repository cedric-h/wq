// vim: sw=2 ts=2 expandtab smartindent
// #include <stdio.h>
// #include <math.h>
#define t_begin(str) env->trace_begin((char *)(str), sizeof(str))
#define t_end() env->trace_end()
#define ARR_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#include "wq.h"
#include "font.h"

#ifdef __wasm__
extern void print(char *, size_t);
extern void print_f(float f);
extern float fmodf(float f, float n);
extern float sqrtf(float f);
extern float cosf(float f);
extern float sinf(float f);
extern float floorf(float f);
extern float ceilf(float f);
extern float atan2f(float f, float n);
static size_t strlen(const char *str) {
  size_t len = 0;

  if (str)
    for (int i = 0; str[i]; i++)
      len++;

  return len;
}
static char *strchr(const char *str, int c) {
  char *s = 0;

  for (size_t i = 0; i <= strlen(str); i++) {
    if (str[i] == c)
      s = ((char *)str) + i;
    break;
  }

  return s;
}
#endif


/* [ ] use Rock * in sawmill
 * remove use of env->ts() for host timing
 * make Ahquicker slightly longer
 * add signaling b4 attack to 'w's, 'e's && 'm's
 * gradual health regen
 * interpolation
 * 
 * bugs:
 *   can hold more than 3 hp_pots
 *   sometimes sawminions linger during sucking,
 *   sometimes sawminions teleport at start of knockback
 *
 * combat enhancements ???
 *   "rage" bar that tracks combos?
 *   - increase speed proportional to rage
 *
 *   add "magic" bar, special attacks?
 *
 *
 *   rage for combos simply increases speed
 *   can acquire sashes that change what rage does?
 *   e.g. more rage, chance to reflect bullets
 *        more rage, quicker attacks
 *        sash where proximity to bullets gives rage
 */

/* TODO: worry about this eventually
 *   https://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian/2782742#2782742
 * and all the host stuff that uses ts instead of ticks
 * and interpolation
 * multi-room support will need some ironing out (Realm wen?)
 * and Env should be global?
 */
 
/* theoretically part of our contract with our environment but i was lazy */
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
/* TODO: write our own allocator for LogNode *
 * ... maybe just a ring buffer of chars?
 *
 * thought about this some more (too much), conclusion:
 * ring buffer of message struct containing 100 chars and ts */ 

/* rolling our own math makes it easier to smol WASM, :> */

/* -- mathematical constants -- */
#define MATH_PI  3.141592653589793
#define MATH_TAU 6.283185307179586
/* how long it take to take a swig? */
#define QUAFF_SECONDS (1.5f)
/* how long between quaff messages before we cancel your quaff? */
#define QUAFF_LAPSE (QUAFF_SECONDS/8)
#define GOLDEN_RATIO (1.61803f)

static float inline ease_out_quad(float x) { return 1 - (1 - x) * (1 - x); }

static float inline lerp(float v0, float v1, float t) { return (1 - t) * v0 + t * v1; }
#define inv_lerp(min, max, p) (((p) - (min)) / ((max) - (min)))
static float inline lerp_rads(float a, float b, float t) {
  float difference = fmodf(b - a, MATH_PI*2.0f),
        distance = fmodf(2.0f * difference, MATH_PI*2.0f) - difference;
  return a + distance * t;
}
static float inline fabsf(float f) { return f < 0 ? -f : f; }
static inline float signf(float f) { return f < 0 ? -1 : 1; }
#ifdef CUSTOM_MATH
static inline float fmodf(float f, float n) { return f - (float)(n * (int)(f/n)); }
static inline float floorf(float f) { return (int)f; }
static inline float ceilf(float f) { return (int)f + 1; }

/* thx nakst bby uwu */
// https://gitlab.com/nakst/essence/-/blob/master/shared/math.cpp
typedef union { float f; uint32_t i; } ConvertFloatInteger;
#define F(x) (((ConvertFloatInteger) { .i = (x) }).f)

static inline float _ArcTangentFloat(float x) {
	// Calculates arctan(x) for x in [0, 0.5].

	float x2 = x * x;

	return x * (F(0x3F7FFFF8) + x2 * (F(0xBEAAA53C) + x2 * (F(0x3E4BC990) + x2 * (F(0xBE084A60) + x2 * F(0x3D8864B0)))));
}

static inline float _EsCRTatanf(float x) {
	int negate = 0;

	if (x < 0) { 
		x = -x; 
		negate = 1; 
	}

	int reciprocalTaken = 0;

	if (x > 1) {
		x = 1 / x;
		reciprocalTaken = 1;
	}

	float y;

	if (x < 0.5f) {
		y = _ArcTangentFloat(x);
	} else {
		y = 0.463647609000806116f + _ArcTangentFloat((2 * x - 1) / (2 + x));
	}

	if (reciprocalTaken) {
		y = MATH_PI / 2 - y;
	}
	
	return negate ? -y : y;
}

float atan2f(float y, float x) {
	if (x == 0) return y > 0 ? MATH_PI / 2 : -MATH_PI / 2;
	else if (x > 0) return _EsCRTatanf(y / x);
	else if (y >= 0) return MATH_PI + _EsCRTatanf(y / x);
	else return -MATH_PI + _EsCRTatanf(y / x);
}

// Remez' algorithm approximation
// https://stackoverflow.com/questions/23837916/a-faster-but-less-accurate-fsin-for-intel-asm
static inline float sinf(float _x) {
  float x = fmodf(fabsf(_x), MATH_TAU) - MATH_PI;
  if (_x > 0) x = -x;

  register float xx = x*x;
  register float s =
    x + (x * xx) * (-0.16612511580269618f +
        xx * (8.0394356072977748e-3f +
          xx * -1.49414020045938777495e-4f));
  return s;
}
static inline float cosf(float x) {
  return sinf(x + MATH_PI/2);
}

static inline float sqrtf(float z) {
	union { float f; uint32_t i; } val = {z};	/* Convert type, preserving bit pattern */
	val.i -= 1 << 23;	/* Subtract 2^m. */
	val.i >>= 1;		/* Divide by 2. */
	val.i += 1 << 29;	/* Add ((b + 1) / 2) * 2^m. */

	return val.f;		/* Interpret again as float */
}
#else
#define log print_log
#endif

/* vector fns (only things that don't generalize to N=1) */
static inline float mag(float x, float y) { return sqrtf(x*x + y*y); }
static inline void norm(float *x, float *y) {
  float m = mag(*x, *y);
  if (m > 0.0f)
    *x /= m,
    *y /= m;
}
static inline void reflect(float *_vx, float *_vy, float nx, float ny) {
  float vx = *_vx;
  float vy = *_vy;

  float vdotn = vx*nx + vy*ny;
  *_vx = vx - (2 * vdotn * nx);
  *_vy = vy - (2 * vdotn * ny);
}

typedef struct {
  struct { float x, y; } grip, tip, circ;
  float radius;

  /* out */
  struct { float x, y; } closest;
} LineHitsCircle;
static int line_hits_circle(LineHitsCircle *lhc) {
  float v_x = lhc->tip.x - lhc->grip.x,
        v_y = lhc->tip.y - lhc->grip.y;
  float line_len = mag(v_x, v_y);
  norm(&v_x, &v_y);

  float u_x = lhc->circ.x - lhc->grip.x,
        u_y = lhc->circ.y - lhc->grip.y;

  /* sidecar is point where ray gets closest */
  float uv_dot = u_x*v_x + u_y*v_y;
  float sidecar_x = uv_dot * v_x;
  float sidecar_y = uv_dot * v_y;
  if (mag(sidecar_x, sidecar_y) > line_len) return 0;

  /* this is the closest the ray gets to the circle */
  float bridge_x = u_x - sidecar_x;
  float bridge_y = u_y - sidecar_y;
  float closest = mag(bridge_x, bridge_y);

  lhc->closest.x = lhc->grip.x + v_x * closest;
  lhc->closest.y = lhc->grip.y + v_y * closest;

  return closest <= lhc->radius;
}


/* forward decls */
static void log(Env *e, char *p);

/* --- map --- */
#define map_tile_world_size (90)
#define player_world_size (8*4)
typedef enum {
  MapKey_Ahquicker,
  MapKey_Ahquicker_Stuff,
  MapKey_Ahquicker_WaterCurrents,
  MapKey_MoleHole,
  MapKey_Tutorial,
  MapKey_COUNT,
} MapKey;
typedef struct {
  MapKey key;
  struct { float x, y; } world_offset;
  struct {   int x, y; } tile_size;
  int str_rows, str_cols;
} Map;

// char map_str[] = 
//   "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
//   "B....B.BvB.BvB.BvB...vBeeeeB\n"
//   "B..................!u....eee\n"
//   "B....B^B.B^B.B^B.B...^B.eeeB\n"
//   "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n";

/* you're welcome, leo */
char *map_strs[MapKey_COUNT] = {

  [MapKey_Ahquicker] = 
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBB....BBBBBBBBBBBB\n"
    "BB+++..BBBBBBBBBBBB\n"
    "BBB+++BBBBBBBBBBBBB\n"
    "B++++++++++++++.BBB\n"
    "B+BB+++++++++++..BB\n"
    "B++BBBBB.++++++..BB\n"
    "BB++BB++...+BBBBBBB\n"
    "BBB++++++...BBBBBBB\n"
    "BBBB..+++++.BBBBBBB\n"
    "BBBBBB++++++BBBBBBB\n"
    "BBBBBBBBBB++BBBBBBB\n"
    "BBBBBB++++++BBBBBBB\n"
    "BBBBBB+++..+BBBBBBB\n"
    "BBBBB+++B..BBBBBBBB\n"
    "BBBBB+++BBBBBBBBBBB\n"
    "BBBBB...BBBBBBBBBBB\n"
    "BBBBB...B.uBBBBBBBB\n"
    "BBBBBB....BBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
  ,
  [MapKey_Ahquicker_WaterCurrents] = 
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBB....BBBBBBBBBBBB\n"
    "BB>vv..BBBBBBBBBBBB\n"
    "BBBvvvBBBBBBBBBBBBB\n"
    "B>>>>>>>vv<<<<<.BBB\n"
    "B^^BBBBB>v<<<<<..BB\n"
    "BB^^BBvv.v<<<<<..BB\n"
    "BBB^<<<v...+BBBBBBB\n"
    "BBBB..>vv...BBBBBBB\n"
    "BBBB..>vvvv.BBBBBBB\n"
    "BBBBBB>>>>>vBBBBBBB\n"
    "BBBBBBBBBBvvBBBBBBB\n"
    "BBBBBBv<<<<<BBBBBBB\n"
    "BBBBBBvv<..<BBBBBBB\n"
    "BBBBBvvvB..BBBBBBBB\n"
    "BBBBBvvvBBBBBBBBBBB\n"
    "BBBBB...BBBBBBBBBBB\n"
    "BBBBB...B.uBBBBBBBB\n"
    "BBBBBB....BBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
  ,
  [MapKey_Ahquicker_Stuff] = 
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBB...hBBBBBBBBBBBB\n"
    "BB+++..BBBBBBBBBBBB\n"
    "BBB+++vvBBBBBBBBBBB\n"
    "BBB+++zzZZ+++++.BBB\n"
    "B+++++zz+ZZ++++idBB\n"
    ">++<BB^^O+ZZ+++..BB\n"
    "B>++<B++..h+BBBBBBB\n"
    "BBB+++++++.hBBBBBBB\n"
    "BBBBh.++++++BBBBBBB\n"
    "BBBBBB+++YYYBBBBBBB\n"
    "BBBBBBBBBB++BBBBBBB\n"
    "BBBBBB++++yyBBBBBBB\n"
    "BBBBBB+++i.+BBBBBBB\n"
    "BBBBB+i+B..OBBBBBBB\n"
    "BBBBB+++BBBBBBBBBBB\n"
    "BBBBB.O.BBBBBBBBBBB\n"
    "BBBBB...B.uBBBBBBBB\n"
    "BBBBBB....BBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
    "BBBBBBBBBBBBBBBBBBB\n"
  ,
#if 0
    ... maybe one day ...
  [MapKey_Ahquicker] = 
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BBB....BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BB+++..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BBB+++BBBBBBBBBBBBBBBB+++..BBBBBBBBBB\n"
    "BBB++++++++++++.+++++++++++.BBBBBBBBB\n"
    "BBBB+++++++++++.+++++..B++++BBBBBBBBB\n"
    "BBBBBBBB.++++++.....BBBBB++++BBBBBBBB\n"
    "BBBBBB++...+BBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB+++B..BBBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB+++BBBBBBBBBBBBBBBBBB++BBBBBBBBB\n"
    "BBBBB...BBBBBBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB...B.uBBBBBBBBBBBBBBBB++BBBBBBBB\n"
    "BBBBBB....BBBBBBBBB++++++BB+++BBBBBBB\n"
    "BBBBBBBBBBBBBBB++++++++++++++++BBBBBB\n"
    "BBBBBBBBBBBBB++++++++++++++++++++BBBB\n"
    "BBBBBBBBBBBB+++++++........++++++.BBB\n"
    "BBB..+++...BB++++.BBBBBBBBB.+++++.BBB\n"
    "BB+++++++++.B+++..B...+++++..++++BBBB\n"
    "B.+++++++++..+++..B..+++++B.+++++..BB\n"
    "B..+++++++++++++..B..+++++B.+++++..BB\n"
    "BB.+++...+++++++..B..+++..B..++++B.BB\n"
    "BBB+++.BB++++++..+BBBBBBBBB..++++BBBB\n"
    "BB.+++BBB.+++++..+++..++....+++++BBBB\n"
    "BB.+++BBBB.++++++++++++++..++++++BBBB\n"
    "BB.+++BBBB.+++++++++++++++++++++BBBBB\n"
    "BB.+++.BBBB.+++++++++++++++++++BBBBBB\n"
    "BB.+++.BBBBB...++++++++++++++BBBB..BB\n"
    "BBB.++++BBBBBB...+++...BBBBBBBB....BB\n"
    "BBB...+++..BBBB..+++..BBBBBBBB..+..BB\n"
    "BBBBBB.++++.BBBB.+++.BBBBBBBBB+++++BB\n"
    "BBBBBBBB.++.BBB..+++..BBBBBBBBB++BBBB\n"
    "BBBBBBB..++..BB..+++...BBBBBBB+++BBBB\n"
    "BBBBBBB.+++.BBBBB.++++...BBB..++BBBBB\n"
    "BBBBB...+++BBBBBB...+++++.B..++.BBBBB\n"
    "BBBB...+++BBBBBBBBB....++++++++.BBBBB\n"
    "BBBBB.++BBBBBBBBBBBBBBBB.+++++.BBBBBB\n"
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
  ,
  [MapKey_Ahquicker_Stuff] = 
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BBB...hBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BB+++..BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    "BBB+++vvBBBBBBBBBBBBBB+++..BBBBBBBBBB\n"
    "BBB+++zzZZ+++++.+++++++++++.BBBBBBBBB\n"
    "BBBB++zz+ZZ++++i+++++..B++++BBBBBBBBB\n"
    "BBBBBB^^O+ZZ+++.....BBBBB++++BBBBBBBB\n"
    "BBBBBB++...ZBBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB+i+B..BBBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB+++BBBBBBBBBBBBBBBBBB++BBBBBBBBB\n"
    "BBBBB.O.BBBBBBBBBBBBBBBBBB+++BBBBBBBB\n"
    "BBBBB...B.uBBBBBBBBBBBBBBBB++BBBBBBBB\n"
    "BBBBBB....BBBBBBBBB++++++BB+++BBBBBBB\n"
    "BBBBBBBBBBBBBBB++++++++++++++++BBBBBB\n"
    "BBBBBBBBBBBBB++++++++++++++++++++BBBB\n"
    "BBBBBBBBBBBB+++++++..i.....++++++.BBB\n"
    "BBB..+++...BB++++.BBBBBBBBB.+++++.BBB\n"
    "BB+++++++++iB+++.iBQ..+++++.i++++BBBB\n"
    "B.+++++++++..+++..B..+++++B.+++++..BB\n"
    "B..+++++++++++++..B..+++++B.+++++..BB\n"
    "BB.+++...+++++++.iB..+++idB..++++BhBB\n"
    "BBB+++.BB++++++..+BBBBBBBBBi.++++BBBB\n"
    "BBi+++BBB.+++++i.+++..++.i..+++++BBBB\n"
    "BB.+++BBBB.++++++++++++++..++++++BBBB\n"
    "BB.+++BBBB.+++++++++++++++++++++BBBBB\n"
    "BB.+++iBBBB.+++++++++++++++++++BBBBBB\n"
    "BB.+++.BBBBB...++++++++++++++BBBB.qBB\n"
    "BBB.++++BBBBBB...+++...BBBBBBBBi...BB\n"
    "BBB...+++.iBBBB..+++..BBBBBBBB..+..BB\n"
    "BBBBBB.++++.BBBB.+++.BBBBBBBBB+++++BB\n"
    "BBBBBBBB.++.BBB..+++..BBBBBBBBB++BBBB\n"
    "BBBBBBB..++.iBB..+++..iBBBBBBB+++BBBB\n"
    "BBBBBBB.+++.BBBBB.++++...BBB..++BBBBB\n"
    "BBBBB..i+++BBBBBB.i.+++++.Bi.++.BBBBB\n"
    "BBBBq..+++BBBBBBBBB....++++++++iBBBBB\n"
    "BBBBB.++BBBBBBBBBBBBBBBB.+++++.BBBBBB\n"
    "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
  ,
#endif
  [MapKey_MoleHole] = 
      "..BBBBBBBBBBBBBB...\n"
      ".BB...BBB......BBBB\n"
      "BB..B.....BBB.....B\n"
      "B..BB.BB.BBBBBBB..B\n"
      "Bh.B..B..B.....B..B\n"
      "BB.B..B.BB.m.m....B\n"
      "BB.BB..hBBm.d.mBBBB\n"
      "B...BBBBBB..m..BwhB\n"
      "B.....BuBB.....BB.B\n"
      "Bo..o.B.BBBBBBBBB.B\n"
      "Bo..oBB.BB..BB..B.B\n"
      "Bo..oB......n.....B\n"
      "B...BBBBBBBn.nBBB.B\n"
      "BB.BBhBBB.......B.B\n"
      ".B.BB.......BBB...B\n"
      ".B.hBBB..BBBBBw...B\n"
      ".BB..wBBBB..BB..w.B\n"
      "..BB.............BB\n"
      "...BBBBBBBBBBBBBBB.\n"
  ,
  [MapKey_Tutorial] = 
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
    ".B......Bh..B..........Bhe..B.\n"
    ".B.E....B.B.Be...B.....BBBB.B.\n"
    ".B........Be...e.B..........B.\n"
    ".B....BBBBBBBBBBBBBBBBBB.hB.B.\n"
    ".BBBBBB.BvB.BvB.BvB...vBBBB.BB\n"
    ".B.................!.........B\n"
    ".B.u..B^B.B^B.B^B.B...^B...e.B\n"
    ".BBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
  ,
};

static inline int map_flip_y(Map *map, int y) { return map->tile_size.y - 1 - y; }

static inline int map_x_from_world(Map *map, float x) {
  x += map->world_offset.x;
  if (x < 0) return -9999;
  return x / (float)map_tile_world_size;
}
static inline int map_y_from_world(Map *map, float y) {
  y += map->world_offset.y;
  if (y < 0) return -9999;
  return map_flip_y(map, y / (float)map_tile_world_size);
}

static inline float map_x_to_world(Map *map, float x)
  { return                 x *map_tile_world_size - map->world_offset.x; }
static inline float map_y_to_world(Map *map, float y)
  { return map_flip_y(map, y)*map_tile_world_size - map->world_offset.y; }

static inline char map_index(Map *map, int mx, int my)
  { return map_strs[map->key][my*map->str_cols + mx]; }
static inline int map_in_bounds(Map *map, int mx, int my) {
  if (mx < 0 || mx >= map->tile_size.x) return 0;
  if (my < 0 || my >= map->tile_size.y) return 0;
  return 1;
}

typedef struct { Map *map; int tx, ty, i; char c; } MapIter;
static int map_iter(MapIter *mi, char filter) {
  while (map_strs[mi->map->key][mi->i]) {
    mi->tx = mi->i % mi->map->str_cols;
    mi->ty = mi->i / mi->map->str_cols;
    mi->c = map_strs[mi->map->key][mi->i];
    mi->i++;
    if (mi->c == '\n') continue;
    if (!filter || filter == mi->c) return 1;
  }

  return 0;
}

static void map_init(Map *map, MapKey key) {
  memset(map, 0, sizeof(Map));

  map->key = key;
  char *str = map_strs[map->key];
  int str_size = strlen(str);

  int cols = 0;
  while (str[cols] && str[cols] != '\n') cols++;
  map->str_cols = cols + 1; /* newline */

  map->str_rows = str_size / map->str_cols;

  map->tile_size.x = map->str_cols - 1; /* newline isn't a tile */
  map->tile_size.y = map->str_rows    ;

  MapIter mi = { .map = map };
  if (map_iter(&mi, 'u')) {
    int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
    map->world_offset.x = ox;
    map->world_offset.y = oy;
  }
}
/* --- map --- */


/* --- persistent State layout --- */
typedef enum {
  ClientState_Inactive,
  ClientState_Play,
  ClientState_Dead,
} ClientState;
typedef uint32_t EntId;
typedef struct {
  Addr addr;

  ClientState state;
  uint32_t tick_state_start, tick_state_end;

  struct { float x, y; } vel;
  uint16_t hp_pots;
  uint32_t tick_quaff_earliest, tick_quaff_last;

  EntId pc; /* player character's Ent */
} Client;
#define CLIENTS_MAX (1 << 8)

typedef struct LogNode LogNode;
typedef struct {
  LogNode *prev;
  double ts;
} LogNodeHeader;
struct LogNode {
  LogNodeHeader header;
  char data[];
};

/* used by host for cross-level state */
typedef enum {
  EntKind_Empty,
  EntKind_Limbo,
  EntKind_Alive,
} EntKind;

typedef struct {
  EntKind kind;
  char looks;
  float x, y;
  uint8_t is_player, sword;
  uint16_t hp, max_hp;

  struct { uint32_t tick_start, tick_end; float dir; } swing;
} HostEnt;

/* core problem:
 * the client needs to be able to track server ents across frames,
 * to do good interpolation and to reuse memory effectively.
 *
 * the server wants to generate its ents from scratch each frame,
 * because it makes gameplay code easier to write.
 * (ex: no entity lifetime management, just code)
 * (ex: no compulsion to store all state in generic Ent,
 *      write your specific gameplay logic for this part of the map, that's fine!)
 *
 * old solution (simple but worked!):
 * have a scratch_id that you increment each time you need a new ent
 * if you spawn everything in the exact same order each frame, you're fine.
 * - nothing can just _not be spawned_
 * - nothing can be spawned in a different order
 * - reiterating over the same batches of entities is annoying
 * - (store where scratch_id is, where it was when we stopped pushing, etc.)
 *
 * current solution (just a lil more complicated >:) ):
 * the server still stores state across frames to track your ent.
 * you could generate random numbers for each ent on init, and store them
 * in that struct, ... but maybe even that much bookkeeping isn't necessary.
 * why not simply map those fixed memory addresses to ids the client can
 * use for its interpolation?
 * - you can just not spawn if you want, that Ent will stay zeroed
 * - you can spawn in whatever order you want, id will be the same
 *   (your memory didn't move!)
 * - iterating over state and getting entities is trivial
 *   (no juggling scratch_id to know what this state's entity is)
 *
 * possible optimization:
 *  - These tables are used for individual levels.
 *  - All of the level's state, that you're using pointers into
 *    to get unique, persistent data, occupies the same struct.
 *  - You don't need to put the pointers into a hash map.
 *  - Just subtract from the address of the beginning of the level's
 *    state, and index directly. It's a small enough range that you
 *    can probably allocate a sufficiently large buffer.
 *    (or even skip this indirection and reserve that many Ents)
 */
typedef struct {
  uint32_t ent_id_offset;
  void *pointer_offset;
  size_t table[500];
} EntIdTable;
static EntId eit_p(EntIdTable *eit, void *p) {
  size_t pi = (uint8_t *)p - (uint8_t *)eit->pointer_offset;

  int LEN = sizeof(eit->table) / sizeof(eit->table[0]);
  for (int i = 0; i < LEN; i++)
    if (eit->table[i] == pi)
      return eit->ent_id_offset + i;

  for (int i = 0; i < LEN; i++)
    if (eit->table[i] == 0) {
      eit->table[i] = pi;
      return eit->ent_id_offset + i;
    }

  /* TODO: something here */
  // assert(0); // EntIdTable exhausted
  return ~1;
}
static EntId eit_pi(EntIdTable *eit, void *p, int i) { return eit_p(eit, (uint8_t *)p + i); }

typedef struct {
  char looks;
  float x, y;
  uint8_t you, sword;
  uint16_t hp, max_hp;

  struct { double ts_start, ts_end; float dir; } swing;
} ClntEnt;

/* purpose of this enum:
 * - "name" effect-worthy portions of the gameplay code
 *   e.g. you can use these names for navigation
 * - tell the client to do effects when they occur
 */
typedef enum {
  SpurtKind_SparkWedge,
  SpurtKind_HpPotGulp,
  SpurtKind_PlayerDies,
  SpurtKind_HitPlayer,
  SpurtKind_SawMinionDies,
  SpurtKind_SawMasterDies,
  SpurtKind_AhqLatcherDies,
  SpurtKind_ShootyThingyAlreadyDead,
  SpurtKind_MoleChargerDies,
  SpurtKind_MoleDies,
  SpurtKind_MolePopsIn,
  SpurtKind_COUNT,
} SpurtKind;
typedef struct {
  double ts_start, ts_end;
  SpurtKind kind;
  float x, y, angle; /* angle is for wedges */
} Spurt;

typedef struct {
  char looks;
  float x, y;
  uint8_t you, sword;
  uint16_t hp, max_hp;
} EntUpd;

typedef struct {
  double ts_start, ts_end;
  struct { float x, y; } start, end;
} Rock;

static Rock *rock_recycle(Env *env, Rock *rocks, int len) {
  for (int i = 0; i < len; i++) {
    Rock *rock = rocks + i;
    if (env->ts() < rock->ts_end) continue;
    return rock;
  }
  return NULL;
}


/* - sawmill - */
typedef enum {
  SawMinionState_Init,
  SawMinionState_Think,
  SawMinionState_Charge,
  SawMinionState_Sucking,
  SawMinionState_Attack,
  SawMinionState_Dead,
} SawMinionState;
typedef struct {
  SawMinionState state;
  uint16_t hp;
  double ts_state_start, ts_state_end;
  struct { float x, y; } pos_state_start,
                         pos_state_end; /* <- meaning depends on state */
} SawMinion;

/* -- saw master -- */
typedef enum {
  BouncySawState_Init,
  BouncySawState_Going,
  BouncySawState_Gone,
  BouncySawState_Sucking,
} BouncySawState;
typedef struct {
  BouncySawState state;
  double ts_state_start, ts_state_end;

  uint32_t tick_no_hurt_till;
  struct { float x, y; } pos, vel;
} BouncySaw; 
typedef enum {
  SawMasterState_Waiting,
  SawMasterState_BlinkSucking,
  SawMasterState_Blink,
  SawMasterState_WaveStart,
  SawMasterState_BouncySaw,
  SawMasterState_MinionWave,
  SawMasterState_Damageable,
  SawMasterState_Defeated,
} SawMasterState;
typedef struct {
  SawMasterState state;
  double ts_state_start, ts_state_end;
  int times_killed;

  /* SawMasterState_BouncySaw */ int saws_launched, saws_to_launch;
  /* SawMasterState_Damageable */ int hp, max_hp;

  int bouncy_saw_count;
  BouncySaw bouncy_saws[10];

  int minion_count;
  SawMinion minions[10];
} SawMaster;
/* -- saw master -- */

typedef struct {
  Map map;

  EntIdTable eit;
  uint8_t sword_collected;
  uint8_t hp_pot_collected[10];

  uint32_t dead_on_cycle[10];
  uint32_t dead_after_cycle[10];
  SawMinion saw_minions[10];
  SawMaster saw_master;
} TutorialState;
/* - sawmill - */

/* - mole_hole - */
typedef struct {
  int alive;
  int8_t hole_index;
} Mole;
typedef struct {
  int alive;
  float x, y, dir, dir_vel, shoot_dir;
} MoleCharger;
typedef enum {
  MoleTunnelState_Init,
  MoleTunnelState_Waiting,
  MoleTunnelState_PopUp,
  MoleTunnelState_Attack,
  MoleTunnelState_SpawnCharger,
  MoleTunnelState_PopIn,
} MoleTunnelState;
typedef struct {
  MoleTunnelState state;
  int chargers_to_spawn;
  double ts_state_start, ts_state_end;

  /* MoleTunnelState_Attack */ int last_attack_mole;

  Mole moles[10];
  uint8_t _hole_ids[10];
} MoleTunnel;
typedef struct {
  Map map;
  EntIdTable eit;

  MoleTunnel tunnels[5];

  uint8_t hp_pot_collected[10];
  uint8_t charger_den_triggered[10];
  MoleCharger chargers[10];
  Rock rocks[50];
} MoleHoleState;

Mole *mole_at_hole(Mole *moles, int mole_count, int hole_index) {
  for (int i = 0; i < mole_count; i++) {
    Mole *mole = moles + i;
    if (mole->alive && mole->hole_index == hole_index)
      return mole;
  }
  return NULL;
}
Mole *mole_at_hole_any(Mole *moles, int mole_count) {
  for (int i = 0; i < mole_count; i++) {
    Mole *mole = moles + i;
    if (mole->alive && mole->hole_index > -1)
      return mole;
  }
  return NULL;
}
/* - mole_hole - */

/* - ahquicker - */
typedef enum {
  AhqPadRiderState_Walking,
  AhqPadRiderState_Mounted,
} AhqPadRiderState;
typedef struct {
  AhqPadRiderState state;
} AhqPadRider;
typedef struct {
  float x, y;
  struct { float x, y; } vel;
  uint32_t tick_last_ridden;
  EntId waffle[10];
} AhqPad;
typedef enum {
  AhqLatcherState_Dormant,
  AhqLatcherState_Init,
  AhqLatcherState_FindTarget,
  AhqLatcherState_Chase,
  AhqLatcherState_Knockback,
  AhqLatcherState_Dazed,
  AhqLatcherState_Latched,
  AhqLatcherState_Bite,
  AhqLatcherState_Dead,
} AhqLatcherState;
typedef struct {
  AhqLatcherState state;
  /* AhqLatcherState_FindTarget, */ int waffle_i; EntId target_id;
  float x, y;
  uint32_t tick_state_start, tick_state_end;

  uint8_t hp;

  /* AhqLatcherState_Knockback, */ struct { float x, y; } pos_state_start,
                                                          pos_state_end;
} AhqLatcher;
typedef enum {
  AhqStalkState_Init,
  AhqStalkState_Mean,
  AhqStalkState_Dead,
} AhqStalkState;
typedef struct {
  AhqStalkState state;
  uint8_t hp;
} AhqStalk;
typedef struct {
  uint8_t triggered;
  AhqLatcher latchers[5];
} AhqLatcherDen;
typedef struct {
  Map map;
  EntIdTable eit;

  AhqPad pads[10];
  AhqPadRider riders[CLIENTS_MAX];

  AhqLatcherDen latcher_dens[5];

  Rock rocks[100];
  AhqStalk stalks[25];
  uint8_t hp_pot_collected[10];

} AhquickerState;
/* - ahquicker - */

/* -- will probably need eventually, --
 * ... but doesn't achieve stated goal: Mole Level.
typedef struct Realm Realm;
typedef struct {
  size_t size;
  Realm *next;

  MapKey map;
  Client *players;
} RealmHeader;
struct Realm {
  Realm *next;

  char data[];
};
*/

typedef struct { uint32_t tick_until; EntId dealt_by, dealt_to; } HitTableEntry;
/* client ents are basically just a networking abstraction
 * to track a moving thing and maybe make it look good
 *
 * host ents are the server-side mirror of this, but there,
 * they double as gameplay abstractions for reusing e.g. collision detection
 *
 * not sure if that is actually useful, we may use them exclusively for the networking */
#define WQ_ENTS_MAX (1 << 10)
typedef struct {
  /* --- dbg --- */
  LogNode *log;
  int log_open;
  double frametime_ring_buffer[256];
  int frametime_ring_index, frametime_ring_len;
  /* --- dbg --- */

  struct {
    Map map;
    MapKey map_key;

    double ts_last;
    uint32_t last_known_tick;
    
    uint16_t hp_pots;
    float quaff_prog;
    double quaff_zero_since;

    int am_connected, tries;
    char keysdown[WqVk_MAX];

    ClntEnt ents[WQ_ENTS_MAX];
    struct { float x, y; } cam;
    struct { float x, y; } cursor;
#ifdef VISUALIZE_SWING
    struct { float x, y; } tip, grip;
#endif
    uint32_t seed;

    float shake_secs, blood_secs;
    Spurt spurts[100];
  } clnt;

  int am_host;
  struct {
    uint32_t tick_count;

    double last, dt_acc;
    Client clients[CLIENTS_MAX];

    HostEnt ents[WQ_ENTS_MAX];
    int next_ent;

    MapKey map_key;
    TutorialState tutorial;
    MoleHoleState mole_hole;
    AhquickerState ahquicker;

    HitTableEntry hit_table[100];
  } host;

} State;
INIT_HACK(State)

/* can't use static because DLL constantly reloading */
static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len < sizeof(State)) {
#ifdef __wasm__
    env->stash.buf = __stash_buf;
#else
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);
#endif
    env->stash.len = sizeof(State);

    /* --- init state --- */
    for (int i = 0; i < 256; i++) state(env)->frametime_ring_buffer[i] = 1/60;

    state(env)->host.next_ent += 550; /* i need some scratch ents for my mad hax */
    /* lol tick 0 is special because evals to false (im lazy ok) */
    state(env)->host.tick_count = 1;

    state(env)->clnt.seed = 0x5EED;
    state(env)->clnt.ts_last = env->ts();

    state(env)->host.map_key = MapKey_Tutorial;

    log(env, "sup nerd");
    /* --- init state --- */
  }
  return (State *)env->stash.buf;
}

static float clnt_rand(Env *env) {
  State *s = state(env);
  s->clnt.seed ^= (s->clnt.seed << 17);
  s->clnt.seed ^= (s->clnt.seed >> 13);
  s->clnt.seed ^= (s->clnt.seed << 5);
  return (float)s->clnt.seed/4294967295.0f;
}

static ClntEnt *clnt_ent_you(Env *env) {
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    ClntEnt *e = state(env)->clnt.ents + i;
    if (e->you) return e;
  }
  return NULL;
}
/* --- persistent State layout --- */


/* --- log --- */
#ifdef __wasm__
static void log(Env *env, char *p) {
  print(p, strlen(p));
}
#else
static void log(Env *env, char *p) {
  int len = strlen(p);

  /* maintain linked list */
  LogNode *ln = calloc(sizeof(LogNodeHeader) + len + 1, 1);
  ln->header.prev = state(env)->log;
  ln->header.ts = env->ts();
  state(env)->log = ln;

  /* copy in payload after header */
  memcpy((uint8_t *)ln + sizeof(LogNodeHeader), p, len);
}
#endif
/* --- log --- */


/* --- rcx --- */
typedef struct {
  uint32_t *pixels;
  int stride;
  struct { int x, y; } raw_size;
  struct { int x, y; } size;
  struct {
    int text_size;
    uint32_t text_color;
    float alpha;
    float mat[4];
    struct { float x, y; } origin;
  } cfg;
} Rcx;

static inline uint32_t rcx_color(int r, int g, int b) {
  return ((r << 0) & 0x000000FF) |
         ((g << 4) & 0x0000FF00) |
         ((b << 8) & 0x00FF0000) ;
}
static inline void rcx_decompose(uint32_t c, int *r, int *g, int *b) {
  *r = (c & 0x000000FF) >> 0;
  *g = (c & 0x0000FF00) >> 4;
  *b = (c & 0x00FF0000) >> 8;
}

Rcx *_rcx = 0;
static inline void rcx_transform(float *x, float *y) {
  float _x = *x,
        _y = *y;
  *x = _rcx->cfg.mat[0]*_x + _rcx->cfg.mat[2]*_y - _rcx->cfg.origin.x;
  *y = _rcx->cfg.mat[1]*_x + _rcx->cfg.mat[3]*_y - _rcx->cfg.origin.y;
}

static void rcx_cfg_default(void) {
  /* initialize _rcx->cfg */
  memset(&_rcx->cfg, 0, sizeof(_rcx->cfg));

  /* you probably want this to be an identity matrix */
  memcpy(_rcx->cfg.mat, (float []) { 1, 0, 0, 1 }, sizeof(float[4]));
  _rcx->cfg.text_size = _rcx->cfg.alpha = 1;
}

/* plot pixel */
static void rcx_p(int x, int y, uint32_t p) {
  /* transform in floatspace roudning to pixel ints */
  float _x = x,
        _y = y;
  rcx_transform(&_x, &_y);
  x = _x;
  y = _y;

  /* put it on the centered, 16:9 "canvas" */
  x += (_rcx->raw_size.x - _rcx->size.x)/2;
  y += (_rcx->raw_size.y - _rcx->size.y)/2;

  if (x < 0 || x >= _rcx->raw_size.x) return;
  if (y < 0 || y >= _rcx->raw_size.y) return;

  /* y is up, yall */
  y = _rcx->raw_size.y - y - 1;

  int i = y*_rcx->stride + x;
  if (_rcx->cfg.alpha < 1.0f) {
    int r0, g0, b0, r1, g1, b1;
    rcx_decompose(_rcx->pixels[i], &r0, &g0, &b0);
    rcx_decompose(              p, &r1, &g1, &b1);

    _rcx->pixels[i] = rcx_color(
      lerp(r0, r1, _rcx->cfg.alpha),
      lerp(g0, g1, _rcx->cfg.alpha),
      lerp(b0, b1, _rcx->cfg.alpha)
    );
  } else
    _rcx->pixels[i] = p;
}

typedef enum {
  RcxSpurtForm_Boom,
  RcxSpurtForm_Wedge,
  RcxSpurtForm_Fountain,
} RcxSpurtForm;
typedef struct {
  RcxSpurtForm form;
  float size, duration;
  int count;
  uint32_t color;

  /* RcxSpurtForm_Wedge */ float width;

  float x, y;
  /* RcxSpurtForm_Wedge */ float offset;
} RcxSpurtDesc;
RcxSpurtDesc spurt_kind_descs[SpurtKind_COUNT] = {
  [SpurtKind_SparkWedge] = {
    .form = RcxSpurtForm_Wedge,
    .size = 1.3f,
    .duration = 0.7f,
    .count = 12,
    .color = 0xFFFFFFFF,
    .width = 0.25f,
  },
  [SpurtKind_ShootyThingyAlreadyDead] = {
    .form = RcxSpurtForm_Wedge,
    .size = 1.3f,
    .duration = 1.7f,
    .count = 4,
    .color = 0xFFFFFFFF,
    .width = 0.45f,
  },
  [SpurtKind_PlayerDies] = {
    .form = RcxSpurtForm_Fountain,
    .size = 1.4f,
    .duration = 3.0f,
    .count = 10,
    .color = 0xF08888FF,
  },
  [SpurtKind_HitPlayer] = {
    .form = RcxSpurtForm_Wedge,
    .size = 1.7f,
    .duration = 1.1f,
    .count = 12,
    .color = 0xF08888FF,
    .width = 0.25f,
  },
  [SpurtKind_SawMasterDies] = {
    .form = RcxSpurtForm_Boom,
    .size = 2.5f,
    .duration = 1.7f,
    .count = 22,
    .color = 0xFFFFFFFF,
  },
  [SpurtKind_AhqLatcherDies] = {
    .form = RcxSpurtForm_Boom,
    .size = 1.7f,
    .duration = 1.7f,
    .count = 17,
    .color = 0xFFFFFFFF,
  },
  [SpurtKind_SawMinionDies] = {
    .form = RcxSpurtForm_Boom,
    .size = 1.7f,
    .duration = 1.7f,
    .count = 17,
    .color = 0xFFFFFFFF,
  },
  [SpurtKind_MoleChargerDies] = {
    .form = RcxSpurtForm_Boom,
    .size = 1.7f,
    .duration = 1.7f,
    .count = 12,
    .color = 0xFFFFFFFF,
  },
  [SpurtKind_MoleDies] = {
    .form = RcxSpurtForm_Boom,
    .size = 1.3f,
    .duration = 1.3f,
    .count = 13,
    .color = 0xFFFFFFFF,
  },
  [SpurtKind_MolePopsIn] = {
    .form = RcxSpurtForm_Fountain,
    .size = 0.8f,
    .duration = 0.5f,
    .count = 10,
    .color = 0xFFF5F5DC,
  },
  [SpurtKind_HpPotGulp] = {
    .form = RcxSpurtForm_Fountain,
    .size = 1.2f,
    .duration = 3.0f,
    .count = 18,
    .color = 0xFF00FF00,
  },
};
static void rcx_spurt(Env *env, Spurt *spurt) {
  RcxSpurtDesc *rsd = spurt_kind_descs + spurt->kind;

  float t = inv_lerp(spurt->ts_start, spurt->ts_end, env->ts());
  float count7 = rsd->count * 0.7f;
  for (int i = 0; i < rsd->count; i++) {
    float mag = (map_tile_world_size * 0.5f) * rsd->size;
    float fuzz_i = fmodf(i, count7)/count7;
    float it = t + t*fuzz_i;

    int x = spurt->x;
    int y = spurt->y;
    switch (rsd->form) {
      case (RcxSpurtForm_Boom): {
        x += cosf(GOLDEN_RATIO*i) * mag * it;
        y += sinf(GOLDEN_RATIO*i) * mag * it;
      } break;
      case (RcxSpurtForm_Wedge): {
        float distribute = cosf(MATH_TAU*GOLDEN_RATIO*i);
        float sign = signf(distribute);
        distribute = distribute * distribute * distribute * distribute * sign;
        distribute *= 0.5f;
        x += cosf(spurt->angle + distribute*rsd->width*MATH_TAU) * mag * it;
        y += sinf(spurt->angle + distribute*rsd->width*MATH_TAU) * mag * it;
      } break;
      case (RcxSpurtForm_Fountain): {
        int _x = mag*it * 0.7f;

        float cosi = cosf(MATH_TAU*GOLDEN_RATIO*i);
        float RANGE = 0.4157f;
        float fi = (1.0f - RANGE) + RANGE*fabsf(cosi);

        float xx = 2.0f * (it*0.9f) - 1;
        y += (1 - xx*xx) * fi * mag;
        if (cosi > 0) x -= _x;
        else          x += _x;
      } break;
    }

    if (it > 1.0f) continue;
    rcx_p(x+0, y+0, rsd->color);
    if (it > 0.75f) continue;
    rcx_p(x+0, y+1, rsd->color);
    rcx_p(x+1, y+0, rsd->color);
    rcx_p(x-0, y-1, rsd->color);
    rcx_p(x-1, y-0, rsd->color);
    if (it > 0.45f) continue;
    rcx_p(x+1, y+1, rsd->color);
    rcx_p(x-1, y+1, rsd->color);
    rcx_p(x+1, y-1, rsd->color);
    rcx_p(x-1, y-1, rsd->color);
  }
}

/* x, y in domain 0..8, c is char */
static int fontdata_read(int x, int y, char c) {
  uint8_t bits = fontdata[c*8 + y];
  return (bits >> (7-x)) & 1;
}

static void rcx_char(int px, int py, int scale, char c) {
  for (int y = 0; y < 8*scale; y++) {
    for (int x = 0; x < 8*scale; x++)
      if (fontdata_read(x/scale, 7 - y/scale, c))
        rcx_p(px + x, py + y, _rcx->cfg.text_color);
  }
}

static void rcx_str_cursor(int *x, int *y, char *str) {
  int start = *x;
  int scale = _rcx->cfg.text_size;
  for (; *str; str++) {

    if (*x > (_rcx->size.x-2*8*scale) || *str == '\n')
      *y -= 8*scale,
      *x = start;

    if (*str != '\n')
      rcx_char(*x += 8*scale, *y, scale, *str);
  }
}

static void rcx_str(int x, int y, char *str) {
  rcx_str_cursor(&x, &y, str);
}

typedef struct {
  struct { float x, y, dir, time; } in;
  struct {
    float x, y, rot;
    int dmg; /* true if should damage */
  } out;
} SwordAnim;

static void sword_anim(SwordAnim *sa) {
  /* where to put sword? */
  typedef enum {
    KF_Rotates = (1 << 0),
    KF_Moves   = (1 << 1),
    KF_Damages = (1 << 2),
  } KF_Flag;
  typedef struct {
    float duration;
    KF_Flag flags;
    float rot;
    float x, y;
  } KeyFrame;

  float rot = sa->in.dir;
  float time = sa->in.time;
  float rest_rot = rot;
  float swing = -0.5f;
  KeyFrame keys[] = {
    {0.2174f,              KF_Rotates | KF_Moves, rot-swing * 1.f },
    {0.2304f,              KF_Rotates           , rot-swing * 2.f },
    {0.0870f, KF_Damages | KF_Rotates           , rot+swing * 2.f },
    {0.2478f,              KF_Rotates           , rot+swing * 3.f },
    {0.2174f,              KF_Rotates | KF_Moves,        rest_rot },
  };
  int hand = 0;
  int rest = sizeof(keys)/sizeof(keys[0]) - 1;
  keys[hand].x = sa->in.x + cosf(rot) * 20;
  keys[hand].y = sa->in.y + sinf(rot) * 20;
  keys[rest].x = sa->in.x;
  keys[rest].y = sa->in.y;

  sa->out.x = keys[rest].x;
  sa->out.y = keys[rest].y;
  sa->out.rot = rest_rot;
  sa->out.dmg = 0;

  for (KeyFrame *f = keys;
      (f - keys) < sizeof(keys)/sizeof(keys[0]);
      f++
  ) {
    if (time > f->duration) {
      time -= f->duration;
      if (f->flags & KF_Rotates) sa->out.rot = f->rot;
      if (f->flags & KF_Moves) sa->out.x = f->x,
                               sa->out.y = f->y;
      continue;
    };

    float t = time / f->duration;
    if (f->flags & KF_Rotates) sa->out.rot = lerp_rads(sa->out.rot, f->rot, t);
    if (f->flags & KF_Moves) sa->out.x = lerp(sa->out.x, f->x, t),
                             sa->out.y = lerp(sa->out.y, f->y, t);
    if (f->flags & KF_Damages) sa->out.dmg = 1;
    break;
  }
}

static int rcx_ent_swing(Env *env, ClntEnt *ent) {
  if (ent->swing.ts_end < env->ts()) return 0;
  State *s = state(env);

  SwordAnim sa = {
    .in.time = inv_lerp(ent->swing.ts_start, ent->swing.ts_end, env->ts()),
    .in.x = ent->x,
    .in.y = ent->y,
    .in.dir = ent->swing.dir,
  };
  sword_anim(&sa);

  /* okay put sword there */
  _rcx->cfg.origin.x = s->clnt.cam.x - sa.out.x;
  _rcx->cfg.origin.y = s->clnt.cam.y - sa.out.y;

  /* TOOD: rearrange matrix members so offset isn't necessary
   * (just flip over xy?) */
  float r = sa.out.rot - MATH_PI*0.5f;
  memcpy(_rcx->cfg.mat, (float []) {
     cosf(r)     , sinf(r),
    -sinf(r)*1.0f, cosf(r)*1.0f
  }, sizeof(float[4]));

  _rcx->cfg.text_color = 0xFFFFFFFF;
  rcx_char(
    0.0f - 5*8/2,
    0.0f - 5*8*0.1f,
    5, '!'
  );

  rcx_cfg_default();

  return 1;
}


EXPORT void wq_render(Env *env, uint32_t *pixels, int stride) {
  t_begin(__FUNCTION__);

  float dt = env->ts() - state(env)->clnt.ts_last;
  state(env)->clnt.ts_last = env->ts();

  double start_ts = env->ts();

  Rcx __rcx = {
    .pixels = pixels,
    .stride = stride,
    .raw_size = { env->win_size.x, env->win_size.y }
  };
  _rcx = &__rcx;

  rcx_cfg_default();

  /* what's the biggest we can be and preserve 16x9? */
  {
    int aspect_x = env->win_size.x/16.f;
    int aspect_y = env->win_size.y/ 9.f;
    float scale = (aspect_x < aspect_y) ? aspect_x : aspect_y;
    _rcx->size.x = 16 * scale;
    _rcx->size.y =  9 * scale;
  }

  /* update cursor position based on mouse + (possibly) new size */
  {
    float x = env->mouse.x,
          y = env->mouse.y;

    /* y is up, yall */
    y = env->win_size.y - y - 1;

    /* put it on the centered, 16:9 "canvas" */
    x -= (env->win_size.x - _rcx->size.x)/2;
    y -= (env->win_size.y - _rcx->size.y)/2;

    state(env)->clnt.cursor.x = x;
    state(env)->clnt.cursor.y = y;
  }


  {
    t_begin("bg");

    /* clear to black */
    memset(pixels, 0, env->win_size.x * env->win_size.y * sizeof(uint32_t));

    t_end();
  }

  /* some net stats */
#if 0
  if (state(env)->am_host) {
    rcx_str(0, 1*28, "hosting");

    /* see how many players we are hosting */
    int nclient = 0;
    {
      Client *clients = state(env)->host.clients;
      for (int i = 0; i < CLIENTS_MAX; i++)
        nclient += clients[i].state != ClientState_Inactive;
    }

    char c = '1' + (nclient-1);
    rcx_str(0, 2*28, &c);
  }
#endif

  /* before we do any worldspace/ui, let's calc screenshake;
   * applying inverted screenshake to ui really amplifies the effect!
   */
  float shake_x = 0, shake_y = 0;
  {
    State *s = state(env);
    s->clnt.shake_secs -= dt;
    if (s->clnt.shake_secs < 0) s->clnt.shake_secs = 0;
    /* start gradually reducing shake when there's this much left */
    float down_after = 0.85f;
    float mult = (s->clnt.shake_secs < down_after)
      ? s->clnt.shake_secs/down_after
      : 1.0f;
    shake_x = (1 - 2*clnt_rand(env)) * 5 * mult;
    shake_y = (1 - 2*clnt_rand(env)) * 5 * mult;
  }
  _rcx->cfg.origin.x = shake_x;
  _rcx->cfg.origin.y = shake_y;

  /* --- world space? --- */

  {
    t_begin("map water");
    State *s = state(env);

    int min_x = s->clnt.cam.x;
    int max_x = s->clnt.cam.x + _rcx->size.x;
    int min_y = s->clnt.cam.y;
    int max_y = s->clnt.cam.y + _rcx->size.y;
    for (int y = min_y; y < max_y; y++)
      for (int x = min_x; x < max_x; x++) {
        int tx = map_x_from_world(&s->clnt.map, x);
        int ty = map_y_from_world(&s->clnt.map, y);

        if (map_in_bounds(&s->clnt.map, tx, ty) &&
            map_index(&s->clnt.map, tx, ty) == '+')
          rcx_p(x - min_x, y - min_y, 0xFF405FD0);
        else {
          int big = 1 << 12;
          rcx_p(x - min_x, y - min_y,
              ((y + big)/4%2 == (x + big)/4%2) ? 0xFF5F4F2F : 0xFF6F5F3F);
        }
      }

    t_end();
  }

  /* a quest of self-discovery */
  ClntEnt *you = clnt_ent_you(env);

  {
    State *s = state(env);
    rcx_char(s->clnt.cursor.x - 5*8/2                ,
             s->clnt.cursor.y - 5*8/2                , 5, 'x');
#ifdef VISUALIZE_SWING
    rcx_char(s->clnt.   tip.x - 3*8/2 - s->clnt.cam.x,
             s->clnt.   tip.y - 3*8/2 - s->clnt.cam.y, 3, 'x');
    rcx_char(s->clnt.  grip.x - 3*8/2 - s->clnt.cam.x,
             s->clnt.  grip.y - 3*8/2 - s->clnt.cam.y, 3, 'x');
#endif
  }

  {
    t_begin("particles");
    State *s = state(env);

    /* fountain, wedge, boom */

    _rcx->cfg.origin.x = s->clnt.cam.x;
    _rcx->cfg.origin.y = s->clnt.cam.y;

    for (int i = 0; i < ARR_LEN(s->clnt.spurts); i++) {
      Spurt *spurt = s->clnt.spurts + i;
      if (env->ts() < spurt->ts_end)
        rcx_spurt(env, spurt);
    }

    rcx_cfg_default();
    t_end();
  }

  {
    t_begin("ents");

    State *s = state(env);

    /* move cam to you */
    if (you) {
      s->clnt.cam.x = lerp(s->clnt.cam.x, you->x - _rcx->size.x/2, 0.08f);
      s->clnt.cam.y = lerp(s->clnt.cam.y, you->y - _rcx->size.y/2, 0.08f);

      s->clnt.cam.x -= shake_x;
      s->clnt.cam.y -= shake_y;
    }
    _rcx->cfg.text_color = 0xFFFFFFFF;

    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      ClntEnt *e = s->clnt.ents + i;
      if (!e->looks) continue;

      char c = e->looks;
      if (e->you) c = 'u';

      int px, py;
      rcx_char(
        px = (e->x - s->clnt.cam.x - player_world_size/2),
        py = (e->y - s->clnt.cam.y - player_world_size/2),
        4, c
      );

      if (e->sword && !rcx_ent_swing(env, e))
        rcx_char(
          px + player_world_size/2 * 1.4,
          py + player_world_size/2 * 0.8,
          3, '!'
        );

      if (e->max_hp != e->hp) {
        int y = py - player_world_size*0.4f;
        int bar_len = player_world_size * 0.8;
        int dx = (player_world_size - bar_len)/2;
        for (int i = 0; i < bar_len; i++) {
          int on = i == 0 || i == (bar_len-1) ||
            (((float)i / (float)bar_len) < ((float)e->hp / (float)e->max_hp));

          if ( 1) rcx_p(dx + px + i, y + 0, 0xFF00FF00);
          if (on) rcx_p(dx + px + i, y + 1, 0xFF00FF00);
          if ( 1) rcx_p(dx + px + i, y + 2, 0xFF00FF00);
        }
      }
    }

    t_end();
  }

  float prog = state(env)->clnt.quaff_prog;
  float qprog = inv_lerp(state(env)->clnt.quaff_zero_since + 0.5f,
                     state(env)->clnt.quaff_zero_since,
                     env->ts());
  float mprog = (prog > qprog) ? prog : qprog;
  if (you && mprog > 0) {
    t_begin("quaff prog ui");

    _rcx->cfg.origin.x = state(env)->clnt.cam.x;
    _rcx->cfg.origin.y = state(env)->clnt.cam.y;

    /* ring of pain ... not ? ... uh ... fairy ring! */
    _rcx->cfg.alpha = mprog;
    float prog = state(env)->clnt.quaff_prog;
    for (int i = 0; i < 30; i++) {
      float f = (i /    30.0f);
      float p = f * MATH_TAU;
      float t = cosf(env->ts()*3 + 3*p);
      int x = you->x + (3*t + 35)*cosf(env->ts()*0.2f + p);
      int y = you->y + (3*t + 35)*sinf(env->ts()*0.2f + p);

      uint32_t color = (prog > f) ? 0xFF00FF00 : 0xAA33AA33;
      rcx_p(x+0, y+0, color);
      rcx_p(x+0, y+1, color);
      rcx_p(x+1, y+0, color);
      rcx_p(x-0, y-1, color);
      rcx_p(x-1, y-0, color);
      if (prog < f) continue;
      rcx_p(x+1, y+1, color);
      rcx_p(x-1, y+1, color);
      rcx_p(x+1, y-1, color);
      rcx_p(x-1, y-1, color);
    }

    rcx_cfg_default();
    t_end();
  }

  {
    t_begin("map walls");
    State *s = state(env);

    int min_x = s->clnt.cam.x;
    int max_x = s->clnt.cam.x + _rcx->size.x;
    int min_y = s->clnt.cam.y;
    int max_y = s->clnt.cam.y + _rcx->size.y;
    for (int y = min_y; y < max_y; y++)
      for (int x = min_x; x < max_x; x++) {
        int tx = map_x_from_world(&s->clnt.map, x);
        int ty = map_y_from_world(&s->clnt.map, y);

        if (!map_in_bounds(&s->clnt.map, tx, ty)) continue;
        if (map_index(&s->clnt.map, tx, ty) != 'B') continue;
        rcx_p(x - min_x, y - min_y, 0xFF403020);
      }

    t_end();
  }

  /* -- okay i think it's like UI after here? -- */
  _rcx->cfg.origin.x = -shake_x;
  _rcx->cfg.origin.y = -shake_y;

  /* log */
  {
    t_begin("log");
    _rcx->cfg.text_color = 0xBBBBBBBB;

    // int scale = _rcx->cfg.text_size = (_rcx->size.y/20/8);
    int scale = _rcx->cfg.text_size = 1;
    int x = 0, y = 1*8*scale;
    int i = 0;
    for (LogNode *ln = state(env)->log; ln; ln = ln->header.prev) {
      if (!state(env)->log_open) {
        _rcx->cfg.alpha = 3 - (env->ts() - ln->header.ts);
        _rcx->cfg.alpha /= 3;
        if (_rcx->cfg.alpha < 0) _rcx->cfg.alpha = 0;
        _rcx->cfg.alpha = ease_out_quad(_rcx->cfg.alpha);

        float max = 6;
        if (i < max) _rcx->cfg.alpha += ease_out_quad(1.0f - (float)i/max);
        if (_rcx->cfg.alpha > 1) _rcx->cfg.alpha = 1;
      }

      /* render the text somewhere offscreen to figure out how big it is */
      /* i am so, so sorry to whoever is reading this */
      int fake_y = -100;
      int fake_x = 0;
      rcx_str_cursor(&fake_x, &fake_y, ln->data);

      y += -100-fake_y + scale*8;
      int yb4 = y;
      rcx_str_cursor(&x, &y, ln->data);
      y = yb4;
      x = 0;

      i++;
    }
    _rcx->cfg.alpha = 1;

    t_end();
  }


  /* display frame time */
  {
    State *s = state(env);

    t_begin("frametime counter");
    s->frametime_ring_index = (s->frametime_ring_index + 1) % 256;
    s->frametime_ring_len += s->frametime_ring_len < 256;
    s->frametime_ring_buffer[s->frametime_ring_index] = env->ts() - start_ts;

    double average = 0.0;
    for (int i = 0; i < 256; i++) average += s->frametime_ring_buffer[i];
    average /= s->frametime_ring_len;

    int len = 20;
    int scale = 8*(_rcx->cfg.text_size = 1);
#ifndef __wasm__
    char buf[32] = {0};
    snprintf(
      buf, sizeof(buf),
      "%.1fms\n%dx%d",
      1000*average, _rcx->size.x, _rcx->size.y
    );

    rcx_str(_rcx->size.x - scale*10,
            _rcx->size.y - scale*2, buf);

    memset(buf, 0, sizeof(buf));
    snprintf(buf, len, "%d/3 health potions", s->clnt.hp_pots);
    rcx_str(_rcx->size.x - scale*len,
                           scale*1, buf);
#endif

    if (s->clnt.hp_pots) {
      if ((int)(env->ts()*2)%2) _rcx->cfg.text_color = 0xFFFFFFFF;
      char *str = "SPACE TO QUAFF";
      if (s->clnt.quaff_prog > 0) str = "NOW HOLD IT";

      rcx_str(_rcx->size.x - scale*(len - 2), scale*2, str);
    }

    rcx_cfg_default();

    t_end();
  }

  {
    t_begin("blood screenspace effect");
    State *s = state(env);

    s->clnt.blood_secs -= dt;
    if (s->clnt.blood_secs < 0) s->clnt.blood_secs = 0;

    float down_after = 0.5f;
    float mult = (s->clnt.blood_secs < down_after)
      ? s->clnt.blood_secs/down_after
      : 1.0f;

    _rcx->cfg.alpha = mult * (0.3f + 0.05f*cosf(env->ts() * 20));

    if (s->clnt.blood_secs > 0 && _rcx->cfg.alpha > (1/255))
      for (int y = 0; y < _rcx->size.y; y++)
        for (int x = 0; x < _rcx->size.x; x++)
          rcx_p(x, y, 0xFFFF0000);

    rcx_cfg_default();
    t_end();
  }

  t_end();
}
/* --- rcx --- */


/* --- net --- */

static Client *clients_find(Env *env, uint32_t addr_hash) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].state && clients[i].addr.hash == addr_hash)
      return clients + i;
  return NULL;
}

static Client *clients_storage(Env *env) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].state == ClientState_Inactive)
      return clients + i;
  log(env, "too many clients! dropping one");
  return NULL;
}

static EntId host_ent_next(Env *env) {
  return state(env)->host.next_ent++;
}
static HostEnt *host_ent_get(Env *env, EntId id) {
  return state(env)->host.ents + id;
}
static EntId host_ent_id(Env *env, HostEnt *e) {
  return e - state(env)->host.ents;
}
static void eit_zero_out_host_ents(EntIdTable *eit, Env *env) {
  for (int i = 0; i < ARR_LEN(eit->table); i++)
    if (eit->table[i] > 0) {
      HostEnt *e = host_ent_get(env, i + eit->ent_id_offset);
      if (e) *e = (HostEnt) {0};
    }
}

/* how many ticks in a second? */
#define TICK_SECOND (20)

/* basically, debounces hits.
 * returns 0 if already present, inserts and returns 1 otherwise */
#define HIT_TABLE_ENTRY_DURATION_TICKS (TICK_SECOND * 0.1)
static int host_hit_table_debounce(Env *env, EntId dealt_by, EntId dealt_to) {
  State *s = state(env);
  uint32_t tick = s->host.tick_count;

  int len = ARR_LEN(s->host.hit_table);

  for (int i = 0; i < len; i++) {
    HitTableEntry *hte = s->host.hit_table + i;
    if (hte->tick_until < tick) continue;

    if (hte->dealt_by == dealt_by && hte->dealt_to == dealt_to )
      return 0;
  }

  for (int i = 0; i < len; i++) {
    HitTableEntry *hte = s->host.hit_table + i;

    if (hte->tick_until < tick) {
      *hte = (HitTableEntry) {
        .dealt_by = dealt_by,
        .dealt_to = dealt_to,
        .tick_until = tick + HIT_TABLE_ENTRY_DURATION_TICKS,
      };
      return 1;
    }
  }

  /* TODO: oh boy */
  return 0;
}


typedef struct {
  Env *env;
  HostEnt *collider;
  EntId hit_id;
  int i;
} HostEntEntCollisionState;
static int host_ent_ent_collision(HostEntEntCollisionState *heecs) {
  /* quadratic perf goes weeee */
  for (; heecs->i < WQ_ENTS_MAX; heecs->i++) {
    HostEnt *e = state(heecs->env)->host.ents + heecs->i;
    if (e->kind <= EntKind_Limbo) continue;
    if (e == heecs->collider) continue;

    float dx = e->x - heecs->collider->x;
    float dy = e->y - heecs->collider->y;
    if (mag(dx, dy) < player_world_size) {
      /* gotta make sure to increment here, wont hit loop end */
      heecs->hit_id = heecs->i++;
      return 1;
    }
  }

  return 0;
}

typedef struct {
  struct { int x, y; } normal;
} HostEntTileCollisionOut;
typedef struct {
  Env *env;
  Map *map;
  HostEnt *e;
  char *hard;
  int move_e;
} HostEntTileCollisionIn;
static int host_ent_tile_collision_ex(
  HostEntTileCollisionIn *in,
  HostEntTileCollisionOut *out
) {
  Map *map = in->map;
  HostEnt _e, *e = in->e;

  float ex = e->x;
  float ey = e->y;
  /* HAHaHAAAAAaaaa ... ha */
  if (!in->move_e) _e = *e, e = &_e;

  /* to make this robust:
   *   track tile ent was in last frame
   *   see if there's a legitimate tile-based path from there to here
   *   or maybe we don't want that! this doesn't break any gameplay yet
   *   so we'll keep it! */

  struct { int x, y; } offsets[] = {
    { 0, 0},
    { 1, 0},
    {-1, 0},
    { 0, 1},
    { 0,-1},
    { 1, 1},
    {-1, 1},
    {-1,-1},
    { 1,-1},
  };

  /* find closest empty tile */
  int empty_tx = -1, empty_ty = -1;
  int best_offset_i = -1;
  float best_dist = 1e9;
  for (int i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {

    /* are you in a solid tile? */
    int tx = map_x_from_world(map, ex + offsets[i].x * map_tile_world_size);
    int ty = map_y_from_world(map, ey + offsets[i].y * map_tile_world_size);
    if (!map_in_bounds(map, tx, ty)) continue;
    if (strchr(in->hard, map_index(map, tx, ty)) != NULL) continue;

    /* find center of tile */
    int cx = map_x_to_world(map, tx) + map_tile_world_size/2;
    int cy = map_y_to_world(map, ty) + map_tile_world_size/2;

    /* from center to Ent */
    float dx = ex - cx;
    float dy = ey - cy;

    float dist = mag(dx, dy);

    if (dist < best_dist) {
      best_offset_i = i;
      best_dist = dist;
      empty_tx = tx;
      empty_ty = ty;
    }
  }

  /* i hate it when the nearest empty tile doesn't exist */
  if (!map_in_bounds(map, empty_tx, empty_ty))
    e->x = e->y = 0.0f;
  else {
    /* find center of tile */
    int cx = map_x_to_world(map, empty_tx) + map_tile_world_size/2;
    int cy = map_y_to_world(map, empty_ty) + map_tile_world_size/2;

    /* unit vector from center to Ent */
    float dx = ex - cx;
    float dy = ey - cy;

    int hit = 0;
    float he = map_tile_world_size/2;
    if (fabsf(dx) > he) hit = 1, e->x = cx + he * signf(dx);
    if (fabsf(dy) > he) hit = 1, e->y = cy + he * signf(dy);

    if (hit && out)
      out->normal.x = offsets[best_offset_i].x,
      out->normal.y = offsets[best_offset_i].y;
    return hit;
  }
  return 0;
}
static int host_ent_tile_collision(
  Env *env, Map *map, HostEnt *e, HostEntTileCollisionOut *out
) {
  return host_ent_tile_collision_ex(
    &(HostEntTileCollisionIn) {
      .env = env,
      .map = map,
      .e = e,
      .hard = "B",
      .move_e = 1,
    },
    out
  );
}

typedef struct {
  HostEnt *hitter;
  struct { float x, y; } hit_place, swing_dir;
} HostEntSwordCollisionOut;
static int host_ent_sword_collision(Env *env, HostEnt *p, HostEntSwordCollisionOut *out) {
  uint32_t tick = state(env)->host.tick_count;

  /* quadratic perf goes weee */
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    HostEnt *e = state(env)->host.ents + i;
    if (e->kind <= EntKind_Limbo) continue;
    if (e == p) continue;
    if (!e->sword) continue;
    if (e->swing.tick_end < tick) continue;

    /* you swing: we simulate 100 sword positions, aka we do 100 sims.
     * your swing lasts 5 ticks? that's 20 sword positions per tick. */

    float t_start = e->swing.tick_start;
    float t_end = e->swing.tick_end;
    /* we simulate to the next tick */
    int sim_start = floorf(100.0f * inv_lerp(t_start, t_end, (float)tick    ));
    int sim_end   =  ceilf(100.0f * inv_lerp(t_start, t_end, (float)tick + 1));

    for (int sim = sim_start; sim < sim_end; sim++) {
      SwordAnim sa = {
        .in.time = (float)sim / 100.0f,
        .in.x = e->x,
        .in.y = e->y,
        .in.dir = e->swing.dir,
      };
      sword_anim(&sa);
      if (!sa.out.dmg) continue;

      float sword_len = 32;
      LineHitsCircle lhc = {
        .tip .x = sa.out.x + cosf(sa.out.rot)*sword_len,
        .tip .y = sa.out.y + sinf(sa.out.rot)*sword_len,

        .grip.x = sa.out.x,
        .grip.y = sa.out.y,

        .circ.x = p->x,
        .circ.y = p->y,
        .radius = 5*8/2,
      };
#ifdef VISUALIZE_SWING
      state(env)->clnt. tip.x = lhc. tip.x;
      state(env)->clnt. tip.y = lhc. tip.y;
      state(env)->clnt.grip.x = lhc.grip.x;
      state(env)->clnt.grip.y = lhc.grip.y;
#endif

      if (line_hits_circle(&lhc)) {
        if (out) {
          out->hitter = e;

          /* uhh where are we swinging towards? */
          float now_tip_x = lhc.tip.x;
          float now_tip_y = lhc.tip.y;

          sa.in.time = (float)(sim - 1) / 100.0f;
          sword_anim(&sa);
          float last_tip_x = sa.out.x + cosf(sa.out.rot)*sword_len;
          float last_tip_y = sa.out.y + sinf(sa.out.rot)*sword_len;

          out->swing_dir.x = now_tip_x - last_tip_x;
          out->swing_dir.y = now_tip_y - last_tip_y;
          norm(&out->swing_dir.x,
               &out->swing_dir.y);

          out->hit_place.x = lhc.closest.x;
          out->hit_place.y = lhc.closest.y;
        }
        EntId p_id = host_ent_id(env, p);
        EntId e_id = host_ent_id(env, e);
        return host_hit_table_debounce(env, e_id, p_id);
      }
    }
  }
  return 0;
}


/* find Client * associated with this EntId */
static Client *host_ent_client(Env *env, EntId id) {
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = state(env)->host.clients + i;
    if (c->state == ClientState_Inactive) continue;
    if (c->pc != id) continue;

    return c;
  }
  return NULL;
}

/* to client */
typedef enum {
  ToClntMsgKind_Ping,
  ToClntMsgKind_Map,
  ToClntMsgKind_EntUpd,
  ToClntMsgKind_HpPots,
  ToClntMsgKind_Swing,
  ToClntMsgKind_BloodSecs,
  ToClntMsgKind_ShakeSecs,
  ToClntMsgKind_Spurt,
} ToClntMsgKind;

typedef struct {
  ToClntMsgKind kind;
  union {
    /* _Map    */ MapKey map;
    /* _EntUpd */ struct { EntId id; uint32_t tick; EntUpd ent; } ent_upd;
    /* _Swing  */ struct { EntId id; uint32_t tick; float dir; double secs; } swing;
    /* _HpPots */ struct { float quaff_prog; uint16_t hp_pots; } hp_pots;

    /* blood, sex ... what kind of game am I making?!?? */
    /* _BloodSecs */ float add_blood_secs;
    /* _ShakeSecs */ float add_shake_secs;
    /* _Spurt */ struct { SpurtKind kind; float x, y, angle; } spurt;
  };
} ToClntMsg;

/* to host */
typedef enum {
  ToHostMsgKind_Ping,
  ToHostMsgKind_Move,
  ToHostMsgKind_Swing,
  ToHostMsgKind_Quaff,
} ToHostMsgKind;

typedef struct {
  ToHostMsgKind kind;
  union {
    /* ToHostMsgKind_Move  */ struct { float x, y; } move;
    /* ToHostMsgKind_Swing */ struct { float dir; } swing;
  };
} ToHostMsg;

static void ts_send_blood(Env *env, EntId id, float secs) {
  Client *c = host_ent_client(env, id);
  if (!c) return;

  ToClntMsg msg = { .kind = ToClntMsgKind_BloodSecs, .add_blood_secs = secs };
  env->send(&c->addr, (void *)&msg, sizeof(msg));
}

typedef struct {
  float x, y;
  float nx, ny; /* normal tells wedge where to point */
  SpurtKind kind;
} TsSpurt;
static void ts_spurt_nearby(Env *env, float dist, TsSpurt *tsprt) {
  /* TODO: localize? */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = state(env)->host.clients + i;
    if (c->state == ClientState_Inactive) continue;

    HostEnt *e = host_ent_get(env, c->pc);
    if (!e) continue;

    if (mag(e->x - tsprt->x,
            e->y - tsprt->y) < dist) {
      ToClntMsg msg = {
        .kind = ToClntMsgKind_Spurt,
        .spurt.kind = tsprt->kind,
        .spurt.x = tsprt->x,
        .spurt.y = tsprt->y,
      };

      if (fabsf(tsprt->ny + tsprt->nx) > 0)
        msg.spurt.angle = atan2f(tsprt->ny, tsprt->nx);

      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
  }
}

static void ts_spurt_nearby_hesco(
    Env *env, HostEntSwordCollisionOut *hesco,
    float dist, SpurtKind kind
) {
  ts_spurt_nearby(env, dist, &(TsSpurt) {
    .kind = kind,
    .nx   = -hesco->swing_dir.x,
    .ny   = -hesco->swing_dir.y,
    .x    = hesco->hit_place.x,
    .y    = hesco->hit_place.y,
  });
}

static void ts_shake_nearby(Env *env, float x, float y, float dist, float secs) {
  /* TODO: localize? */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = state(env)->host.clients + i;
    if (c->state == ClientState_Inactive) continue;

    HostEnt *e = host_ent_get(env, c->pc);
    if (!e) continue;

    if (mag(e->x - x,
            e->y - y) < dist) {
      ToClntMsg msg = { .kind = ToClntMsgKind_ShakeSecs, .add_shake_secs = secs };
      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
  }
}
static void ts_kill_player(Env *env, EntId id) {
  Client *c = host_ent_client(env, id);
  // HostEnt *e = host_ent_get(env, id);
  if (!c) return;

  c->state = ClientState_Dead;
  c->tick_state_start = state(env)->host.tick_count;
  c->tick_state_end   = state(env)->host.tick_count + 2.5f * TICK_SECOND;
}

static void ts_hurt_player(Env *env, EntId hit_id, float nx, float ny) {
  HostEnt *hit = host_ent_get(env, hit_id);

  hit->hp -= 1;
  ts_send_blood(env, hit_id, 0.9f);
  if (hit->hp <= 0) ts_kill_player(env, hit_id);
  
  ts_spurt_nearby(env, map_tile_world_size*3, &(TsSpurt) {
    .kind = SpurtKind_HitPlayer,
    .nx   = nx,
    .ny   = ny,
    .x    = hit->x,
    .y    = hit->y,
  });
}

static int ts_ent_hurts_player(Env *env, HostEnt *collider, float gx, float gy) {
  HostEntEntCollisionState heecs = { .env = env, .collider = collider };
  while (host_ent_ent_collision(&heecs)) {
    HostEnt *hit = host_ent_get(env, heecs.hit_id);
    if (hit->is_player) {
      float nx = collider->x - gx;
      float ny = collider->y - gy;
      norm(&nx, &ny);
      ts_hurt_player(env, heecs.hit_id, nx, ny);
      return 1;
    }
  }
  return 0;
}

static void host_rock_to_ent(Env *env, EntIdTable *eit, Map *map, Rock *rock) {
  HostEnt *rock_e = host_ent_get(env, eit_p(eit, rock));
  *rock_e = (HostEnt) { .kind = EntKind_Limbo };

  if (rock->ts_end < env->ts()) return;
  float t = inv_lerp(rock->ts_start,
                     rock->ts_end  ,
                     env->ts());
  rock_e->kind = EntKind_Alive;
  rock_e->looks = '+';
  rock_e->x = lerp(rock->start.x, rock->end.x, t);
  rock_e->y = lerp(rock->start.y, rock->end.y, t);

  /* bullets hitting stuff is cool */
  float gx = rock->end.x;
  float gy = rock->end.y;
  if (ts_ent_hurts_player(env, rock_e, gx, gy)             ||
      host_ent_tile_collision(env, map, rock_e, NULL))
    rock->ts_end = env->ts();
}

static void ts_hp_pots(Env *env, Map *map, EntIdTable *eit, uint8_t mem[]) {
  MapIter mi = { .map = map };
  for (int h_i = 0; map_iter(&mi, 'h'); h_i++) {
    uint8_t *collected = mem + h_i;

    HostEnt *h_e = host_ent_get(env, eit_p(eit, collected));
    *h_e = (HostEnt) {
      .kind = EntKind_Limbo,
      .looks = 'h',
      .x = map_x_to_world(map, mi.tx) + map_tile_world_size/2,
      .y = map_y_to_world(map, mi.ty) + map_tile_world_size/2,
    };

    if (*collected) continue;
    h_e->kind = EntKind_Alive;

    HostEntEntCollisionState heecs = { .env = env, .collider = h_e };
    while (host_ent_ent_collision(&heecs)) {
      HostEnt *hit = host_ent_get(env, heecs.hit_id);
      Client *c = hit->is_player ? host_ent_client(env, heecs.hit_id) : NULL;
      if (!c) continue;

      c->hp_pots++;
      *collected = 1;
      break;
    }
  }
}

static void map_door_tick(Env *env, Map *map, EntIdTable *eit, EntId id, MapKey next) {
  HostEnt *door = host_ent_get(env, id);

  MapIter mi = { .map = map };
  if (map_iter(&mi, 'd')) {
    /* pos is center of the tile */
    *door = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = 'd',
      .x = map_x_to_world(map, mi.tx) + map_tile_world_size/2,
      .y = map_y_to_world(map, mi.ty) + map_tile_world_size/2,
    };

    HostEntEntCollisionState heecs = { .env = env, .collider = door };
    while (host_ent_ent_collision(&heecs)) {
      HostEnt *hit = host_ent_get(env, heecs.hit_id);

      if (hit->is_player) {
        hit->x = hit->y = 0;
        state(env)->host.map_key = next;
        eit_zero_out_host_ents(eit, env);
        return;
      }
    }
  }
}

static void ts_sword(Env *env, TutorialState *ts, EntId sword_id) {
  HostEnt *sword = host_ent_get(env, sword_id);
  *sword = (HostEnt) {0};

  Map *map = &ts->map;
  MapIter mi = { .map = map };
  if (map_iter(&mi, '!')) {
    /* pos is center of the tile */
    *sword = (HostEnt) {
      .kind = EntKind_Limbo,
      .looks = '!',
      .x = map_x_to_world(map, mi.tx) + map_tile_world_size/2,
      .y = map_y_to_world(map, mi.ty) + map_tile_world_size/2,
    };

    if (ts->sword_collected) return;
    sword->kind = EntKind_Alive;

    HostEntEntCollisionState heecs = { .env = env, .collider = sword };
    while (host_ent_ent_collision(&heecs)) {
      HostEnt *hit = host_ent_get(env, heecs.hit_id);

      if (hit->is_player) {
        ts->sword_collected = 1;
        hit->sword = 1;
      }
    }
  }
}

static void ts_bouncy_saw_tick(Env *env, TutorialState *ts, BouncySaw *bs, float ox, float oy) {
  HostEnt *bse = host_ent_get(env, eit_p(&ts->eit, bs));
  *bse = (HostEnt) { .kind = EntKind_Limbo, .looks = 'O' };
  switch (bs->state) {

    case (BouncySawState_Init): {
      bs->state = BouncySawState_Going;
    } break;

    case (BouncySawState_Sucking): {
      float t = inv_lerp(bs->ts_state_start, bs->ts_state_end, env->ts());
      if (t > 1) t = 1;
      bse->kind = ((int)(env->ts() * 1000)/20 % 3 == 2)
        ? EntKind_Limbo
        : EntKind_Alive;
      bse->x = lerp(bs->pos.x, ox, t);
      bse->y = lerp(bs->pos.y, oy, t);
    } break;

    case (BouncySawState_Going): {
      bse->kind = EntKind_Alive;
      bse->x = bs->pos.x += bs->vel.x * 4.2f;
      bse->y = bs->pos.y += bs->vel.y * 4.2f;

      HostEntTileCollisionOut hetco = {0};
      if (host_ent_tile_collision(env, &ts->map, bse, &hetco)) {
        reflect(&bs->vel.x,
                &bs->vel.y,
                            hetco.normal.x,
                            hetco.normal.y);
        norm(&bs->vel.x,
             &bs->vel.y);
      }

      /* ouch! tears clear through ya */
      uint32_t tick = state(env)->host.tick_count;
      int can_hurt = bs->tick_no_hurt_till <= tick;
      if (can_hurt && ts_ent_hurts_player(env, bse,
                                         bs->pos.x + bs->vel.x,
                                         bs->pos.y + bs->vel.y))
        bs->tick_no_hurt_till = tick + TICK_SECOND/2;
    } break;

    case (BouncySawState_Gone): {
      /* ??? */
    } break;

  }
}

static void nearest_player(
    Env *env,
    float x, float y,
    float *_best_dist, EntId *_best_target_id
) {
  float best_dist = _best_dist ? *_best_dist : 1e9;

  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    HostEnt *e = state(env)->host.ents + i;
    if (e->kind <= EntKind_Limbo) continue;
    if (!e->is_player) continue;

    float dist = mag(e->x - x,
                     e->y - y);
    if (dist < best_dist) {
      if (_best_dist     ) *_best_dist      = dist;
      if (_best_target_id) *_best_target_id = i;
    }
  }
}

typedef struct {
  Env *env;
  TutorialState *ts;
  float init_x, init_y;
  int sm_i;
} TsSawMinionTickIn;
static void ts_saw_minion_tick(SawMinion *sm, TsSawMinionTickIn *in) {
  Env *env = in->env;
  Map *map = &in->ts->map;
  EntIdTable *eit = &in->ts->eit;
  int sm_i = in->sm_i;

  HostEnt *sme = host_ent_get(env, eit_p(eit, sm));
  *sme = (HostEnt) { .kind = EntKind_Alive, .looks = 'e' };

  HostEnt *fbe = host_ent_get(env, eit_pi(eit, sm, 1));
  *fbe = (HostEnt) { .kind = EntKind_Limbo, .looks = 'o' };

  int SAW_MINION_MAX_HP = 2;

  double WAIT = 0.8;
  float ATTACK_DIST_MIN = player_world_size * 1.25;
  float ATTACK_DIST_MAX = map_tile_world_size;
  float t = (sm->state == SawMinionState_Init)
    ? 0
    : inv_lerp(sm->ts_state_start, sm->ts_state_end, env->ts());
  if (t > 1) t = 1;

  switch (sm->state) {

    case (SawMinionState_Init): {
      /* saw minion is at tile center */
      float ox = in->init_x;
      float oy = in->init_y;

      sme->x = sm->pos_state_start.x = ox;
      sme->y = sm->pos_state_start.y = oy;

      sm->hp = SAW_MINION_MAX_HP;
      sm->state = SawMinionState_Think;
      sm->ts_state_start = env->ts();
      sm->ts_state_end   = env->ts() + WAIT;
    } break;

    case (SawMinionState_Dead): {
      sme->kind = EntKind_Limbo;
    } break;

    case (SawMinionState_Think): {
      sme->x = sm->pos_state_start.x;
      sme->y = sm->pos_state_start.y;

      if (t < 1.0) break;

      /* find nearest player in ent list */
      float best_dist = 3 * map_tile_world_size;
      EntId best_target_id = -1;
      nearest_player(env, sme->x, sme->y, &best_dist, &best_target_id);
      if (best_target_id == -1) break;
      HostEnt *best_target = host_ent_get(env, best_target_id);

      /* fuzz target slightly to distribute minions on ring around player */
      float ring_size = best_dist * 0.3f;
      if (ring_size < ATTACK_DIST_MIN) ring_size = ATTACK_DIST_MIN;

      /* golden ratio should give us evenish ring distribution around target */
      float target_x = best_target->x + cosf(GOLDEN_RATIO*sm_i) * ring_size;
      float target_y = best_target->y + sinf(GOLDEN_RATIO*sm_i) * ring_size;
      float dx = target_x - sme->x;
      float dy = target_y - sme->y;
      best_dist = mag(dx, dy);
      norm(&dx, &dy);

      /* move/shoot dist */
      float action_dist = map_tile_world_size;

      float adt = inv_lerp(ATTACK_DIST_MIN, ATTACK_DIST_MAX, best_dist);
      /* great, we're the proper distance away to attack */
      if (adt >= 0 && adt <= 1.0f) {
        sm->state = SawMinionState_Attack;

        /* make sure to actually shoot the player */
        dx = best_target->x - sme->x;
        dy = best_target->y - sme->y;
        norm(&dx, &dy);
      }
      else {
        sm->state = SawMinionState_Charge;

        float ideal = (adt < 0) ? ATTACK_DIST_MIN : ATTACK_DIST_MAX;

        /* in which direction and how far do we need to go to get to ideal? */
        /* ex: we need to back up, ideal is MIN, best_dist is slightly under.
         * this will be negative, which makes sense! */
        float delta = best_dist - ideal;
        action_dist = delta;
      }

      /* let's reevaluate after a bit, though */ 
      if (action_dist >  map_tile_world_size) action_dist =  map_tile_world_size;
      if (action_dist < -map_tile_world_size) action_dist = -map_tile_world_size;

      double SECS_PER_PIXEL = WAIT / map_tile_world_size;

      sm->ts_state_start = env->ts();
      sm->ts_state_end   = env->ts() + (double)action_dist * SECS_PER_PIXEL;
      sm->pos_state_end.x = sm->pos_state_start.x + dx*action_dist;
      sm->pos_state_end.y = sm->pos_state_start.y + dy*action_dist;
    } break;

    case (SawMinionState_Attack): {
      sme->x = sm->pos_state_start.x;
      sme->y = sm->pos_state_start.y;

      float gx = sm->pos_state_end.x;
      float gy = sm->pos_state_end.y;
      fbe->x = lerp(sm->pos_state_start.x, gx, t);
      fbe->y = lerp(sm->pos_state_start.y, gy, t);
      fbe->kind = EntKind_Alive;

      /* bullets hitting stuff is cool */
      if (ts_ent_hurts_player(env, fbe, gx, gy))
        sm->state = SawMinionState_Think;

      if (t < 1.0) break;
      sm->state = SawMinionState_Think;
      sm->ts_state_start = env->ts();
      sm->ts_state_end   = env->ts() + WAIT;
    } break;

    case (SawMinionState_Charge): {
      sme->x = lerp(sm->pos_state_start.x, sm->pos_state_end.x, t);
      sme->y = lerp(sm->pos_state_start.y, sm->pos_state_end.y, t);

      if (t < 1.0) break;
      sm->state = SawMinionState_Think;
      sm->pos_state_start.x = sm->pos_state_end.x;
      sm->pos_state_start.y = sm->pos_state_end.y;
      sm->ts_state_start = env->ts();
      sm->ts_state_end   = env->ts() + WAIT;
    } break;

    case (SawMinionState_Sucking): {
      sme->kind = ((int)(env->ts() * 1000)/20 % 3 == 1)
        ? EntKind_Limbo
        : EntKind_Alive;
      sme->x = lerp(sm->pos_state_start.x, sm->pos_state_end.x, t);
      sme->y = lerp(sm->pos_state_start.y, sm->pos_state_end.y, t);
    }
  }

  /* if you hit a wall, rethink your life */
  int dead = sm->state == SawMinionState_Dead;
  if (!dead && host_ent_tile_collision(env, map, sme, NULL)) {
    sm->state = SawMinionState_Think;

    sm->pos_state_start.x = sme->x;
    sm->pos_state_start.y = sme->y;
    sm->ts_state_start = env->ts();
    sm->ts_state_end   = env->ts() + WAIT;
  }

  /* did you swipe our saw minion!? */
  int damagable = !dead;
  HostEntSwordCollisionOut hesco = {0};
  if (damagable && host_ent_sword_collision(env, sme, &hesco)) {
    ts_spurt_nearby_hesco(
      env, &hesco,
      map_tile_world_size * 3.0f, SpurtKind_SparkWedge);

    sm->hp -= 1;
    if (sm->hp <= 0) {
      sm->state = SawMinionState_Dead;

      ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
        .kind = SpurtKind_SawMinionDies,
        .x    = sme->x,
        .y    = sme->y,
      });
    } else {
      /* knockback! */
      // float dx = sme->x - hesco.hitter->x;
      // float dy = sme->y - hesco.hitter->y;
      // norm(&dx, &dy);
      float dx = hesco.swing_dir.x;
      float dy = hesco.swing_dir.y;

      double SECS_PER_PIXEL = (WAIT / (map_tile_world_size * 3));
      float action_dist = map_tile_world_size;

      sm->state = SawMinionState_Charge;
      sm->ts_state_start = env->ts();
      sm->ts_state_end   = env->ts() + (double)action_dist * SECS_PER_PIXEL;
      sm->pos_state_end.x = sm->pos_state_start.x + dx*action_dist;
      sm->pos_state_end.y = sm->pos_state_start.y + dy*action_dist;
    }
  }

  sme->hp = sm->hp;
  sme->max_hp = SAW_MINION_MAX_HP;
}

static void ts_saw_minion_tick_collision_pass(Env *env, TutorialState *ts, SawMinion sms[], int count) {
  EntIdTable *eit = &ts->eit;
  // Map *map = &ts->map;

  for (int sm_i = 0; sm_i < count; sm_i++) {
    HostEnt *sme = host_ent_get(env, eit_p(eit, sms + sm_i));

    HostEntEntCollisionState heecs = { .env = env, .collider = sme };
    while (host_ent_ent_collision(&heecs)) {
      HostEnt *hit = host_ent_get(env, heecs.hit_id);

      /* you can only collide with other saw minions */
      for (int i = 0; i < count; i++) {
        if (eit_p(eit, sms + i) == heecs.hit_id)
          goto SAW_MINION_IDENTIFIED;
      }
      continue;
  SAW_MINION_IDENTIFIED:
      ;

      float dx = hit->x - sme->x;
      float dy = hit->y - sme->y;

      float dmag = mag(dx, dy);
      float ideal = player_world_size * 0.4f;
      /* we only care if the distance is smaller than our ideal */
      if (dmag > ideal) continue;
      float delta = ideal - dmag;

      norm(&dx, &dy);
      sme->x -= dx * delta/2;
      sme->y -= dy * delta/2;
      hit->x += dx * delta/2;
      hit->y += dy * delta/2;
    }
  }
}

static void host_tick_players(Env *env) {
  State *s = state(env);
  uint32_t tick = s->host.tick_count;

  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (c->state == ClientState_Inactive) continue;

    HostEnt *pc_e = host_ent_get(env, c->pc);
    float t = ((c->tick_state_end - c->tick_state_start) != 0)
      ? inv_lerp(c->tick_state_start, c->tick_state_end, s->host.tick_count)
      : 0;
    switch (c->state) {

      case (ClientState_Play): {
        pc_e->kind = EntKind_Alive;

        /* apply Client velocity */
        float speed = 6.0f;

        int ticks_since_last = tick - c->tick_quaff_last;
        if (ticks_since_last < TICK_SECOND*QUAFF_LAPSE)
          speed = 2.0f;

        pc_e->x += c->vel.x * speed;
        pc_e->y += c->vel.y * speed;
      } break;

      case (ClientState_Dead): {
        pc_e->kind = EntKind_Limbo;
        int range = c->tick_state_end - c->tick_state_start;
        if ((s->host.tick_count)%(range/3) == 2)
          ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
            .kind = SpurtKind_PlayerDies,
            .x    = pc_e->x,
            .y    = pc_e->y,
          });

        if (t < 1.0f) break;
        log(env, "i weep for you, truly.");

        pc_e->x = pc_e->y = 0.0f;
        pc_e->hp = pc_e->max_hp;
        c->state = ClientState_Play;
      } break;

      default: break;

    }
  }
}

static int mole_hole_tunnel_tick(
  Env        *env,
  MoleHoleState *mh_s, MoleTunnel *tun,
  char hole_char, int chargers_to_spawn
) {
  Map *map = &mh_s->map;
  EntIdTable *eit = &mh_s->eit;

  int hole_count = 0;
  for (MapIter mi = { .map = map }; map_iter(&mi, hole_char); hole_count++)
    ;
  int mole_count = hole_count - 1;

  int mole_count_living = 0;
  for (int i = 0; i < mole_count; i++)
    mole_count_living += tun->moles[i].alive;

  float t = (tun->ts_state_start && tun->ts_state_end)
    ? inv_lerp(tun->ts_state_start,
               tun->ts_state_end  ,
               env->ts())
    : 0.0f;

  int defeated = (mole_count_living == 0) &&
    (tun->state != MoleTunnelState_Init);
  if (!defeated) switch (tun->state) {

    case (MoleTunnelState_Init): {
      tun->last_attack_mole = -1;
      for (int i = 0; i < mole_count; i++)
        tun->moles[i].hole_index = -1,
        tun->moles[i].alive = 1;

      tun->chargers_to_spawn = chargers_to_spawn;
      tun->state = MoleTunnelState_Waiting;
      tun->ts_state_start = env->ts();
      tun->ts_state_end   = env->ts() + 3.0f;
    } break;

    case (MoleTunnelState_Waiting): {
      tun->last_attack_mole = -1;

      for (MapIter mi = { .map = map }; map_iter(&mi, hole_char);) {
        /* hole is at center of tile */
        int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
        int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;

        float dist = 1e9;
        nearest_player(env, ox, oy, &dist, NULL);
        if (dist < 1.85f*map_tile_world_size)
          tun->state = MoleTunnelState_PopUp;
      }
    } break;

    case (MoleTunnelState_PopUp): {
      if (t < 1.0f) break;

      /* find the next mole that needs assigning */
      Mole *homeless_mole = mole_at_hole(tun->moles, mole_count, -1);

      /* if everyone's assigned, this stage is over */
      if (!homeless_mole) {
        tun->state = MoleTunnelState_Attack;
        tun->ts_state_start = env->ts();
        tun->ts_state_end   = env->ts() + 0.3f;
        break;
      }

      /* find the farthest-from-harm unassigned hole and assign it */
      int best_hole_i = 0;
      float best_dist = 0.0f;

      MapIter mi = { .map = map };
      for (int h_i = 0; map_iter(&mi, hole_char); h_i++) {
        /* can't give it to you if someone already lives there */
        if (mole_at_hole(tun->moles, mole_count, h_i))
          continue;

        /* hole is at center of tile */
        int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
        int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
        float dist = 1e9;
        nearest_player(env, ox, oy, &dist, NULL);

        if (dist > best_dist) {
          best_hole_i = h_i;
          best_dist = dist;
        }
      }

      homeless_mole->hole_index = best_hole_i;

      /* "resetting" this state so there's a wait between mole pop ups */
      tun->ts_state_start = env->ts();
      tun->ts_state_end   = env->ts() + 0.295f;
    } break;

    case (MoleTunnelState_Attack): {
      if (t < 1.0f) break;

      do {
        tun->last_attack_mole++;
      } while (tun->last_attack_mole < mole_count       &&
               !tun->moles[tun->last_attack_mole].alive);

      if (tun->last_attack_mole >= mole_count) {
        tun->last_attack_mole = -1;
        tun->state = MoleTunnelState_SpawnCharger;
        break;
      }

      Mole *mole = tun->moles + tun->last_attack_mole;
      HostEnt *mole_e = host_ent_get(env, eit_p(eit, mole));

      EntId victim_id = -1;
      nearest_player(env, mole_e->x, mole_e->y, NULL, &victim_id);
      if (victim_id == -1) break;

      HostEnt *victim = host_ent_get(env, victim_id);
      Rock *rock = rock_recycle(env, mh_s->rocks, ARR_LEN(mh_s->rocks));
      if (rock) {
        float dx = victim->x - mole_e->x;
        float dy = victim->y - mole_e->y;
        norm(&dx, &dy);
        float secs = 1.95f;
        rock->start.x = mole_e->x;
        rock->start.y = mole_e->y;
        rock->end.x = mole_e->x + dx * map_tile_world_size*0.85f*secs;
        rock->end.y = mole_e->y + dy * map_tile_world_size*0.85f*secs;
        rock->ts_start = env->ts();
        rock->ts_end   = env->ts() + secs;
      }

      tun->ts_state_start = env->ts();
      tun->ts_state_end   = env->ts() + 0.5f;
    } break;

    case (MoleTunnelState_SpawnCharger): {

      /* set up the next state, but it ain't over till we break; */
      tun->state = MoleTunnelState_PopIn;
      tun->ts_state_start = env->ts();
      tun->ts_state_end   = env->ts() + 0.1f;

      if (tun->chargers_to_spawn == 0) break;
      tun->chargers_to_spawn--;

      for (int i = 0; i < ARR_LEN(mh_s->chargers); i++) {
        MoleCharger *mc = mh_s->chargers + i;
        if (mc->alive) continue;

        /* find empty hole for charger */
        MapIter mi = { .map = map };
        for (int h_i = 0; map_iter(&mi, hole_char); h_i++) {
          /* can't give it to you if someone already lives there */
          if (mole_at_hole(tun->moles, mole_count, h_i))
            continue;

          /* hole is at center of tile */
          int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
          int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;

          mc->alive = 1;
          mc->dir = fmodf(env->ts(), MATH_TAU);
          mc->x = ox;
          mc->y = oy;
          break;
        }
        break;
      }
    } break;

    case (MoleTunnelState_PopIn): {
      if (t < 1.0f) break;

      /* find a mole out in the open */
      Mole *naked_mole = mole_at_hole_any(tun->moles, mole_count);

      /* if everyone's tucked inside, this stage is over */
      if (!naked_mole) {
        tun->state = MoleTunnelState_PopUp;
        tun->ts_state_start = env->ts();
        tun->ts_state_end   = env->ts() + 0.4f;
        break;
      }

      /* hide that sucker */
      naked_mole->hole_index = -1;
      ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
        .kind = SpurtKind_MolePopsIn,
        .x    = host_ent_get(env, eit_p(eit, naked_mole))->x,
        .y    = host_ent_get(env, eit_p(eit, naked_mole))->y,
      });

      /* "resetting" this state so there's a wait between mole pop ups */
      tun->ts_state_start = env->ts();
      tun->ts_state_end   = env->ts() + 0.275f;
    } break;

  }

  /* "render" the holes to ents */
  MapIter mi = { .map = map };
  for (int h_i = 0; map_iter(&mi, hole_char); h_i++) {
    int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
    Mole *guest_mole = mole_at_hole(tun->moles, mole_count, h_i);

    HostEnt *hole_e = host_ent_get(env, eit_p(eit, tun->_hole_ids + h_i));
    *hole_e = (HostEnt) {
      .kind = guest_mole ? EntKind_Limbo : EntKind_Alive,
      .looks = '.',
      .x = ox, .y = oy,
    };
    if (!guest_mole) continue;

    size_t mole_i = guest_mole - tun->moles;
    HostEnt *mole_e = host_ent_get(env, eit_p(eit, guest_mole));
    *mole_e = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = (tun->last_attack_mole == mole_i) ? 'M' : 'm',
      .x = ox, .y = oy,
    };

    /* moles take damage from player here  */
    HostEntSwordCollisionOut hesco = {0};
    if (host_ent_sword_collision(env, mole_e, &hesco)) {
      ts_spurt_nearby_hesco(
        env, &hesco,
        map_tile_world_size * 3.0f, SpurtKind_SparkWedge);
      ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
        .kind = SpurtKind_MoleDies,
        .x    = mole_e->x,
        .y    = mole_e->y,
      });

      guest_mole->alive = 0;
    }
  }

  /* "render" (or rather, "unrender") the dead && hidden-in-hole moles */
  for (int i = 0; i < ARR_LEN(tun->moles); i++) {
    Mole *mole = tun->moles + i;
    if (!mole->alive || mole->hole_index == -1)
      host_ent_get(env, eit_p(eit, mole))->kind = EntKind_Limbo;
  }

  return defeated;
}

static void mole_hole_tick(Env *env, MoleHoleState *mh_s) {
  State *s = state(env);

  Map *map = &mh_s->map;
  map_init(map, MapKey_MoleHole);

  EntIdTable *eit = &mh_s->eit;
  eit->pointer_offset = mh_s;
  eit->ent_id_offset = 50;

  uint32_t tick = s->host.tick_count;

  /* lil lost spinny dude */
  *host_ent_get(env, 0) = (HostEnt) {
    .kind = EntKind_Alive, 
    .looks = 'p',
    .sword = 1,
    .x = cosf(env->ts()) * 50,
    .y = sinf(env->ts()) * 50,
  };

  /* tick players */
  host_tick_players(env);
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    HostEnt *pc_e = host_ent_get(env, c->pc);
    if (c->state == ClientState_Play)
      host_ent_tile_collision(env, map, pc_e, NULL);
  }
  ts_hp_pots(env, map, eit, mh_s->hp_pot_collected); // ts_hp_pots(env, ts);

  mole_hole_tunnel_tick(env, mh_s, mh_s->tunnels+0, 'n', 1);
  mole_hole_tunnel_tick(env, mh_s, mh_s->tunnels+1, 'o', 1);
  int beat_boss = 
    mole_hole_tunnel_tick(env, mh_s, mh_s->tunnels+2, 'm', 3);

  {
    int den_i = 0;
    MapIter mi = { .map = map };
    for (; map_iter(&mi, 'w'); den_i++) {
      if (mh_s->charger_den_triggered[den_i]) continue;

      int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
      int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
      float dist = 1e9;
      nearest_player(env, ox, oy, &dist, NULL);
      if (dist < 1.85f*map_tile_world_size)
        for (int i = 0; i < ARR_LEN(mh_s->chargers); i++) {
          MoleCharger *mc = mh_s->chargers + i;
          if (mc->alive) continue;

          mc->alive = 1;
          mc->dir = fmodf(env->ts(), MATH_TAU);
          mc->x = ox;
          mc->y = oy;
          mh_s->charger_den_triggered[den_i] = 1;
          break;
        }
    }
  }

  /* charger logic (if you can call it that */
  for (int i = 0; i < ARR_LEN(mh_s->chargers); i++) {
    MoleCharger *mc = mh_s->chargers + i;
    if (!mc->alive) continue;

    EntId victim_id = -1;
    float victim_dist = 1e9;
    nearest_player(env, mc->x, mc->y, &victim_dist, &victim_id);
    if (victim_id == -1) continue;

    HostEnt *victim = host_ent_get(env, victim_id);

    /* fuzz target slightly to distribute minions on ring around player */
    float ring_size = victim_dist * 0.35f;
    int close = (ring_size < player_world_size * 0.7f);
    if (ring_size < player_world_size) ring_size = player_world_size;

    /* golden ratio should give us evenish ring distribution around target */
    float target_x = victim->x + cosf(MATH_TAU*GOLDEN_RATIO*i) * ring_size;
    float target_y = victim->y + sinf(MATH_TAU*GOLDEN_RATIO*i) * ring_size;

    float ideal_dir = 
      atan2f(target_y - mc->y,
             target_x - mc->x);
    float ideal_dir_nofuzz = 
      atan2f(victim->y - mc->y,
             victim->x - mc->x);

    if (close) {
      mc->shoot_dir = lerp_rads(mc->shoot_dir, ideal_dir_nofuzz, 0.03f);
    } else {
      mc->dir_vel += lerp_rads(mc->dir, ideal_dir, 0.03f) - mc->dir;
      mc->dir_vel *= 0.975f;
      mc->dir += mc->dir_vel;
      mc->shoot_dir = mc->dir;
    }

    mc->alive = 1;
    mc->x += cosf(mc->dir) * (close ? 0.4f : 1.9f);
    mc->y += sinf(mc->dir) * (close ? 0.4f : 1.9f);

    /* charger shooty rock */
    if (tick % (TICK_SECOND) == 0) {
      Rock *rock = rock_recycle(env, mh_s->rocks, ARR_LEN(mh_s->rocks));
      if (rock) {
        float secs = 0.55f;
        float start = player_world_size * 0.35f;
        float dist = start + map_tile_world_size*1.35f*secs;
        rock->start.x = mc->x + cosf(mc->shoot_dir) * start;
        rock->start.y = mc->y + sinf(mc->shoot_dir) * start;
        rock->end.x   = mc->x + cosf(mc->shoot_dir) * dist;
        rock->end.y   = mc->y + sinf(mc->shoot_dir) * dist;
        rock->ts_start = env->ts();
        rock->ts_end   = env->ts() + secs;
      }
    }
  }

  /* "render" rocks */
  for (int i = 0; i < ARR_LEN(mh_s->rocks); i++)
    host_rock_to_ent(env, eit, &mh_s->map, mh_s->rocks + i);

  /* "render" the chargers */
  for (int i = 0; i < ARR_LEN(mh_s->chargers); i++) {
    MoleCharger *mc = mh_s->chargers + i;
    HostEnt *chrg_e = host_ent_get(env, eit_p(eit, mc));
    *chrg_e = (HostEnt) { .kind = EntKind_Limbo };
    if (!mc->alive) continue;

    *chrg_e = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = 'w',
      .x = mc->x, .y = mc->y,
    };

    /* chargers take damage from player here  */
    HostEntSwordCollisionOut hesco = {0};
    if (host_ent_sword_collision(env, chrg_e, &hesco)) {
      ts_spurt_nearby_hesco(
        env, &hesco,
        map_tile_world_size * 3.0f, SpurtKind_SparkWedge);
      ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
        .kind = SpurtKind_MoleChargerDies,
        .x    = chrg_e->x,
        .y    = chrg_e->y,
      });

      mc->alive = 0;
    }
    if (host_ent_tile_collision(env, &mh_s->map, chrg_e, NULL))
      ;
    mc->x = chrg_e->x;
    mc->y = chrg_e->y;
  }

  /* door logic */
  if (beat_boss)
    map_door_tick(env, map, eit, eit_pi(eit, map, 1), MapKey_Ahquicker);
}

static void ts_tick(Env *env, TutorialState *ts) {
  State *s = state(env);

  uint32_t tick = s->host.tick_count;
  double sec = (double)tick / (double)TICK_SECOND;

  int scratch_id = 0;

  /* lil lost spinny dude */
  *host_ent_get(env, scratch_id++) = (HostEnt) {
    .kind = EntKind_Alive, 
    .looks = 'p',
    .sword = 1,
    .x = cosf(env->ts()) * 50,
    .y = sinf(env->ts()) * 50,
  };

  Map *map = &ts->map;
  map_init(map, MapKey_Tutorial);

  EntIdTable *eit = &ts->eit;
  eit->pointer_offset = ts;
  eit->ent_id_offset = 50;

  /* tick players */
  host_tick_players(env);
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    HostEnt *pc_e = host_ent_get(env, c->pc);
    if (c->state == ClientState_Play)
      host_ent_tile_collision(env, map, pc_e, NULL);
  }

  ts_sword(env, ts, eit_p(eit, &ts->sword_collected));
  ts_hp_pots(env, map, eit, ts->hp_pot_collected); // ts_hp_pots(env, ts);

  /* find range of X axis values occupied by shooty thingies */
  int shooter_min_x = 1e9, shooter_max_x = 0;
  int shooter_count = 0, shooter_avg_y = 0;
  MapIter mi = { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    if (mi.tx < shooter_min_x) shooter_min_x = mi.tx;
    if (mi.tx > shooter_max_x) shooter_max_x = mi.tx;
    shooter_avg_y += mi.ty;
    shooter_count++;
  }
  shooter_avg_y /= shooter_count;

  /* "fireballs" (saws in saw frogger) first pass
   * TODO: get off of scratch_id, to EIT */
  int fb_i = 0;
  int fb_id0 = scratch_id;
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    /* origin is center of the tile */
    float ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    float oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;

    /* shoot towards the middle */
    float dx = (map_x_to_world(map,         mi.tx) + map_tile_world_size/2) - ox;
    float dy = (map_y_to_world(map, shooter_avg_y) + map_tile_world_size/2) - oy;
    norm(&dx, &dy);
    dx *= 3.0f * map_tile_world_size;
    dy *= 3.0f * map_tile_world_size;

    /* how far along is bullet on its journey? */
    float difficulty = inv_lerp(shooter_min_x, shooter_max_x, mi.tx);
    double cycle = sec / lerp(1.0f, 0.8f, difficulty);
    float t = fmodf(cycle, 1.0f);

    /* ok now fireball, yeet it into enthood */
    HostEnt *fb = host_ent_get(env, scratch_id++);
    *fb = (HostEnt) {
      .kind = EntKind_Limbo,
      .looks = 'o',
      .x = ox + t*dx,
      .y = oy + t*dy,
    };

    int alive = (int)cycle != ts->dead_on_cycle[fb_i];
    int dead_after = ts->dead_after_cycle[fb_i];
    if (dead_after != 0 && (int)cycle > dead_after) alive = 0;

    if (alive) fb->kind = EntKind_Alive;

    /* also gotta put one for the shooty thingy */
    HostEnt *shooty = host_ent_get(env, scratch_id++);
    *shooty = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = mi.c,
      .x = ox,
      .y = oy,
    };

    /* did you swipe our shooty thingy!? */
    HostEntSwordCollisionOut hesco = {0};
    if (host_ent_sword_collision(env, shooty, &hesco)) {
      ts_spurt_nearby_hesco(
        env, &hesco, map_tile_world_size * 3.0f,
        ts->dead_after_cycle[fb_i]
          ? SpurtKind_ShootyThingyAlreadyDead
          : SpurtKind_SparkWedge
      );
      ts->dead_after_cycle[fb_i] = (int)cycle-1;
    }

    fb_i++;
  }

  fb_i = 0;
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    /* how far along is bullet on its journey? */
    float difficulty = inv_lerp(shooter_min_x, shooter_max_x, mi.tx);
    double cycle = sec / lerp(1.0f, 0.8f, difficulty);

    /* ok now fireball, yeet it into enthood */
    EntId fb_id = fb_id0 + fb_i*2;
    HostEnt *fb = host_ent_get(env, fb_id);

    int alive = (int)cycle != ts->dead_on_cycle[fb_i];
    int dead_after = ts->dead_after_cycle[fb_i];
    if (dead_after != 0 && (int)cycle > dead_after) alive = 0;

    if (alive) {
      /* hit tile = ded */
      if (host_ent_tile_collision(env, map, fb, NULL))
        ts->dead_on_cycle[fb_i] = (int)cycle;

      /* hit player = ouch */
      HostEntEntCollisionState heecs = { .env = env, .collider = fb };
      while (host_ent_ent_collision(&heecs)) {
        HostEnt *hit = host_ent_get(env, heecs.hit_id);

        /* can't hit our own shooter */
        if (heecs.hit_id == (fb_id+1)) continue;

        ts->dead_on_cycle[fb_i] = (int)cycle;

        ts_send_blood(env, heecs.hit_id, 1.15f);
        if (hit->is_player) ts_kill_player(env, heecs.hit_id);
      }
    }

    fb_i++;
  }

  /* saw master */
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 'E')) {
    SawMaster *sm = &ts->saw_master;
    HostEnt *sme = host_ent_get(env, eit_p(eit, sm));
    *sme = (HostEnt) { .kind = EntKind_Alive, .looks = 'M' };

    /* saw master is at tile center */
    float ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    float oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
    sme->x = ox;
    sme->y = oy;

    float t = (sm->state == 0)
      ? 0
      : inv_lerp(sm->ts_state_start, sm->ts_state_end, env->ts());

    /* TODO: animation after death where all minions get sucked in */
    /* [x] bump up the health
     * [x] add more rooms with just minions
     * [x] health potions?
     *
     * [ ] only first potion collectable
     * [ ] minions passive?
     * */

    /* "polish"
     * - particle effects on damage taken/dealt
     * - screen shake durring M mode
     * - red tint on low hp */

    switch (sm->state) {
      case (SawMasterState_Waiting): {
        float best_dist = 1e9;
        nearest_player(env, sme->x, sme->y, &best_dist, NULL);

        if (best_dist < 3*map_tile_world_size)
          sm->state = SawMasterState_WaveStart;
      } break;

      case (SawMasterState_BlinkSucking): {
        sme->kind = ((int)(env->ts() * 1000)/20 % 3 == 0)
          ? EntKind_Limbo
          : EntKind_Alive;

        /* bouncy saws */
        for (int i = 0; i < ARR_LEN(sm->bouncy_saws); i++) {
          BouncySaw *bs = sm->bouncy_saws + i;
          if (bs->state == BouncySawState_Gone) continue;
          bs->state          = BouncySawState_Sucking;
          bs->ts_state_start = sm->ts_state_start;
          bs->ts_state_end   = sm->ts_state_end  ;
        }

        if (t < 1.0) break;
        /* uhhh ... TODO: structify these/this? */
        sm->bouncy_saw_count = 0;
        memset(sm->bouncy_saws, 0, sizeof(sm->bouncy_saws));
        sm->minion_count = 0;
        memset(sm->minions, 0, sizeof(sm->minions));

        sm->saws_launched = 0;
        sm->state = SawMasterState_Blink;
        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + 0.8f*sm->times_killed;
      } break;

      case (SawMasterState_Blink): {
        sme->kind = ((int)(env->ts() * 1000)/20 % 2)
          ? EntKind_Limbo
          : EntKind_Alive;

        if (t < 1.0) break;
        sm->state = (sm->times_killed >= 3)
          ? SawMasterState_Defeated
          : SawMasterState_WaveStart;

        if (sm->state == SawMasterState_Defeated)
          ts_shake_nearby(
            env, sme->x, sme->y,
            map_tile_world_size*5, 0.8f
          );
      } break;

      case (SawMasterState_WaveStart): {

        sm->state = SawMasterState_BouncySaw;
        sm->saws_launched = 0;
        sm->saws_to_launch = (1 + sm->times_killed) * 3;
        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + 0.5f;

        ts_shake_nearby(
          env, sme->x, sme->y,
          map_tile_world_size*5, 3.0f + sm->times_killed
        );
      } break;

      case (SawMasterState_BouncySaw): {
        if (t < 1.0) break;

        BouncySaw *bs = sm->bouncy_saws + sm->bouncy_saw_count;
        float r = (float)sm->saws_launched / (float)sm->saws_to_launch;
        // bs->vel.x = cosf(r * 0.1f + env->ts());
        // bs->vel.y = sinf(r * 0.1f + env->ts());
        bs->vel.x = cosf(r * MATH_TAU + 0.854f);
        bs->vel.y = sinf(r * MATH_TAU + 0.854f);
        bs->pos.x = ox;
        bs->pos.y = oy;

        sm->bouncy_saw_count++;
        sm->saws_launched++;

        if (sm->saws_launched == sm->saws_to_launch) {
          sm->state = SawMasterState_MinionWave;
          sm->ts_state_start = env->ts();
          sm->ts_state_end   = env->ts() + 1.4f;
          break;
        }

        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + 0.08f + 0.7f*!(sm->saws_launched%2);
      } break;

      case (SawMasterState_MinionWave): {
        if (t < 1.0) break;

        sm->minion_count++;

        if (sm->minion_count == (2 + sm->times_killed)) {
          sm->hp = sm->max_hp = 4;
          sm->ts_state_start = env->ts();
          sm->ts_state_end   = env->ts() + 0.5f;
          sm->state = SawMasterState_Damageable;
          break;
        }
        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + 0.3f + 0.07f * sm->minion_count;
      } break;

      case (SawMasterState_Damageable): {
        if (t < 1.0f) break;

        sme->looks = 'E';

        /* ouchy y u hurty saw master */
        HostEntSwordCollisionOut hesco = {0};
        int hit = host_ent_sword_collision(env, sme, &hesco);
        if (hit)
          ts_spurt_nearby_hesco(
            env, &hesco,
            map_tile_world_size * 3.0f, SpurtKind_SparkWedge);
        if (hit && --sm->hp == 0) {
          sm->times_killed++;

          ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
            .kind = SpurtKind_SawMasterDies,
            .x    = sme->x,
            .y    = sme->y,
          });

          sm->ts_state_start = env->ts();
          sm->ts_state_end   = env->ts() + 1.0f;
          sm->state = SawMasterState_BlinkSucking;

          for (int i = 0; i < ARR_LEN(sm->minions); i++) {
            SawMinion *min = sm->minions + i;
            HostEnt *min_e = host_ent_get(env, eit_p(eit, min));
            if (min->state == SawMinionState_Dead) continue;
            min->state          = SawMinionState_Sucking;
            min->ts_state_start = sm->ts_state_start;
            min->ts_state_end   = sm->ts_state_end  ;
            min->pos_state_start.x = min_e->x;
            min->pos_state_start.y = min_e->y;
            min->pos_state_end.x = ox;
            min->pos_state_end.y = oy;
          }
          break;
        }

        /* so it shows up on the screen idk */
        sme->    hp = sm->    hp;
        sme->max_hp = sm->max_hp;
      } break;

      case (SawMasterState_Defeated): {
        sm->bouncy_saw_count = 0;
        sm->minion_count = 0;
        sme->looks = '.';

        HostEnt *door = sme;

        *door = (HostEnt) {
          .kind = EntKind_Alive,
          .looks = 'd',
          .x = ox,
          .y = oy,
        };

        HostEntEntCollisionState heecs = { .env = env, .collider = door };
        while (host_ent_ent_collision(&heecs)) {
          HostEnt *hit = host_ent_get(env, heecs.hit_id);

          if (hit->is_player) {
            hit->x = hit->y = 0;
            /* beat "sawmill" tutorial, now go to ... */
            s->host.map_key = MapKey_MoleHole;
            eit_zero_out_host_ents(eit, env);
            return;
          }
        }
      } break;
    }

    /* no comment (look it works ok) */
    {
      /* bouncy saws */
      for (int i = 0; i < ARR_LEN(sm->bouncy_saws); i++)
        *host_ent_get(env, eit_p(eit, sm->bouncy_saws + i)) =
          (HostEnt) { .kind = EntKind_Limbo };

      /* saw minions */
      for (int i = 0; i < ARR_LEN(sm->minions); i++)
        *host_ent_get(env, eit_p (eit, sm->minions + i)) =
          (HostEnt) { .kind = EntKind_Limbo },
        *host_ent_get(env, eit_pi(eit, sm->minions + i, 1)) =
          (HostEnt) { .kind = EntKind_Limbo };
    }

    /* bouncy saws */
    for (int i = 0; i < sm->bouncy_saw_count; i++)
      ts_bouncy_saw_tick(env, ts, sm->bouncy_saws + i, ox, oy);

    /* saw minions */
    for (int i = 0; i < sm->minion_count; i++) {
      float r = (float)i/(float)sm->minion_count;
      float dist = map_tile_world_size * (0.6f + r * 0.4f);
      ts_saw_minion_tick(sm->minions + i, &(TsSawMinionTickIn) {
        .env = env, .ts = ts, .sm_i = i,
        .init_x = ox + cosf(i * GOLDEN_RATIO + 0.183f) * dist,
        .init_y = oy + sinf(i * GOLDEN_RATIO + 0.183f) * dist 
      });
    }

    ts_saw_minion_tick_collision_pass(env, ts,
                                      ts->saw_minions, sm->minion_count);
  }

  /* saw minions */
  mi = (MapIter) { .map = map };
  int sm_count = 0; 
  for (; map_iter(&mi, 'e'); sm_count++)
    ts_saw_minion_tick(ts->saw_minions + sm_count, &(TsSawMinionTickIn) {
      .env = env, .ts = ts, .sm_i = sm_count,
      .init_x = map_x_to_world(map, mi.tx) + map_tile_world_size/2,
      .init_y = map_y_to_world(map, mi.ty) + map_tile_world_size/2
    });
  ts_saw_minion_tick_collision_pass(env, ts, ts->saw_minions, sm_count);
}

static void ahq_shooty_thing(
  Env *env, AhquickerState *ahq, Map *stuff_map,
  char c, float vx, float vy
) {
  int tick = state(env)->host.tick_count;

  MapIter mi = { .map = stuff_map };
  while (map_iter(&mi, c)) {
    /* shoot from center of tile */
    int ox = map_x_to_world(stuff_map, mi.tx) + map_tile_world_size/2;
    int oy = map_y_to_world(stuff_map, mi.ty) + map_tile_world_size/2;

    uint32_t shoot_every = TICK_SECOND;
    uint32_t tick_offset = (mi.tx + mi.ty) * (TICK_SECOND/3);

    if (((tick + tick_offset) % shoot_every) == 0) {
      Rock *rock = rock_recycle(env, ahq->rocks, ARR_LEN(ahq->rocks));
      if (rock) {
        float secs = 2.0f;
        float start = 0.5*map_tile_world_size;
        float dist  = 5.0*map_tile_world_size;

        rock->start.x = ox + vx*start;
        rock->start.y = oy + vy*start;
        rock->end.x   = ox + vx*(start + dist);
        rock->end.y   = oy + vy*(start + dist);
        rock->ts_start = env->ts();
        rock->ts_end   = env->ts() + secs;
      }
    }
  }
}

static void ahq_shooty_stalks(Env *env, AhquickerState *ahq, Map *stuff_map) {
  int tick = state(env)->host.tick_count;

  int STALK_MAX_HP = 3;

  MapIter mi = { .map = stuff_map };
  for (int stalk_i = 0; map_iter(&mi, 'i'); stalk_i++) {
    AhqStalk *stalk = ahq->stalks + stalk_i;
    HostEnt *stalk_e = host_ent_get(env, eit_p(&ahq->eit, stalk));

    if (stalk->state == AhqStalkState_Dead) {
      *stalk_e = (HostEnt) { .kind = EntKind_Limbo, };
      continue;
    }
    if (stalk->state == AhqStalkState_Init) {
      stalk->hp = STALK_MAX_HP;
      stalk->state = AhqStalkState_Mean;
    }

    /* shoot from center of tile */
    int ox = map_x_to_world(stuff_map, mi.tx) + map_tile_world_size/2;
    int oy = map_y_to_world(stuff_map, mi.ty) + map_tile_world_size/2;

    uint32_t shoot_every = TICK_SECOND*5;
    uint32_t tick_offset = (mi.tx + mi.ty) * (TICK_SECOND/3);

    uint32_t shoot_tick = ((tick + tick_offset) % shoot_every);

    float shoot_t = (float)shoot_tick / (float)shoot_every;
    float hop = 0.0f;
    if (shoot_t > 0.9f)
      hop =        inv_lerp(0.9f, 1.0f, shoot_t);
    if (shoot_t < 0.1f)
      hop = 1.0f - inv_lerp(0.0f, 0.1f, shoot_t);
    oy += ease_out_quad(hop)*player_world_size;

    if (shoot_tick == 0) {
      /* find target */
      EntId victim_id = -1;
      nearest_player(env, ox, oy, NULL, &victim_id);
      if (victim_id == -1) continue;
      HostEnt *pc_e = host_ent_get(env, victim_id);

      float vx = pc_e->x - ox;
      float vy = pc_e->y - oy;
      norm(&vx, &vy);

      Rock *rock = rock_recycle(env, ahq->rocks, ARR_LEN(ahq->rocks));
      if (rock) {
        float secs = 2.0f;
        float mps = 5.0f*map_tile_world_size/2.0f;
        float start = 0.2f*player_world_size;
        float dist  = mps*secs;

        rock->start.x = ox + vx*start;
        rock->start.y = oy + vy*start;
        rock->end.x   = ox + vx*(start + dist);
        rock->end.y   = oy + vy*(start + dist);
        rock->ts_start = env->ts();
        rock->ts_end   = env->ts() + secs;
      }
    }

    /* "render" shooty stalk to a HostEnt */
    *stalk_e = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = 'i',
      .x = ox,
      .y = oy,
      .hp = stalk->hp,
      .max_hp = STALK_MAX_HP
    };

    HostEntSwordCollisionOut hesco = {0};
    int hit = host_ent_sword_collision(env, stalk_e, &hesco);
    if (hit) {
      ts_spurt_nearby_hesco(
        env, &hesco,
        map_tile_world_size * 3.0f, SpurtKind_SparkWedge);

      if (--stalk->hp <= 0) {
        ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
          .kind = SpurtKind_AhqLatcherDies,
          .x    = ox,
          .y    = ox,
        });
        stalk->state = AhqStalkState_Dead;
      }
    }
  }

}

static void ahq_den_test(
  Map *stuff_map, AhqLatcherDen *den, HostEnt *trespasser,
  char trigger_c, char spawn_at_c
) {
  MapIter mi = { .map = stuff_map };
  while (!den->triggered && map_iter(&mi, trigger_c))
    if (mi.tx == map_x_from_world(stuff_map, trespasser->x) &&
        mi.ty == map_y_from_world(stuff_map, trespasser->y)) {
      den->triggered = 1;

      // log(env, "YOU STEPPED ON MY TRAP CARD");
      {
        MapIter mi = { .map = stuff_map };
        for (int al_i = 0; map_iter(&mi, spawn_at_c); al_i++) {
          AhqLatcher *al = den->latchers + al_i;
          al->state = AhqLatcherState_Init;

          /* spawn it in the center of the spot spot tile */
          al->x = map_x_to_world(stuff_map, mi.tx) + map_tile_world_size/2;
          al->y = map_y_to_world(stuff_map, mi.ty) + map_tile_world_size/2;
        }
      }
    }
}

static void ahq_tick(Env *env, AhquickerState *ahq) {
  State *s = state(env);

  Map *map = &ahq->map;
  map_init(map, MapKey_Ahquicker);
  Map stuff_map = {0};
  map_init(&stuff_map, MapKey_Ahquicker_Stuff);

  EntIdTable *eit = &ahq->eit;
  eit->pointer_offset = ahq;
  eit->ent_id_offset = 50;

  uint32_t tick = s->host.tick_count;
  float AHQ_PAD_SIZE = player_world_size*1.2f;

  /* lil lost spinny dude */
  *host_ent_get(env, 0) = (HostEnt) {
    .kind = EntKind_Alive, 
    .looks = 'p',
    .sword = 1,
    .x = cosf(env->ts()) * 50,
    .y = sinf(env->ts()) * 50,
  };

  /* initialize lily pads */
  {
    MapIter mi = { .map = &stuff_map };
    for (int pad_i = 0; map_iter(&mi, 'O'); pad_i++) {
      AhqPad *pad = ahq->pads + pad_i;

      /* TODO: this is probably bad/dumb, i dont trust floating point cmp */
      /* ... it works tho */
      if (pad->x == 0.0f && pad->y == 0.0f) {
        /* lily pad is a bit past top of tile */
        int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
        int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
        oy += map_tile_world_size/2+player_world_size;

        pad->x = ox;
        pad->y = oy;
      }
    }
  }

  /* tick players */
  host_tick_players(env);
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    AhqPadRider *rider = ahq->riders + i;
    HostEnt *pc_e = host_ent_get(env, c->pc);
    if (c->state != ClientState_Play || pc_e == NULL) {
      /* you die, we dismount you */
      rider->state = AhqPadRiderState_Walking;
      continue;
    }

    char *hard = "B";
    float pad_size = AHQ_PAD_SIZE;

    /* find closest pad */
    AhqPad *pad = NULL;
    float dx, dy, pad_dist = 1e9;
    {
      MapIter mi = { .map = &stuff_map };
      for (int pad_i = 0; map_iter(&mi, 'O'); pad_i++) {
        AhqPad *_pad = ahq->pads + pad_i;
        float _dx = _pad->x - pc_e->x;
        float _dy = _pad->y - pc_e->y;
        float _pad_dist = mag(_dx, _dy);

        if (_pad_dist < pad_dist) {
          pad_dist = _pad_dist;
          dx       = _dx      ;
          dy       = _dy      ;
          pad      = _pad     ;
        }
      }
    }
    if (pad == NULL) continue;

    if (rider->state == AhqPadRiderState_Walking) {
      if (pad_dist < pad_size) {
        rider->state = AhqPadRiderState_Mounted;
      } else {
        /* you are not standing on a pad, so you cannot walk on water */
        hard = "B+";
      }
    }
    else if (rider->state == AhqPadRiderState_Mounted) {

      /* propel lily pad when rider swings */
      if (tick < pc_e->swing.tick_end &&
          tick > pc_e->swing.tick_start) {
        pad->vel.x += cosf(pc_e->swing.dir) * 0.3f;
        pad->vel.y += sinf(pc_e->swing.dir) * 0.3f;

        /* velocity cap the pad (no fun allowed) */
        float pad_speed = mag(pad->vel.x, pad->vel.y);
        if (pad_speed < 2.0f) {
          norm(&pad->vel.x, &pad->vel.y);
          pad->vel.x *= 1.75f;
          pad->vel.y *= 1.75f;
        }
      }

      /* you cannot dismount unless you are on land */
      if (pad_dist > pad_size) {
        HostEntTileCollisionIn hetci = {
          .env = env,
          .e = pc_e,
          .map = map,
          .hard = ".",
        };
        if (host_ent_tile_collision_ex(&hetci, NULL))
          rider->state = AhqPadRiderState_Walking;
        else {
          /* STAY ON THE PAD */
          norm(&dx, &dy);
          pc_e->x = pad->x - dx*pad_size;
          pc_e->y = pad->y - dy*pad_size;
        }
      }
    }

    /* basically no matter what you can't walk through walls */
    HostEntTileCollisionIn hetci = {
      .env = env,
      .e = pc_e,
      .map = map,
      .hard = hard,
      .move_e = 1,
    };
    host_ent_tile_collision_ex(&hetci, NULL);

    /* Latcher Dens triggered by player */
    ahq_den_test(&stuff_map, ahq->latcher_dens + 0, pc_e, 'Z', 'z');
    ahq_den_test(&stuff_map, ahq->latcher_dens + 1, pc_e, 'Y', 'y');
  }

  /* latcher update */
  for (int den_i = 0; den_i < ARR_LEN(ahq->latcher_dens); den_i++) {
    AhqLatcherDen *den = ahq->latcher_dens + den_i;

    for (int al_i = 0; al_i < ARR_LEN(den->latchers); al_i++) {
      AhqLatcher *al = den->latchers + al_i;
      EntId al_id = eit_p(eit, al);

      if (al->state == AhqLatcherState_Dormant)
        continue;
      if (al->state == AhqLatcherState_Dead) {
        *host_ent_get(env, al_id) = (HostEnt) { .kind = EntKind_Limbo };
        continue;
      }

      float LATCH_DIST = AHQ_PAD_SIZE * 1.375f;
      int AHQ_LATCHER_MAX_HP = 3;

      float state_t = (al->tick_state_start && al->tick_state_end)
        ? inv_lerp((float)al->tick_state_start,
                   (float)al->tick_state_end  , (float)tick)
        : 0;
      switch (al->state) {
        case (AhqLatcherState_Init): {
          al->hp = AHQ_LATCHER_MAX_HP;

          al->state = AhqLatcherState_FindTarget;
        } break;

        case (AhqLatcherState_FindTarget): {

          /* find nearest player */
          EntId victim_id = -1;
          nearest_player(env, al->x, al->y, NULL, &victim_id);
          if (victim_id == -1) break;
          HostEnt *pc_e = host_ent_get(env, victim_id);

          /* find lily pad underneath this player */
          /* note: must be at least within AHQ_PAD_SIZE */
          float best_pad2player = AHQ_PAD_SIZE;
          AhqPad *best_pad = NULL;
          for (int i = 0; i < ARR_LEN(ahq->pads); i++) {
            AhqPad *pad = ahq->pads + i;

            float pad2player = mag(pad->x - pc_e->x,
                                   pad->y - pc_e->y);
            if (pad2player < best_pad2player)
              best_pad2player = pad2player,
              best_pad = pad;
          }
          if (best_pad == NULL) break;
          EntId pad_e_id = eit_p(eit, best_pad);

          /* are we already in their waffle? */
          for (int i = 0; i < ARR_LEN(best_pad->waffle); i++) {
            EntId id = best_pad->waffle[i];

            if (id == al_id) {
              al->target_id = pad_e_id;
              al->waffle_i = i;
              al->state = AhqLatcherState_Chase;
              break;
            }
          }

          /* find the closest place for us in their waffle,
                (skipping past claimed places, unless claimer died) */
          {
            float best_dist = 1e9;
            int best_i = -1;
            for (int i = 0; i < ARR_LEN(best_pad->waffle); i++) {
              EntId id = best_pad->waffle[i];

              /* we happen to know 0 can't be valid >:) */
              if (!(id == 0 || host_ent_get(env, id)->kind <= EntKind_Limbo))
                continue;

              /* find waffle pos */
              float waffle_len = ARR_LEN(best_pad->waffle);
              float waffle_t = (float)i / waffle_len;
              float target_x = best_pad->x + LATCH_DIST*cosf(waffle_t * MATH_TAU);
              float target_y = best_pad->y + LATCH_DIST*sinf(waffle_t * MATH_TAU);

              float dist = mag(al->x - target_x,
                               al->y - target_y);

              if (dist < best_dist)
                best_dist = dist,
                best_i = i;
            }
            if (best_i > -1) {
              /* claim our place */
              best_pad->waffle[best_i] = al_id;
              al->target_id = pad_e_id;
              al->waffle_i = best_i;
              al->state = AhqLatcherState_Chase;
            }
          }
        }

        case (AhqLatcherState_Chase): {
          HostEnt *victim = host_ent_get(env, al->target_id);
          if (victim->kind <= EntKind_Limbo) {
            al->state = AhqLatcherState_FindTarget;
            break;
          }

          float waffle_len = ARR_LEN(ahq->pads[0].waffle);
          float waffle_t = (float)al->waffle_i / waffle_len;
          float target_x = victim->x + LATCH_DIST*cosf(waffle_t * MATH_TAU);
          float target_y = victim->y + LATCH_DIST*sinf(waffle_t * MATH_TAU);

          float dx = target_x - al->x;
          float dy = target_y - al->y;
          if (mag(dx, dy) < player_world_size*0.1f) {
            al->state = AhqLatcherState_Latched;
            break;
          }
          norm(&dx, &dy);
          al->x += dx*2.15f;
          al->y += dy*2.15f;
        } break;

        case (AhqLatcherState_Knockback): {
          al->x = lerp(al->pos_state_start.x, al->pos_state_end.x, state_t);
          al->y = lerp(al->pos_state_start.y, al->pos_state_end.y, state_t);

          if (state_t < 1.0) break;
          al->tick_state_start = tick;
          al->tick_state_end   = tick + (2*TICK_SECOND)/3;
          al->state = AhqLatcherState_Dazed;
        } break;

        case (AhqLatcherState_Dazed): {
          if (state_t < 1.0) break;
          al->state = AhqLatcherState_Chase;
        } break;

        case (AhqLatcherState_Latched): {
          HostEnt *victim = host_ent_get(env, al->target_id);
          if (victim->kind <= EntKind_Limbo) {
            al->state = AhqLatcherState_FindTarget;
            break;
          }

          float waffle_len = ARR_LEN(ahq->pads[0].waffle);
          float waffle_t = (float)al->waffle_i / waffle_len;
          float t = 0.03f*cosf(((float)tick / (float)TICK_SECOND) * MATH_TAU * 1/3);
          float target_x = victim->x + ((1+t)*LATCH_DIST)*cosf(waffle_t * MATH_TAU);
          float target_y = victim->y + ((1+t)*LATCH_DIST)*sinf(waffle_t * MATH_TAU);

          al->x = target_x;
          al->y = target_y;

          uint32_t ticks_between_bites = (2*TICK_SECOND)/4;

          if ((tick%ticks_between_bites) == 0 &&
              (tick/ticks_between_bites)%((int)waffle_len) == al->waffle_i) {
            al->tick_state_start = tick;
            al->tick_state_end   = tick + TICK_SECOND/4;
            al->state = AhqLatcherState_Bite;
          }
        } break;

        case (AhqLatcherState_Bite): {
          HostEnt *victim = host_ent_get(env, al->target_id);
          if (victim->kind <= EntKind_Limbo) {
            al->state = AhqLatcherState_FindTarget;
            break;
          }

          float waffle_len = ARR_LEN(ahq->pads[0].waffle);
          float waffle_t = (float)al->waffle_i / waffle_len;
          float t = 0.03f*cosf(((float)tick / (float)TICK_SECOND) * MATH_TAU * 1/3);
          float bite_t = 1.0f - fabsf(0.5f - state_t)*2.0f;
          float dist = (1+t)*LATCH_DIST + LATCH_DIST/2*bite_t;
          float target_x = victim->x + dist*cosf(waffle_t * MATH_TAU);
          float target_y = victim->y + dist*sinf(waffle_t * MATH_TAU);

          al->x = target_x;
          al->y = target_y;

          if (state_t < 1.0f) break;
          al->tick_state_start = tick;
          al->tick_state_end   = tick + TICK_SECOND*2;
          al->state = AhqLatcherState_Latched;

          // int tick_dur = al->tick_state_end - al->tick_state_start;
          // if ((al->tick_state_start + tick_dur/2) == tick) {
            Rock *rock = rock_recycle(env, ahq->rocks, ARR_LEN(ahq->rocks));
            if (rock) {
              float secs = 0.5f;
              float mps = 5.0f*map_tile_world_size/2.0f;
              float start = 0.2f*player_world_size;
              float dist  = mps*secs;
              float vx = -cosf(waffle_t * MATH_TAU);
              float vy = -sinf(waffle_t * MATH_TAU);

              rock->start.x = al->x + vx*start;
              rock->start.y = al->y + vy*start;
              rock->end.x   = al->x + vx*(start + dist);
              rock->end.y   = al->y + vy*(start + dist);
              rock->ts_start = env->ts();
              rock->ts_end   = env->ts() + secs;
            }
          // }
        } break;

        default: break;
      }

      HostEnt *al_e = host_ent_get(env, al_id);
      *al_e = (HostEnt) {
        .kind = EntKind_Alive,
        .looks = 'o',
        .x = al->x,
        .y = al->y,
        .max_hp = AHQ_LATCHER_MAX_HP,
        .hp = al->hp
      };

      if (al->state > AhqLatcherState_Init) {
        HostEntTileCollisionIn hetci = {
          .env = env,
          .e = al_e,
          .move_e = 1,
          .map = map,
          /* latchers cant go off water */
          .hard = ".B",
        };
        if (host_ent_tile_collision_ex(&hetci, NULL))
          al->state = AhqLatcherState_Chase;

        HostEntSwordCollisionOut hesco = {0};
        int hit = host_ent_sword_collision(env, al_e, &hesco);
        if (hit) {
          ts_spurt_nearby_hesco(
            env, &hesco,
            map_tile_world_size * 3.0f, SpurtKind_SparkWedge);

          if (--al->hp <= 0) {
            ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
              .kind = SpurtKind_AhqLatcherDies,
              .x    = al->x,
              .y    = al->y,
            });
            al->state = AhqLatcherState_Dead;
          } else {
            /* knockback! */
            float dx = hesco.swing_dir.x;
            float dy = hesco.swing_dir.y;

            double SECS_PER_PIXEL = (0.8f / (map_tile_world_size * 3));
            float action_dist = map_tile_world_size;

            al->state = AhqLatcherState_Knockback;
            al->tick_state_start = tick;
            al->tick_state_end   = tick + TICK_SECOND*(action_dist * SECS_PER_PIXEL);
            al->pos_state_start.x = al->x;
            al->pos_state_start.y = al->y;
            al->pos_state_end.x = al->pos_state_start.x + dx*action_dist;
            al->pos_state_end.y = al->pos_state_start.y + dy*action_dist;
          }
        }
      }
    }
  }

  /* lily pad update */
  MapIter mi = { .map = &stuff_map };
  for (int pad_i = 0; map_iter(&mi, 'O'); pad_i++) {
    AhqPad *pad = ahq->pads + pad_i;
    HostEnt *pad_e = host_ent_get(env, eit_p(eit, pad));
    /* auxiliary boundary indicator ents */
    HostEnt *buoy_ents[5] = {0};
    int BUOY_COUNT = ARR_LEN(buoy_ents);
    for (int n = 0; n < BUOY_COUNT; n++)
      buoy_ents[n] = host_ent_get(env, eit_pi(eit, pad, 1+n));

    /* pad go zoom */
    pad->vel.x *= 0.95f;
    pad->vel.y *= 0.95f;
    pad->x += pad->vel.x;
    pad->y += pad->vel.y;
    /* drag rider - quadratic perf goes weeee */
    int riders = 0;
    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      HostEnt *e = state(env)->host.ents + i;
      if (e->kind <= EntKind_Limbo) continue;
      if (!e->is_player) continue;
      // if (e == pad_e) continue;
      // for (int n = 0; n < BUOY_COUNT; n++)
      //   if (buoy_ents[n] == e)
      //     goto SKIP;

      float dx = pad->x - e->x;
      float dy = pad->y - e->y;
      float pad_dist = mag(dx, dy);
      if (pad_dist < AHQ_PAD_SIZE*1.5f)
        riders++,
        e->x += pad->vel.x*0.8f,
        e->y += pad->vel.y*0.8f;
    }
    if (riders > 0)
      pad->tick_last_ridden = tick;
    if ((tick - pad->tick_last_ridden) > TICK_SECOND) {

      float speed = inv_lerp(
        (float)(TICK_SECOND),
        (float)(TICK_SECOND*5),
        (float)(tick - pad->tick_last_ridden)
      );
      if (speed > 1) speed = 1;

      Map vmap = {0};
      map_init(&vmap, MapKey_Ahquicker_WaterCurrents);
      int tx = map_x_from_world(&vmap, pad->x);
      int ty = map_y_from_world(&vmap, pad->y);
      do {
        if (!map_in_bounds(&vmap, tx, ty)) continue;
        char c = map_index(&vmap, tx, ty);
             if (c == 'v') pad->vel.y -= 0.3f*speed;
        else if (c == '^') pad->vel.y += 0.3f*speed;
        else if (c == '>') pad->vel.x += 0.3f*speed;
        else if (c == '<') pad->vel.x -= 0.3f*speed;
      } while (0);
    }
    // if (pad->state == AhqPadState_Mounted) {
    //   HostEnt *rider_e = host_ent_get(env, pad->rider);
    //   if (rider_e)
    //   else
    //     /* back to empty if rider dead */
    //     pad->state = AhqPadState_Empty;
    // }

    /* "render" pad to Ent(s) */
    *pad_e = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = 'O',
      .x = pad->x,
      .y = pad->y,
    };
    for (int n = 0; n < BUOY_COUNT; n++) {
      float theta = ((float)n/(float)BUOY_COUNT) * MATH_TAU;
      float     r =                                AHQ_PAD_SIZE;
      theta += MATH_TAU/(GOLDEN_RATIO*GOLDEN_RATIO) * pad_i;

      *buoy_ents[n] = (HostEnt) {
        .kind = EntKind_Alive, 
        .looks = '.',
        .x = cosf(theta)*r + pad->x,
        .y = sinf(theta)*r + pad->y,
      };
    }

    /* now that we have Ent, we can use collision detection */
    HostEntTileCollisionOut hetco = {0};
    HostEntTileCollisionIn hetci = {
      .env = env,
      .e = pad_e,
      .move_e = 1,
      .map = map,
      /* pads cant go off water */
      .hard = ".B",
    };
    /* pads bounce off of water with reduced speed */
    if (host_ent_tile_collision_ex(&hetci, &hetco)) {
      float speed = mag(pad->vel.x, pad->vel.y) * 0.8f;
      reflect(&pad->vel.x,
              &pad->vel.y,
                          hetco.normal.x,
                          hetco.normal.y);
      norm(&pad->vel.x,
           &pad->vel.y);
      pad->vel.x *= speed;
      pad->vel.y *= speed;
    }
    /* okay we don't actually want to render that one, its ugly */
    pad_e->looks = EntKind_Limbo;
  }

#if 0
    /* charger shooty rock */
    if (tick % (TICK_SECOND) == 0) {
      Rock *rock = rock_recycle(env, mh_s->rocks, ARR_LEN(mh_s->rocks));
      if (rock) {
        float secs = 0.55f;
        float start = player_world_size * 0.35f;
        float dist = start + map_tile_world_size*1.35f*secs;
        rock->start.x = mc->x + cosf(mc->shoot_dir) * start;
        rock->start.y = mc->y + sinf(mc->shoot_dir) * start;
        rock->end.x   = mc->x + cosf(mc->shoot_dir) * dist;
        rock->end.y   = mc->y + sinf(mc->shoot_dir) * dist;
        rock->ts_start = env->ts();
        rock->ts_end   = env->ts() + secs;
      }
    }
#endif

  ahq_shooty_stalks(env, ahq, &stuff_map);
  ahq_shooty_thing(env, ahq, &stuff_map, 'v',  0.0f, -1.0f);
  ahq_shooty_thing(env, ahq, &stuff_map, '^',  0.0f,  1.0f);
  ahq_shooty_thing(env, ahq, &stuff_map, '>',  1.0f,  0.0f);
  ahq_shooty_thing(env, ahq, &stuff_map, '<', -1.0f,  0.0f);

  ts_hp_pots(env, &stuff_map, eit, ahq->hp_pot_collected);

  /* "render" rocks */
  for (int i = 0; i < ARR_LEN(ahq->rocks); i++)
    host_rock_to_ent(env, eit, &ahq->map, ahq->rocks + i);

  map_door_tick(env, &stuff_map, eit, eit_pi(eit, map, 1), MapKey_Tutorial);
}

static void host_tick(Env *env) {
  State *s = state(env);
  uint32_t tick = s->host.tick_count;

  /* map-specific logic */
  MapKey map_keyb4 = s->host.map_key;
  if (s->host.map_key == MapKey_Tutorial)  ts_tick(       env, &s->host.tutorial );
  if (s->host.map_key == MapKey_MoleHole)  mole_hole_tick(env, &s->host.mole_hole);
  if (s->host.map_key == MapKey_Ahquicker) ahq_tick(      env, &s->host.ahquicker);
  /* broadcasting updates from a map that just changed creates artifacts */
  if (map_keyb4 != s->host.map_key)
    return;

  /* broadcast to ERRYBUDDY */
  ToClntMsg msg = {
    .kind = ToClntMsgKind_EntUpd,
    .ent_upd = { .tick = tick }
  };

  /* send clients in this level all the info for this level */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (c->state == ClientState_Inactive) continue;

    /* also, hey, hi, this is how many health potions you have */
    {
      uint32_t tick = state(env)->host.tick_count;
      int ticks_since_start = tick - c->tick_quaff_earliest;
      int ticks_since_last = tick - c->tick_quaff_last;
      const int QUAFF_TICKS = TICK_SECOND*QUAFF_SECONDS;
      ToClntMsg msg = {
        .kind = ToClntMsgKind_HpPots,
        .hp_pots = {
          .hp_pots = c->hp_pots,
          .quaff_prog = (ticks_since_last <= QUAFF_LAPSE*TICK_SECOND)
            ? ((float)ticks_since_start / (float)QUAFF_TICKS)
            : 0,
        }
      };
      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
    
    /* also hey hi this is what map you are on */
    {
      MapKey map = state(env)->host.map_key;
      ToClntMsg msg = { .kind = ToClntMsgKind_Map, .map = map };
      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }

    for (int i = 0; i < WQ_ENTS_MAX; i++) {
      HostEnt *e = state(env)->host.ents + i;
      if (e->kind == EntKind_Empty) continue;

      msg.ent_upd.id = i;
      msg.ent_upd.ent = (EntUpd) {
        .looks = e->looks, 
        .you = c->pc == i,
        .sword = e->sword,

        .x = e->x,
        .y = e->y,

        .max_hp = e->max_hp,
        .hp = e->hp,
      };

      if (e->kind == EntKind_Limbo)
        msg.ent_upd.ent.looks = 0;

      env->send(&c->addr, (void *)&msg, sizeof(msg));
    }
  }

  s->host.tick_count++;
}

/* broadcast if it's time to */
static void host_poll(Env *env) {
  State *s = state(env);

  double now = env->ts();
  if (s->host.last > 0) {
    /* accumulate delta time */
    double dt = now - s->host.last;
    s->host.dt_acc += dt;

    double FREQ = 1.0 / TICK_SECOND;
    while (s->host.dt_acc > FREQ)
      s->host.dt_acc -= FREQ,
      host_tick(env);
  }
  s->host.last = now;
}

static void host_recv_msg_swing(Env *env, Client *client, ToHostMsg *msg) {
  State *s = state(env);
  uint32_t tick = s->host.tick_count;
  EntId id = client->pc;
  /* struct { EntId id; uint32_t tick; float dir; double secs; } swing; */

  if (s->host.ents[id].swing.tick_end > tick) return;

  s->host.ents[id].swing.dir = msg->swing.dir;
  uint32_t tick_duration = TICK_SECOND * 0.8f;
  s->host.ents[id].swing.tick_start = tick;
  s->host.ents[id].swing.tick_end = tick + tick_duration;

  ToClntMsg out_msg = { .kind = ToClntMsgKind_Swing };
  out_msg.swing.id = client->pc;
  out_msg.swing.tick = tick;
  out_msg.swing.dir = msg->swing.dir;
  out_msg.swing.secs = (double)tick_duration / (double)TICK_SECOND;

  /* tell everyone someone swung! */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (c->state == ClientState_Inactive) continue;

    env->send(&c->addr, (void *)&out_msg, sizeof(out_msg));
  }
}

static void host_recv(Env *env, Addr *addr, uint8_t *buf, int len) {
  Client *client = clients_find(env, addr->hash);

  if (client == NULL) {
    client = clients_storage(env);

    EntId pc = host_ent_next(env);
    *host_ent_get(env, pc) = (HostEnt) {
      .kind = EntKind_Alive,
      .looks = 'p',
      .is_player = 1,
      .max_hp = 5, .hp = 5,
      // .max_hp = 4, .hp = 4, .sword = 1,
      .x = 0,
      .y = 0,
    };
    *client = (Client) {
      .state = ClientState_Play,
      .addr = *addr,
      .pc = pc,
    };
  }

  ToHostMsg *msg = (ToHostMsg *)buf;

  if (msg->kind == ToHostMsgKind_Ping) {
    /* echo! */
    log(env, "(host) echoing back Ping!");
    ToClntMsg msg = { .kind = ToClntMsgKind_Ping };
    env->send(addr, (void *)&msg, sizeof(msg));
  }

  if (msg->kind == ToHostMsgKind_Quaff) {
    /* yum ... :1 */
    HostEnt *pc_e = host_ent_get(env, client->pc);
    uint32_t tick = state(env)->host.tick_count;

    /* so you want to quaff? send a chain of Quaff messages
     * where no two sequential messages are more than QUAFF_LAPSE apart,
     * and the delta-t from the earliest to the latest is >= QUAFF_SECONDS
     *
     * your messages are ignored if they don't meet the following criteria: */

    if (client->state == ClientState_Play &&
        (pc_e && pc_e->hp != pc_e->max_hp) &&
        client->hp_pots > 0
    ) {

      int ticks_since_last = tick - client->tick_quaff_last;
      int ticks_since_start = tick - client->tick_quaff_earliest;
      const int QUAFF_TICKS = TICK_SECOND*QUAFF_SECONDS;

      /* break the chain! can assume this message is new head */
      if (ticks_since_last > QUAFF_LAPSE*TICK_SECOND)
        client->tick_quaff_earliest = client->tick_quaff_last = tick;
      else if (ticks_since_start >= QUAFF_TICKS) {
        /* probably important to reset these idk */
        client->tick_quaff_earliest = client->tick_quaff_last = 0;

        log(env, "*gulps greedily*");
        ts_spurt_nearby(env, map_tile_world_size*3.0f, &(TsSpurt) {
          .kind = SpurtKind_HpPotGulp,
          .x    = pc_e->x,
          .y    = pc_e->y,
        });

        client->hp_pots--;
        pc_e->hp += 3;
        if (pc_e->hp > pc_e->max_hp)
          pc_e->hp = pc_e->max_hp;
      }
      else
        client->tick_quaff_last = tick;
    }
  }

  if (msg->kind == ToHostMsgKind_Move) {
    client->vel.x = msg->move.x;
    client->vel.y = msg->move.y;
    norm(&client->vel.x, &client->vel.y);
  }
  if (msg->kind == ToHostMsgKind_Swing)
    if (client->state == ClientState_Play)
      host_recv_msg_swing(env, client, msg);
}

static void clnt_recv(Env *env, uint8_t *buf, int len) {
  State *s = state(env);

  /* we need to know like, how much bandwidth we realistically have?
   * is it going to be a problem?
   * and also like should we be stuffing more msgs into one datagram?
   * and actually like using the union size so no trailing zeroes? */

  /* gotta be connected to the host to receive a message, so */
  s->clnt.am_connected = 1;

  ToClntMsg *msg = (ToClntMsg *)buf;
#if 0
  switch (msg->kind) {
  case ToClntMsgKind_Ping:      log(env, "ToClntMsgKind_Ping"); break;
  case ToClntMsgKind_Map:       log(env, "ToClntMsgKind_Map"); break;
  case ToClntMsgKind_EntUpd:    log(env, "ToClntMsgKind_EntUpd"); break;
  case ToClntMsgKind_HpPots:    log(env, "ToClntMsgKind_HpPots"); break;
  case ToClntMsgKind_Swing:     log(env, "ToClntMsgKind_Swing"); break;
  case ToClntMsgKind_BloodSecs: log(env, "ToClntMsgKind_BloodSecs"); break;
  case ToClntMsgKind_ShakeSecs: log(env, "ToClntMsgKind_ShakeSecs"); break;
  case ToClntMsgKind_Spurt:     log(env, "ToClntMsgKind_Spurt"); break;
  }
#endif

  if (msg->kind == ToClntMsgKind_Ping)
    log(env, "got host Ping!");
  if (msg->kind == ToClntMsgKind_Map) {
    State *s = state(env);
    if (msg->map != s->clnt.map_key) {
      s->clnt.map_key = msg->map;
      memset(s->clnt.ents, 0, sizeof(s->clnt.ents));
    }
    map_init(&s->clnt.map, msg->map);

    // ToHostMsg msg = { .kind = ToHostMsgKind_Ack };
    // env->send_to_host((void *)&msg, sizeof(msg));
  }
  if (msg->kind == ToClntMsgKind_EntUpd) {
    uint32_t tick = msg->ent_upd.tick;

    if (tick >= s->clnt.last_known_tick) {
      s->clnt.last_known_tick = tick;
      /* this is probably fine */
      s->clnt.ents[msg->ent_upd.id].you    = msg->ent_upd.ent.you;
      s->clnt.ents[msg->ent_upd.id].sword  = msg->ent_upd.ent.sword;
      s->clnt.ents[msg->ent_upd.id].x      = msg->ent_upd.ent.x;
      s->clnt.ents[msg->ent_upd.id].y      = msg->ent_upd.ent.y;
      s->clnt.ents[msg->ent_upd.id].looks  = msg->ent_upd.ent.looks;
      s->clnt.ents[msg->ent_upd.id].max_hp = msg->ent_upd.ent.max_hp;
      s->clnt.ents[msg->ent_upd.id].hp     = msg->ent_upd.ent.hp;
    }
  }
  if (msg->kind == ToClntMsgKind_HpPots) {
    s->clnt.hp_pots    = msg->hp_pots.hp_pots;

    if (msg->hp_pots.quaff_prog <= 0) {
      if (s->clnt.quaff_zero_since <= 0.0f)
        s->clnt.quaff_zero_since = env->ts();
    } else
      s->clnt.quaff_zero_since = 0;
    s->clnt.quaff_prog = msg->hp_pots.quaff_prog;
  }
  if (msg->kind == ToClntMsgKind_Swing) {
    /* prolly shoulda put these in band with the other stuffs but, ah.
     * ... guess time conversions prolly problematic? */

    /* TODO: what happens if these are dropped? */
    s->clnt.ents[msg->swing.id].swing.ts_start = env->ts();
    s->clnt.ents[msg->swing.id].swing.ts_end = env->ts() + msg->swing.secs;
    s->clnt.ents[msg->swing.id].swing.dir = msg->swing.dir;
  }
  if (msg->kind == ToClntMsgKind_BloodSecs) {
    s->clnt.blood_secs += msg->add_blood_secs;
  }
  if (msg->kind == ToClntMsgKind_ShakeSecs) {
    s->clnt.shake_secs += msg->add_shake_secs;
  }
  if (msg->kind == ToClntMsgKind_Spurt) {
    for (int i = 0; i < ARR_LEN(s->clnt.spurts); i++) {
      Spurt *spurt = s->clnt.spurts + i;
      if (spurt->ts_end < env->ts()) {
        *spurt = (Spurt) {
          .ts_start = env->ts(),
          .ts_end   = env->ts() + spurt_kind_descs[msg->spurt.kind].duration,
          .kind  = msg->spurt.kind,
          .x     = msg->spurt.x,
          .y     = msg->spurt.y,
          .angle = msg->spurt.angle,
        };
        break;
      }
    }
  }
}

EXPORT void wq_update(Env *env) {
  State *s = state(env);

  /* better get yourself connected */
  if (!s->clnt.am_connected) {
    log(env, "making connection attempt");
    // printf("made %d connection attempts, sending \"hi\"\n", s->clnt.tries);
    ToHostMsg msg = { .kind = ToHostMsgKind_Ping };
    env->send_to_host((void *)&msg, sizeof(msg));
    s->clnt.tries++;

    if (!s->am_host && s->clnt.tries >= 10) {
      /* Tried 10 times to reach server ...
       * still no luck ... so I'll serve myself! */
      s->am_host = 1;
      log(env, "gonna try to host ...");
    }
  }

  /* even if no keystate has changed, recompute heading and update server */
  if (s->clnt.am_connected) {
    ToHostMsg msg = {
      .kind = ToHostMsgKind_Move,
    };

    if (s->clnt.keysdown[WqVk_W]) msg.move.y += 1;
    if (s->clnt.keysdown[WqVk_S]) msg.move.y -= 1;
    if (s->clnt.keysdown[WqVk_A]) msg.move.x -= 1;
    if (s->clnt.keysdown[WqVk_D]) msg.move.x += 1;

    env->send_to_host((void *)&msg, sizeof(msg));

    if (s->clnt.keysdown[WqVk_Space]) {
      ToHostMsg msg = { .kind = ToHostMsgKind_Quaff };
      env->send_to_host((void *)&msg, sizeof(msg));
    }
  }

  /* server gotta do server stuff */
  if (s->am_host) host_poll(env);

  /* keep polling until both recvs return 0 */
  int more = 1;
  while (more) {
    uint8_t buf[1 << 8] = {0};
    int len = sizeof(buf);
    Addr addr = {0};

    more = 0;

    len = sizeof(buf);
    if (s->am_host && env->host_recv(&addr, buf, &len))
      host_recv(env, &addr, buf, len), more = 1;

    len = sizeof(buf);
    if (env->clnt_recv(buf, &len))
      clnt_recv(env, buf, len), more = 1;
  }
}
/* --- net --- */

/* --- input --- */
EXPORT void wq_mousebtn(Env *env, int down) {
  State *s = state(env);

  ClntEnt *you = clnt_ent_you(env);
  if (!you) return;
  if (!down) return;

  float screen_player_x = you->x - s->clnt.cam.x;
  float screen_player_y = you->y - s->clnt.cam.y;

  ToHostMsg msg = { .kind = ToHostMsgKind_Swing };
  msg.swing.dir = atan2f(s->clnt.cursor.y - screen_player_y,
                         s->clnt.cursor.x - screen_player_x);
  env->send_to_host((void *)&msg, sizeof(msg));
}

EXPORT void wq_keyboard(Env *env, char c, int down) {
  State *s = state(env);

#ifndef __wasm__
  if (c == WqVk_Esc  ) {
    log(env, "restarting everything");

    free(env->stash.buf);
    env->stash.buf = NULL;
    state(env);
  }
#endif

  // char str[256] = {0};
  // sprintf(str, "you pressed %d", c);
  // log(env, str);
  if (c == WqVk_T) s->log_open = down;

  if (c == WqVk_Tilde && down) {
    log(env, "recompiling ...");
    char buf[1024*2*2*2] = {0};
    int len = sizeof(buf);
    env->dbg_sys_run("cl /nologo /Zi /O2 /WX /LD ../wq/wq.c /link /out:wq.dll", buf, &len);

    // env->dbg_sys_run(
    //   "tcc -shared -DCUSTOM_MATH=1 -o wq.dll ../wq/wq.c",
    //   buf, &len
    // );

    buf[sizeof(buf)-1] = 0; /* just in case */
    log(env, buf);
    log(env, "recompilation done!");

    env->dbg_dylib_reload();
  }


  s->clnt.keysdown[(int)c] = down;

}
/* --- input --- */


