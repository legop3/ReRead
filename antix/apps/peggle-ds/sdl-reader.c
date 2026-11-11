// ReRead-specific SDL2 adapter for DeSmuME CLI.
// No system keyboard layout changes, input grabs or emulator source patches.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

typedef union {
    uint32_t type;
    struct {
        uint32_t type, timestamp, windowID;
        uint8_t state, repeat, padding2, padding3;
        struct { int32_t scancode, sym; uint16_t mod; uint32_t unused; } keysym;
    } key;
    uint8_t bytes[56];
} ReaderEvent;

#define SC(x) (0x40000000 | (x))
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_QUIT 0x100
#define SDL_WINDOW_OPENGL 0x00000002u
#define SDL_RENDERER_SOFTWARE 0x00000001u

// SDL2 keycodes used by the physical Reader's XKB symbols.
static int translate(ReaderEvent *e) {
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 1;
    int32_t key = e->key.keysym.sym, dest = 0;
    switch(key) {
      case '\r': dest='x'; break;           // OK -> A/shoot
      case 27: dest='z'; break;             // Back -> B
      case SC(118): dest='\r'; break;       // Menu/Options -> Start
      case SC(261): dest='a'; break;        // Play -> Y
      case SC(86): dest='q'; break;         // KP-minus -> L
      case SC(87): dest='w'; break;         // KP-plus -> R
      case SC(277): dest=SC(229); break;    // View -> right shift/Select
      case SC(274):                    // Favorites -> quit to ReRead
        if(e->type == SDL_KEYDOWN) { e->type=SDL_QUIT; return 1; }
        return 0;
      case SC(290): dest='o'; break;        // Voice Speed -> emulator boost
      case SC(282): return 0;               // System sleep is system-owned
      case SC(58): return 0;                // Guide/F1: no accidental hotkeys
      default: return 1;                   // D-pad remains arrow keys
    }
    if(dest) e->key.keysym.sym = dest;
    return 1;
}

static int read_event(int(*fn)(ReaderEvent*), ReaderEvent *e) {
    int got;
    do {
        got=fn(e);
        if(!got || translate(e)) return got;
    } while (got);
    return got;
}
int SDL_PollEvent(ReaderEvent *e) {
    static int (*real)(ReaderEvent*);
    if(!real) real=dlsym(RTLD_NEXT,"SDL_PollEvent");
    if(!e) return real(e);
    return read_event(real,e);
}
int SDL_WaitEvent(ReaderEvent *e) {
    static int (*real)(ReaderEvent*);
    if(!real) real=dlsym(RTLD_NEXT,"SDL_WaitEvent");
    if(!e) return real(e);
    return read_event(real,e);
}
int SDL_WaitEventTimeout(ReaderEvent *e,int ms) {
    static int (*real)(ReaderEvent*,int);
    if(!real) real=dlsym(RTLD_NEXT,"SDL_WaitEventTimeout");
    int result;
    do { result=real(e,ms); } while(result && !translate(e));
    return result;
}
void *SDL_CreateWindow(const char *title,int x,int y,int w,int h,uint32_t flags) {
    static void *(*real)(const char*,int,int,int,int,uint32_t);
    if(!real) real=dlsym(RTLD_NEXT,"SDL_CreateWindow");
    return real(title,x,y,w,h,flags & ~SDL_WINDOW_OPENGL);
}
void *SDL_CreateRenderer(void *window,int index,uint32_t flags) {
    static void *(*real)(void*,int,uint32_t);
    if(!real) real=dlsym(RTLD_NEXT,"SDL_CreateRenderer");
    (void)flags;
    return real(window,index,SDL_RENDERER_SOFTWARE);
}
