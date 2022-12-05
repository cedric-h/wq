// vim: sw=2 ts=2 expandtab smartindent
#include <stdio.h>

#define t_begin(str) env->trace_begin((str), sizeof(str))
#define t_end() env->trace_end()

#include "wq.h"
#include "font.h"

/* TODO: worry about this eventually
 * https://stackoverflow.com/questions/2782725/converting-float-values-from-big-endian-to-little-endian/2782742#2782742
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

#define MATH_PI  3.141592653589793
#define MATH_TAU 6.283185307179586

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

typedef struct {
  struct { float x, y; } grip, tip, circ;
  float radius;
} LineHitsCircle;
static int line_hits_circle(const LineHitsCircle *lhc) {
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

  return closest <= lhc->radius;
}


/* forward decls */
static void log(Env *e, char *p);


/* --- map --- */
#define map_tile_world_size (90)
#define player_world_size (8*4)
typedef struct {
  struct { float x, y; } world_offset;
  struct {   int x, y; } tile_size;
  int str_rows, str_cols;
} Map;

char map_str[] = 
  "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
  "B....B.BvB.BvB.BvB...vB....B\n"
  "B..................!h....e..\n"
  "B....B^B.B^B.B^B.B...^B....B\n"
  "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n";

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
  { return map_str[my*map->str_cols + mx]; }
static inline int map_in_bounds(Map *map, int mx, int my) {
  if (mx < 0 || mx >= map->tile_size.x) return 0;
  if (my < 0 || my >= map->tile_size.y) return 0;
  return 1;
}

typedef struct { Map *map; int tx, ty, i; char c; } MapIter;
static int map_iter(MapIter *mi, char filter) {
  while (map_str[mi->i]) {
    mi->tx = mi->i % mi->map->str_cols;
    mi->ty = mi->i / mi->map->str_cols;
    mi->c = map_str[mi->i];
    mi->i++;
    if (mi->c == '\n') continue;
    if (!filter || filter == mi->c) return 1;
  }

  return 0;
}

static void map_init(Map *map, char *str, int str_size) {
  int cols = 0;
  while (str[cols] && str[cols] != '\n') cols++;
  map->str_cols = cols + 1; /* newline */

  map->str_rows = str_size / map->str_cols;

  map->tile_size.x = map->str_cols - 1; /* newline isn't a tile */
  map->tile_size.y = map->str_rows    ;

  MapIter mi = { .map = map };
  if (map_iter(&mi, 'h')) {
    int ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    int oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;
    map->world_offset.x = ox;
    map->world_offset.y = oy;
  }
}
/* --- map --- */


/* --- persistent State layout --- */
typedef uint32_t EntId;
typedef struct {
  int active;
  Addr addr;

  struct { float x, y; } vel;

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

typedef struct {
  char looks;
  float x, y;
  uint8_t you, sword;
  uint16_t hp, max_hp;

  struct { double ts_start, ts_end; float dir; } swing;
} ClntEnt;

typedef struct {
  char looks;
  float x, y;
  uint8_t you, sword;
  uint16_t hp, max_hp;
} EntUpd;

typedef enum {
  SawMinionState_Init,
  SawMinionState_Think,
  SawMinionState_Charge,
  SawMinionState_Attack,
  SawMinionState_Dead,
} SawMinionState;
typedef struct {
  SawMinionState state;
  double ts_state_start, ts_state_end;
  struct { float x, y; } pos_state_start,
                         pos_state_end; /* <- meaning depends on state */
} SawMinion;

typedef struct {
  uint32_t dead_on_cycle[10];
  uint32_t dead_after_cycle[10];
  uint8_t sword_collected;
  SawMinion saw_minion;
} TutorialState;

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

  Map map;

  struct {
    uint32_t last_known_tick;

    int am_connected, tries;
    char keysdown[WqVk_MAX];
    ClntEnt ents[WQ_ENTS_MAX];
    struct { float x, y; } cam;
    struct { float x, y; } cursor;
  } clnt;

  int am_host;
  struct {
    uint32_t tick_count;

    double last, dt_acc;
    Client clients[CLIENTS_MAX];

    HostEnt ents[WQ_ENTS_MAX];
    int next_ent;

    TutorialState tutorial;
  } host;

} State;

