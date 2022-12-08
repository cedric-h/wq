// vim: sw=2 ts=2 expandtab smartindent
#include <stdio.h>

#define t_begin(str) env->trace_begin((str), sizeof(str))
#define t_end() env->trace_end()
#define ARR_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

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

// char map_str[] = 
//   "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
//   "B....B.BvB.BvB.BvB...vBeeeeB\n"
//   "B..................!h....eee\n"
//   "B....B^B.B^B.B^B.B...^B.eeeB\n"
//   "BBBBBBBBBBBBBBBBBBBBBBBBBBBB\n";
char map_str[] = 
  "BBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n"
  "B....B.BvB.BvB.BvB...vB.....B\n"
  "B.....................h!..E.B\n"
  "B....B^B.B^B.B^B.B...^B.....B\n"
  "BBBBBBBBBBBBBBBBBBBBBBBBBBBBB\n";

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
  size_t table[100];
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
  return -9999;
}
static EntId eit_pi(EntIdTable *eit, void *p, int i) { return eit_p(eit, (uint8_t *)p + i); }

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

/* - sawmill - */
typedef enum {
  SawMinionState_Init,
  SawMinionState_Think,
  SawMinionState_Charge,
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
} BouncySawState;
typedef struct {
  BouncySawState state;
  uint32_t tick_no_hurt_till;
  struct { float x, y; } pos, vel;
} BouncySaw; 
typedef enum {
  SawMasterState_Waiting,
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

  uint32_t dead_on_cycle[10];
  uint32_t dead_after_cycle[10];
  SawMinion saw_minions[10];
  SawMaster saw_master;
} TutorialState;
/* - sawmill - */

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

  Map map;

  struct {
    uint32_t last_known_tick;

    int am_connected, tries;
    char keysdown[WqVk_MAX];
    ClntEnt ents[WQ_ENTS_MAX];
    struct { float x, y; } cam;
    struct { float x, y; } cursor;
#ifdef VISUALIZE_SWING
    struct { float x, y; } tip, grip;
#endif
  } clnt;

  int am_host;
  struct {
    uint32_t tick_count;

    double last, dt_acc;
    Client clients[CLIENTS_MAX];

    HostEnt ents[WQ_ENTS_MAX];
    int next_ent;

    TutorialState tutorial;

    HitTableEntry hit_table[100];
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

    state(env)->host.next_ent += 150; /* i need some scratch ents for my mad hax */
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
static EntId host_ent_id(Env *env, HostEnt *e) {
  return e - state(env)->host.ents;
}

/* how many ticks in a second? */
#define TICK_SECOND (20)

/* basically, debounces hits.
 * returns 0 if already present, inserts and returns 1 otherwise */
#define HIT_TABLE_ENTRY_DURATION_TICKS (TICK_SECOND * 0.1)
static int host_hit_table_debounce(Env *env, EntId dealt_by, EntId dealt_to) {
  State *s = state(env);
  uint32_t tick = s->host.tick_count;

  for (int i = 0; i < ARR_LEN(s->host.hit_table); i++) {
    HitTableEntry *hte = s->host.hit_table + i;
    if (hte->tick_until < tick) continue;

    if (hte->dealt_by == dealt_by && hte->dealt_to == dealt_to )
      return 0;
  }

  for (int i = 0; i < ARR_LEN(s->host.hit_table); i++) {
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

typedef struct {
  struct { int x, y; } normal;
} HostEntTileCollisionOut;
static int host_ent_tile_collision(Env *env, Map *map, HostEnt *e, HostEntTileCollisionOut *out) {
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
  int best_offset_i = -1;
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
    float he =  map_tile_world_size/2;
    if (fabsf(dx) > he) hit = 1, e->x = cx + he * signf(dx);
    if (fabsf(dy) > he) hit = 1, e->y = cy + he * signf(dy);

    if (hit && out)
      out->normal.x = offsets[best_offset_i].x,
      out->normal.y = offsets[best_offset_i].y;
    return hit;
  }
  return 0;
}

typedef struct {
  HostEnt *hitter;
  struct { float x, y; } swing_dir;
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
        }
        EntId p_id = host_ent_id(env, p);
        EntId e_id = host_ent_id(env, e);
        return host_hit_table_debounce(env, p_id, e_id);
      }
    }
  }
  return 0;
}

