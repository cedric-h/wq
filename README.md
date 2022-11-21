> vim: sw=2 ts=2 expandtab smartindent

Premise: assume you're a developer in the handmade community who has made a game that actually has more than 5 minutes of gameplay.

Rare, I know.

But let's say that's you. Your main looks something like this:

```
typedef struct { /* ... */ } GameState;

static struct {
  /* handles to resources our environment owns */
  struct { /* ... */ } gpu;
  struct { /* ... */ } os;

  /* it's all us, baby */
  GameState game;
} state = {0};

static void state_init(void) { /* fill up State */ }

void main(void) {
  state_init();

  while (1) {
    /* input handling, network msgs, etc. */
    for (OsEvent os_e = {0}; os_event(&os_e);)
      game_os_event(&os_e);

    /* this is a cool way to do it -- 2D vector game can get pretty far like this */
    game_geometry(state.gpu.vbuf, state.gpu.ibuf);
    // or maybe:
    // game_pixels(state.gpu.pixbuf);
  }
}
```

Pretty straightforward, right? Maybe your rendering is a little more complicated than just filling a single geometry buffer, or dumping pixels into a buffer, but there's still some "pinch point" where data is transformed from the logical representation of your game -- whatever was handy for implementing your game logic -- to something that the user sees. Maybe the GPU is used as part of this transformation, maybe not.

And maybe all of your state isn't stored in a single static struct. But as much of it as possible should be! You'll see why >:)

Just like Casey championed in his DLL video, you can pull apart your game logic and the code that juggles handles to OS resources. Maybe it looks something like this.

```c
// game.c

#include "os.h"
#include "gpu.h"
static struct { /* ... */ } game;

static void game_init(void);
static void game_os_event(OsEvent *os_e);
static void game_geometry(GpuVert *vbuf, uint16_t *ibuf);
```

```c
/* meanwhile, back in main.c ... */

/* handles to resources our environment owns */
static struct {
  struct { /* ... */ } gpu;
  struct { /* ... */ } os;
} env;

void main(void) {
  env_init();

  Dylib game_dylib = dylib_open("game.dll");
  dylib_call(game_dylib, "game_init");

  while (1) {
    /* input handling, network msgs, etc. */
    for (OsEvent os_e = {0}; os_event(&os_e);)
      dylib_call(game_dylib, "game_os_event", &os_e);
    
    /* --- NEW: reload! --- */
    if (dylib_changed(game_dylib))
      dylib_call(game_dylib, "game_init");

    dylib_call(game_dylib, "game_geometry", env.gpu.vbuf, env.gpu.ibuf);
  }
}
```

Now, if you recompile your game.dll, you don't need to close and reopen the window. Your game just automatically restarts.

The communication between our environment and our game had to be made super explicit, but, eh, we'd have to do that anyway to support multiple platforms, right?

It's great that your game automatically restarts, without having to close and reopen a window. Seems like a pretty small win, though, right? I mean c'mon, that window takes a second to pop open. Why bother?

Let's circle back to the intro. You have a game with more than 5 minutes of gameplay. That probably means there's a state in the game that takes 5 minutes to reach. You want to add a 6th minute. You need to write and test 10 changes to your gameplay code (really? only 10!? you're a much better programmer than I am!) to make that 6th minute.

That's 50 minutes you spend doing nothing but playing the first five minutes of your game over, and over, and over. But hey, a lot that's useful, it's important to play your own game, right? Except it's knocking you out of a state of flow, you're focused on that 6th minute, you're pretty much happy with those first 5. And you're going to feel inclined to simplify those first 5 so that you can do them in 2. Except now, haha, your game has fewer minutes of gameplay! If your goal was to make something that can entertain someone indefinitely, putting yourself in a situation where you have to replay those first five over and over isn't setting you up for success.

There's a canonical solution to this, right? If there's a state in your game it takes 5 minutes to reach, you, the developer, are probably not the only one who wants to be able to jump directly into that state. Your players probably also want to be able to save their progress so they can safely restart your program and still pick up where they left off.

So you implement a save/load system, maybe you have a debug flag that skips the save selection menu and automatically opens the first save, good to bueno, no?

That's kind of complicated. There's a couple moving parts. What if there was a simpler way?

# Mason Remaley hot reloading

Beloved indie Rust gamedev Mason Remaley has his own improvement over Casey Muratori's approach as demonstrated in the code sample above.

The crazy motherfucker serializes his entire game state to a file at the end of a frame, and deserializes it from the file at the beginning of every frame.

Yeah.

I won't lie, I still don't fully comprehend the ramifications of this. But it seems so much easier than orchestrating some sort of delicate dance between your "environment" and "game" wherein eh, you know, sometimes the memory from the old game is passed in if it's known to be compatible but maybe it isn't and then weird things happen ... no. If you have good and robust serialization, this can be automatically detected and then prompt the developer, "ruh-roh, this aint compatible no more. you gon' fix it or can i just reinitialize?"

It benefits a lot from a language with compile time metaprogramming like Zig or Rust where you just define your data structures and the metaprogram does the rest, but you can imagine something with C where the serialization protocol is `memcpy` and you just preemptively add extra padding to structs for when you add fields later, and bump a version number when you make breaking changes. And just, you know, don't forget to bump the version number.

It _sounds_ grody but I do wonder how much of a difference it would really make; dodging the surface area for chaos that your metaprogram represents may be worthwhile, obviously the performance characteristics are leagues better, and, I don't know about you, but when I'm fucking with my code's data models, I'm definitely not in that iterative "tweak, test, tweak, test," cycle that benefits so tremendously from live-reloading. Data models (especially beyond simply adding fields) are for big thinky, quick typey, slow-deliberate-from-the-beginning testy. It works less well for proper save files since they'd be ABI-specific, but hey, maybe that's not a constraint you care about for your game. (It does make bug-reporting more convenient, but I wouldn't invest dev time in it until you have people on other architectures with bugs you're having trouble reproducing)

```c
// os_store.h

typedef struct {
  int nbytes;
  uint8_t *buf;
} OsStore;

/* returns 0 if our environment could do enough embiggening
 * (idk man maybe you compile to WASM and you're hitting limits of LocalStorage) */
 * (obviously don't copy this API if it doesn't represent the reasonable behavior of your targets) */
int os_store_embiggen(int new_size);
```

```c
/* our main loop from main.c again */

  OsStore store = {0};
  while (1) {
    if (store.nbytes)
      /* game_deserialize can no-op if the version numbers don't match */
      dylib_call(game_dylib, "game_deserialize", store.nbytes, store.buf);

    /* input handling, network msgs, etc. */
    for (OsEvent os_e = {0}; os_event(&os_e);)
      dylib_call(game_dylib, "game_os_event", &os_e);
    
    /* --- NEW: reload! --- */
    if (dylib_changed(game_dylib))
      dylib_call(game_dylib, "game_init");

    dylib_call(game_dylib, "game_geometry", env.gpu.vbuf, env.gpu.ibuf);
    
    /* this should call os_store_embiggen */
    dylib_call(game_dylib, "game_serialize", &os_store);
  }
```