/* can't use static because DLL constantly reloading */
static State *state(Env *env) {
  if (env->stash.buf == NULL || env->stash.len < sizeof(State)) {
    if (env->stash.buf) free(env->stash.buf);
    env->stash.buf = calloc(sizeof(State), 1);
    env->stash.len = sizeof(State);

    /* --- init state --- */
    for (int i = 0; i < 256; i++) state(env)->frametime_ring_buffer[i] = 1/60;

    state(env)->host.next_ent += 100; /* i need some scratch ents for my mad hax */
    /* lol tick 0 is special because lazy */
    state(env)->host.tick_count = 1;

    map_init(&state(env)->map, map_str, sizeof(map_str));

    log(env, "sup nerd");
    /* --- init state --- */
  }
  return (State *)env->stash.buf;
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
  return (r << 0) & 0x000000FF |
         (g << 4) & 0x0000FF00 |
         (b << 8) & 0x00FF0000 ;
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
  for (; *str; *str++) {

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
    -sinf(r)*2.0f, cosf(r)*2.0f
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

    /* checkerboard */
    for (int y = 0; y < _rcx->size.y; y++)
      for (int x = 0; x < _rcx->size.x; x++)
        rcx_p(x, y, (y/4%2 == x/4%2) ? 0x22222222 : 0x33333333);

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
        nclient += clients[i].active;
    }

    char c = '1' + (nclient-1);
    rcx_str(0, 2*28, &c);
  }
#endif

  /* --- world space? --- */

  /* a quest of self-discovery */
  ClntEnt *you = clnt_ent_you(env);

  rcx_char(state(env)->clnt.cursor.x - 5*8/2,
           state(env)->clnt.cursor.y - 5*8/2, 5, 'x');

  {
    t_begin("ents");

    State *s = state(env);

    /* move cam to you */
    if (you) {
      s->clnt.cam.x = lerp(s->clnt.cam.x, you->x - _rcx->size.x/2, 0.08f);
      s->clnt.cam.y = lerp(s->clnt.cam.y, you->y - _rcx->size.y/2, 0.08f);
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

                  rcx_p(dx + px + i, y + 0, 0xFF00FF00);
          if (on) rcx_p(dx + px + i, y + 1, 0xFF00FF00);
                  rcx_p(dx + px + i, y + 2, 0xFF00FF00);
        }
      }
    }

    t_end();
  }

  /* draw map */
  {
    t_begin("map");
    State *s = state(env);


    int min_x = s->clnt.cam.x;
    int max_x = s->clnt.cam.x + _rcx->size.x;
    int min_y = s->clnt.cam.y;
    int max_y = s->clnt.cam.y + _rcx->size.y;
    for (int y = min_y; y < max_y; y++)
      for (int x = min_x; x < max_x; x++) {
        int tx = map_x_from_world(&s->map, x);
        int ty = map_y_from_world(&s->map, y);

        if (!map_in_bounds(&s->map, tx, ty)) continue;
        if (map_index(&s->map, tx, ty) != 'B') continue;

        rcx_p(x - min_x, y - min_y, 0xAA4444AA);
      }

    t_end();
  }

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

    char buf[16] = {0};
    snprintf(buf, 16, "%.1fms\n%dx%d", 1000*average, _rcx->size.x, _rcx->size.y);

    int scale = 8*(_rcx->cfg.text_size = 1);
    rcx_str(_rcx->size.x - scale*10,
            _rcx->size.y - scale*2, buf);

    t_end();
  }

  t_end();
}
/* --- rcx --- */


/* --- net --- */

static Client *clients_find(Env *env, uint32_t addr_hash) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (clients[i].active && clients[i].addr.hash == addr_hash)
      return clients + i;
  return NULL;
}