static int ts_ent_hurt_player(Env *env, HostEnt *collider) {
  HostEntEntCollisionState heecs = { .env = env, .collider = collider };
  while (host_ent_ent_collision(&heecs)) {
    HostEnt *hit = host_ent_get(env, heecs.hit_id);
    if (hit->is_player) {
      hit->hp -= 1;
      if (hit->hp <= 0)
        hit->hp = hit->max_hp,
        hit->x = hit->y = 0.0f;
      return 1;
    }
  }
  return 0;
}

static void ts_sword(Env *env, TutorialState *ts, EntId sword_id) {
  HostEnt *sword = host_ent_get(env, sword_id);
  *sword = (HostEnt) {0};

  Map *map = &ts->map;
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

static void ts_bouncy_saw_tick(Env *env, TutorialState *ts, BouncySaw *bs) {
  HostEnt *bse = host_ent_get(env, eit_p(&ts->eit, bs));
  *bse = (HostEnt) { .kind = EntKind_Limbo, .looks = 'O' };
  switch (bs->state) {

    case (BouncySawState_Init): {
      bs->state = BouncySawState_Going;
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
      if (can_hurt && ts_ent_hurt_player(env, bse))
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

      fbe->x = lerp(sm->pos_state_start.x, sm->pos_state_end.x, t);
      fbe->y = lerp(sm->pos_state_start.y, sm->pos_state_end.y, t);
      fbe->kind = EntKind_Alive;

      /* bullets hitting stuff is cool */
      if (ts_ent_hurt_player(env, fbe))
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
    sm->hp -= 1;
    if (sm->hp <= 0)
      sm->state = SawMinionState_Dead;
    else {
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
  Map *map = &ts->map;

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

static void host_tick(Env *env) {
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

  /* tutorial level logic */
  TutorialState *ts = &s->host.tutorial;
  EntIdTable *eit = &ts->eit;
  eit->pointer_offset = ts;
  eit->ent_id_offset = 50;

  /* TODO: make ts authoritative about this */
  memcpy(&ts->map, &s->map, sizeof(Map));
  Map *map = &ts->map;

  /* move players */
  for (int i = 0; i < CLIENTS_MAX; i++) {
    Client *c = s->host.clients + i;
    if (!c->active) continue;

    /* apply Client velocity */
    host_ent_get(env, c->pc)->x += c->vel.x * 6.0f;
    host_ent_get(env, c->pc)->y += c->vel.y * 6.0f;
    host_ent_tile_collision(env, map, host_ent_get(env, c->pc), NULL);
  }

  ts_sword(env, ts, eit_p(eit, &ts->sword_collected));

  /* find range of X axis values occupied by shooty thingies */
  int shooter_min_x = 1e9, shooter_max_x = 0;
  MapIter mi = { .map = map };
  while (map_iter(&mi, 0)) {
    if (mi.c != '^' && mi.c != 'v') continue;

    if (mi.tx < shooter_min_x) shooter_min_x = mi.tx;
    if (mi.tx > shooter_max_x) shooter_max_x = mi.tx;
  }

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
    if (host_ent_sword_collision(env, shooty, NULL))
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
      if (host_ent_tile_collision(env, map, fb, NULL))
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
    /* bump up the health
     * add more rooms with just minions
     * health potions? */

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

      case (SawMasterState_Blink): {
        sme->kind = ((int)(env->ts() * 1000)/20 % 2) ? EntKind_Limbo : EntKind_Alive;

        if (t < 1.0) break;
        sm->state = (sm->times_killed >= 3)
          ? SawMasterState_Defeated
          : SawMasterState_WaveStart;
      } break;

      case (SawMasterState_WaveStart): {
        /* uhhh ... TODO: structify these? */
        sm->bouncy_saw_count = 0;
        memset(sm->bouncy_saws, 0, sizeof(sm->bouncy_saws));
        sm->minion_count = 0;
        memset(sm->minions, 0, sizeof(sm->minions));

        sm->state = SawMasterState_BouncySaw;
        sm->saws_launched = 0;
        sm->saws_to_launch = (1 + sm->times_killed) * 3;
        sm->ts_state_start = env->ts();
        sm->ts_state_end   = env->ts() + 0.5f;
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
        if (host_ent_sword_collision(env, sme, NULL) && --sm->hp == 0) {
          sm->times_killed++;

          sm->ts_state_start = env->ts();
          sm->ts_state_end   = env->ts() + 0.8f*sm->times_killed;
          sm->state = SawMasterState_Blink;
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
      ts_bouncy_saw_tick(env, ts, sm->bouncy_saws + i);

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
      .max_hp = 3,
      .hp = 3,
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


