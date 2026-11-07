/* Tiny i386 evdev viewer. No libc, graphics server, desktop, or app framework. */
typedef unsigned int u32;
typedef unsigned short u16;
struct event { u32 sec, usec; u16 type, code; int value; };
struct pollfd { int fd; short events, revents; };
typedef char event_must_be_16_bytes[sizeof(struct event) == 16 ? 1 : -1];
static int call(int n, int a, int b, int c, int d) {
    int result;
    __asm__ volatile("int $0x80" : "=a"(result) : "0"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory", "cc");
    return result;
}
static unsigned length(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void text(int fd, const char *s) { call(4, fd, (int)s, length(s), 0); }
static char output[2048];
static unsigned used;
static void add(const char *s) { while (*s && used < sizeof(output)-1) output[used++] = *s++; output[used] = 0; }
static void number(unsigned n) { char b[12]; unsigned i = 0; do { b[i++] = '0' + n % 10; n /= 10; } while (n); while (i && used < sizeof(output)-1) output[used++] = b[--i]; output[used] = 0; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static const char *keyname(unsigned code) {
    switch (code) {
      case 1: return "ESC"; case 28: return "ENTER"; case 57: return "SPACE";
      case 103: return "UP"; case 108: return "DOWN"; case 105: return "LEFT";
      case 106: return "RIGHT"; case 115: return "VOLUME UP";
      case 114: return "VOLUME DOWN"; case 116: return "POWER";
      case 113: return "MUTE"; case 164: return "PLAY/PAUSE";
      default: return "unmapped";
    }
}
static struct pollfd devices[64];
static char names[64][128];
static unsigned count, event_count;
static int last = -1, logfd = -1;
static struct event latest;
static void screen(void) {
    used = 0;
    add("\033[2J\033[HREADER POC - BOOT OK\n\nPress a built-in button.\nInputs: "); number(count);
    add("\n\nLast input:\n");
    if (last < 0) add(count ? "Waiting for a button...\n\n\n" : "No input devices detected.\n\n\n");
    else {
        unsigned i;
        for (i=0; names[last][i] && i<36; i++) { char b[2] = {names[last][i],0}; add(b); }
        add("\nCode "); number(latest.code); add("  "); add(keyname(latest.code));
        add("\n"); add(latest.value == 0 ? "RELEASED" : latest.value == 1 ? "PRESSED" : "REPEAT"); add("\n");
    }
    add("Events: "); number(event_count); add("\n");
    text(1, output);
}
static void discover(void) {
    unsigned i;
    for (i=0;i<64;i++) if (devices[i].fd < 0) {
        char path[40]; unsigned p=0,j; const char *prefix="/dev/input/event";
        while (prefix[p]) { path[p]=prefix[p];p++; }
        if (i>=10) path[p++]='0'+i/10;
        path[p++]='0'+i%10; path[p]=0;
        int fd=call(5,(int)path,2048,0,0); /* O_RDONLY | O_NONBLOCK */
        if (fd<0) continue;
        for(j=0;j<128;j++) names[i][j]=0;
        if(call(54,fd,0x80804506,(int)names[i],0)<0) {
            for(j=0;path[j];j++) names[i][j]=path[j];
        }
        names[i][127]=0;
        call(54,fd,0x40044590,1,0); /* Grab only while this tester owns input. */
        devices[i].fd=fd; devices[i].events=1; devices[i].revents=0; count++;
        text(logfd,"DEVICE "); text(logfd,path); text(logfd," "); text(logfd,names[i]); text(logfd,"\n");
    }
}
int main(int argc, char **argv) {
    unsigned i, tick=0;
    if (argc>1 && (same(argv[1],"--poweroff") || same(argv[1],"--reboot"))) {
        call(36,0,0,0,0);
        call(88,0xfee1dead,672274793,same(argv[1],"--reboot") ? 0x01234567 : 0x4321fedc,0);
        return 1;
    }
    if (argc>1 && same(argv[1],"--self-test")) {
        if (!same(keyname(103),"UP") || !same(keyname(999),"unmapped")) return 1;
        text(1,"SELFTEST OK: i386 input_event=16 bytes\n"); return 0;
    }
    logfd=call(5,(int)"/tmp/buttons.log",1|64|1024,0600,0);
    for(i=0;i<64;i++) devices[i].fd=-1;
    discover(); screen();
    for (;;) {
        call(168,(int)devices,64,500,0);
        for(i=0;i<64;i++) if(devices[i].fd>=0 && devices[i].revents) {
            struct event e;
            int got;
            while((got=call(3,devices[i].fd,(int)&e,sizeof(e),0))==(int)sizeof(e)) {
                if(e.type==1) {
                    latest=e; last=i; event_count++; screen();
                    used=0; add("KEY "); number(e.code); add(" VALUE "); number(e.value);
                    add(" DEVICE "); number(i); add("\n"); text(logfd,output);
                }
            }
            if(got==-19 || got==0) { call(6,devices[i].fd,0,0,0); devices[i].fd=-1; count--; }
        }
        if(++tick>=10) { unsigned old=count; discover(); if(count!=old) screen(); tick=0; }
    }
}
__asm__(".global _start\n_start:\nxor %ebp,%ebp\npop %eax\nmov %esp,%edx\npush %edx\npush %eax\ncall main\nmov %eax,%ebx\nmov $1,%eax\nint $0x80\n");