static Client *clients_storage(Env *env) {
  Client *clients = state(env)->host.clients;

  for (int i = 0; i < CLIENTS_MAX; i++)
    if (!clients[i].active)
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

/* to client */
typedef enum {
  ToClntMsgKind_Ping,
  ToClntMsgKind_EntUpd,
  ToClntMsgKind_Swing,
} ToClntMsgKind;

typedef struct {
  ToClntMsgKind kind;
  union {
    /* _EntUpd */ struct { EntId id; uint32_t tick; EntUpd ent; } ent_upd;
    /* _Swing  */ struct { EntId id; uint32_t tick; float dir; double secs; } swing;
  };
} ToClntMsg;

/* to host */
typedef enum {
  ToHostMsgKind_Ping,
  ToHostMsgKind_Move,
  ToHostMsgKind_Swing,
} ToHostMsgKind;

typedef struct {
  ToHostMsgKind kind;
  union {
    /* ToHostMsgKind_Move  */ struct { float x, y; } move;
    /* ToHostMsgKind_Swing */ struct { float dir; } swing;
  };
} ToHostMsg;

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

static int host_ent_tile_collision(Env *env, Map *map, HostEnt *e) {
  float ex = e->x;
  float ey = e->y;

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
  float best_dist = 1e9;
  for (int i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {

    /* are you in a solid tile? */
    int tx = map_x_from_world(map, ex + offsets[i].x * map_tile_world_size);
    int ty = map_y_from_world(map, ey + offsets[i].y * map_tile_world_size);
    if (!map_in_bounds(map, tx, ty)) continue;
    if (map_index(map, tx, ty) == 'B') continue;

    /* find center of tile */
    int cx = map_x_to_world(map, tx) + map_tile_world_size/2;
    int cy = map_y_to_world(map, ty) + map_tile_world_size/2;

    /* from center to Ent */
    float dx = ex - cx;
    float dy = ey - cy;

    float dist = mag(dx, dy);

    if (dist < best_dist) {
      best_dist = dist;
      empty_tx = tx;
      empty_ty = ty;
    }
  }

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
    float he =  map_tile_world_size/2;
    if (fabsf(dx) > he) hit = 1, e->x = cx + he * signf(dx);
    if (fabsf(dy) > he) hit = 1, e->y = cy + he * signf(dy);
    return hit;
  }
  return 0;
}

static int host_ent_sword_collision(Env *env, HostEnt *p) {
  uint32_t tick = state(env)->host.tick_count;

  /* quadratic perf goes weee */
  for (int i = 0; i < WQ_ENTS_MAX; i++) {
    HostEnt *e = state(env)->host.ents + i;
    if (e->kind <= EntKind_Limbo) continue;
    if (e == p) continue;
    if (!e->sword) continue;
    if (e->swing.tick_end < tick) continue;

    SwordAnim sa = {
      .in.time = inv_lerp((float)e->swing.tick_start, (float)e->swing.tick_end, (float)tick),
      .in.x = e->x,
      .in.y = e->y,
      .in.dir = e->swing.dir,
    };
    sword_anim(&sa);
    // if (!sa.out.dmg) continue;

    LineHitsCircle lhc = {
      .tip .x = sa.out.x + cosf(sa.out.rot)*65,
      .tip .y = sa.out.y + sinf(sa.out.rot)*65,

      .grip.x = sa.out.x,
      .grip.y = sa.out.y,

      .circ.x = p->x,
      .circ.y = p->y,
      .radius = 5*8/2,
    };

    if (line_hits_circle(&lhc))
      return 1;
  }
  return 0;
}

/* how many ticks in a second? */
#define TICK_SECOND (20)

static void ts_sword(Env *env, TutorialState *ts, EntId sword_id) {
  HostEnt *sword = host_ent_get(env, sword_id);
  *sword = (HostEnt) {0};

  Map *map = &state(env)->map;
  MapIter mi = { .map = map };
  if (map_iter(&mi, '!')) {
    /* pos is center of the tile */
    *sword = (HostEnt) {
      sword->kind = EntKind_Limbo,
      sword->looks = '!',
      sword->x = map_x_to_world(map, mi.tx) + map_tile_world_size/2,
      sword->y = map_y_to_world(map, mi.ty) + map_tile_world_size/2,
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

static void host_tick(Env *env) {
  State *s = state(env);
  Map *map = &s->map;

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

  /* move players */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;

    /* apply Client velocity */
    host_ent_get(env, c->pc)->x += c->vel.x * 6.0f;
    host_ent_get(env, c->pc)->y += c->vel.y * 6.0f;
    host_ent_tile_collision(env, map, host_ent_get(env, c->pc));
  }

  /* find range of X axis values occupied by shooty thingies */
  int shooter_min_x = 1e9, shooter_max_x = 0;
  MapIter mi = { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    if (mi.tx < shooter_min_x) shooter_min_x = mi.tx;
    if (mi.tx > shooter_max_x) shooter_max_x = mi.tx;
  }

  /* tutorial level logic */
  TutorialState *ts = &s->host.tutorial;

  ts_sword(env, ts, scratch_id++);

  int fb_i = 0;
  int fb_id0 = scratch_id;
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    /* origin is center of the tile */
    float ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
    float oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;

    /* shoot towards row 2 on Y axis */
    float dx = (map_x_to_world(map, mi.tx) + map_tile_world_size/2) - ox;
    float dy = (map_y_to_world(map,     2) + map_tile_world_size/2) - oy;
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
    if (host_ent_sword_collision(env, shooty))
      ts->dead_after_cycle[fb_i] = (int)cycle-1;

    fb_i++;
  }

  fb_i = 0;
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    /* how far along is bullet on its journey? */
    float difficulty = inv_lerp(shooter_min_x, shooter_max_x, mi.tx);
    double cycle = sec / lerp(1.0f, 0.8f, difficulty);
    float t = fmodf(cycle, 1.0f);

    /* ok now fireball, yeet it into enthood */
    EntId fb_id = fb_id0 + fb_i*2;
    HostEnt *fb = host_ent_get(env, fb_id);

    int alive = (int)cycle != ts->dead_on_cycle[fb_i];
    int dead_after = ts->dead_after_cycle[fb_i];
    if (dead_after != 0 && (int)cycle > dead_after) alive = 0;

    if (alive) {
      /* hit tile = ded */
      if (host_ent_tile_collision(env, map, fb))
        ts->dead_on_cycle[fb_i] = (int)cycle;

      /* hit player = ouch */
      HostEntEntCollisionState heecs = { .env = env, .collider = fb };
      while (host_ent_ent_collision(&heecs)) {
        HostEnt *hit = host_ent_get(env, heecs.hit_id);

        /* can't hit our own shooter */
        if (heecs.hit_id == (fb_id+1)) continue;

        ts->dead_on_cycle[fb_i] = (int)cycle;

        if (hit->is_player) hit->x = hit->y = 0.0f;
      }
    }

    fb_i++;
  }

  /* saw minions */
  mi = (MapIter) { .map = map };
  while (map_iter(&mi, 'e')) {
    HostEnt *sme = host_ent_get(env, scratch_id++);
    *sme = (HostEnt) { .kind = EntKind_Alive, .looks = 'e' };
    SawMinion *sm = &ts->saw_minion;

    HostEnt *fbe = host_ent_get(env, scratch_id++);
    *fbe = (HostEnt) { .kind = EntKind_Limbo, .looks = 'o' };

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
        float ox = map_x_to_world(map, mi.tx) + map_tile_world_size/2;
        float oy = map_y_to_world(map, mi.ty) + map_tile_world_size/2;

        sme->x = sm->pos_state_start.x = ox;
        sme->y = sm->pos_state_start.y = oy;

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
        HostEnt *best_target = NULL;
        for (int i = 0; i < WQ_ENTS_MAX; i++) {
          HostEnt *e = state(env)->host.ents + i;
          if (e->kind <= EntKind_Limbo) continue;
          if (!e->is_player) continue;

          float dist = mag(e->x - sme->x,
                           e->y - sme->y);
          if (dist < best_dist)
            best_dist = dist,
            best_target = e;
        }
        if (best_target == NULL) break;

        float dx = best_target->x - sme->x;
        float dy = best_target->y - sme->y;
        norm(&dx, &dy);

        /* move/shoot dist */
        float action_dist = map_tile_world_size;

        float adt = inv_lerp(ATTACK_DIST_MIN, ATTACK_DIST_MAX, best_dist);
        /* great, we're the proper distance away to attack */
        if (adt >= 0 && adt <= 1) sm->state = SawMinionState_Attack;
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
        if (action_dist > map_tile_world_size) action_dist = map_tile_world_size;

        double SECS_PER_PIXEL = WAIT / map_tile_world_size;

        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + (double)action_dist * SECS_PER_PIXEL;
        sm->pos_state_end.x = sm->pos_state_start.x + dx*action_dist;
        sm->pos_state_end.y = sm->pos_state_start.y + dy*action_dist;
      } break;

      case (SawMinionState_Attack): {
        sme->x = sm->pos_state_start.x;
        sme->y = sm->pos_state_start.y;

        fbe->x = lerp(sm->pos_state_start.x, sm->pos_state_end.x, t);
        fbe->y = lerp(sm->pos_state_start.y, sm->pos_state_end.y, t);
        fbe->kind = EntKind_Alive;

        HostEntEntCollisionState heecs = { .env = env, .collider = fbe };
        while (host_ent_ent_collision(&heecs)) {
          HostEnt *hit = host_ent_get(env, heecs.hit_id);
          if (hit->is_player) {
            hit->x = hit->y = 0.0f;
            sm->state = SawMinionState_Think;
          }
        }

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

    }

    /* did you swipe our saw minion!? */
    if (host_ent_sword_collision(env, sme))
      sm->state = SawMinionState_Dead;
  }

  /* broadcast to ERRYBUDDY */
  ToClntMsg msg = {
    .kind = ToClntMsgKind_EntUpd,
    .ent_upd = { .tick = tick }
  };

  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;
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
      };

      if (c->pc == i)
        msg.ent_upd.ent.max_hp = 2,
        msg.ent_upd.ent.hp = 1;

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

  /* broadcast! */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;
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
      .x = 0,
      .y = 0,
    };
    *client = (Client) {
      .active = 1,
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

  if (msg->kind == ToHostMsgKind_Move) {
    client->vel.x = msg->move.x;
    client->vel.y = msg->move.y;
    norm(&client->vel.x, &client->vel.y);
  }
  if (msg->kind == ToHostMsgKind_Swing)
    host_recv_msg_swing(env, client, msg);
}

static void clnt_recv(Env *env, uint8_t *buf, int len) {
  State *s = state(env);

  /* gotta be connected to the host to receive a message, so */
  s->clnt.am_connected = 1;

  ToClntMsg *msg = (ToClntMsg *)buf;
  if (msg->kind == ToClntMsgKind_Ping)
    log(env, "got host Ping!");
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
  if (msg->kind == ToClntMsgKind_Swing) {
    s->clnt.ents[msg->swing.id].swing.ts_start = env->ts();
    s->clnt.ents[msg->swing.id].swing.ts_end = env->ts() + msg->swing.secs;
    s->clnt.ents[msg->swing.id].swing.dir = msg->swing.dir;
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

  /* keystate has changed, recompute heading and update server */
  if (s->clnt.am_connected) {
    ToHostMsg msg = {
      .kind = ToHostMsgKind_Move,
    };

    if (s->clnt.keysdown[WqVk_W]) msg.move.y += 1;
    if (s->clnt.keysdown[WqVk_S]) msg.move.y -= 1;
    if (s->clnt.keysdown[WqVk_A]) msg.move.x -= 1;
    if (s->clnt.keysdown[WqVk_D]) msg.move.x += 1;

    env->send_to_host((void *)&msg, sizeof(msg));
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

  if (c == WqVk_Esc  ) {
    log(env, "restarting everything");

    free(env->stash.buf);
    env->stash.buf = NULL;
    state(env);
  }

  // char str[256] = {0};
  // sprintf(str, "%d", c);
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


  s->clnt.keysdown[c] = down;

}
/* --- input --- */


