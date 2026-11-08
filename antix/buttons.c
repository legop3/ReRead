/* Tiny i386 evdev viewer. No libc, graphics server, desktop, or app framework. */
typedef unsigned int u32;
typedef unsigned short u16;
#include "reader-keys.h"
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
static void signed_number(int n) { if(n<0) { add("-"); number(0u-(unsigned)n); } else number(n); }
static void hex(unsigned n) { char b[8]; unsigned i=0; add("0x"); do { b[i++]="0123456789abcdef"[n&15]; n>>=4; } while(n); while(i && used<sizeof(output)-1) output[used++]=b[--i]; output[used]=0; }
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
static unsigned is_reader[64], scans[64], has_scan[64];
static unsigned count, event_count, binding_count, mapped_count;
static int last = -1, logfd = -1;
static struct event latest;
static unsigned power_down[64], power_block_until;
static const char *power_message = "Power: press and release to sleep";
static unsigned milliseconds(void) {
    struct { unsigned sec, nsec; } ts;
    if(call(265,1,(int)&ts,0,0)<0) return 0; /* clock_gettime(CLOCK_MONOTONIC) */
    return ts.sec*1000u+ts.nsec/1000000u;
}
static const char *reader_name(unsigned value, int by_scan) {
    unsigned i;
    for(i=0;i<READER_KEY_COUNT;i++)
        if((by_scan ? reader_keys[i].scan : reader_keys[i].key)==value) return reader_keys[i].name;
    return by_scan ? "unknown scan" : keyname(value);
}
static void bind_reader_keys(int fd) {
    unsigned i;
    for(i=0;i<READER_KEY_COUNT;i++) {
        unsigned pair[2]={reader_keys[i].scan,0};
        int status=call(54,fd,0x80084504,(int)pair,0); /* EVIOCGKEYCODE */
        used=0; add("MAP SCAN "); hex(pair[0]); add(" "); add(reader_keys[i].name);
        if(status<0) { add(" QUERY ERROR "); signed_number(status); }
        else if(pair[1]!=reader_keys[i].key) {
            pair[1]=reader_keys[i].key;
            status=call(54,fd,0x40084504,(int)pair,0); /* EVIOCSKEYCODE */
            if(status==0) { binding_count++; mapped_count++; add(" BOUND KEY "); number(pair[1]); }
            else { add(" BIND ERROR "); signed_number(status); }
        } else { mapped_count++; add(" EXISTING KEY "); number(pair[1]); }
        add("\n"); text(logfd,output);
    }
}
static void screen(void) {
    used = 0;
    add("\033[2J\033[HReRead - BOOT OK\nRoot: SSD\n\nPress a built-in button.\nInputs: "); number(count);
    add("  Bindings: "); number(binding_count);
    add("\nMapped: "); number(mapped_count); add("/"); number(READER_KEY_COUNT);
    add("\n\nLast input:\n");
    if (last < 0) add(count ? "Waiting for a button...\n\n\n" : "No input devices detected.\n\n\n");
    else {
        unsigned i;
        for (i=0; names[last][i] && i<36; i++) { char b[2] = {names[last][i],0}; add(b); }
        if(latest.type==1) {
            add("\nCode "); number(latest.code); add("  ");
            add(is_reader[last] ? reader_name(latest.code,0) : keyname(latest.code));
            add("\n"); add(latest.value == 0 ? "RELEASED" : latest.value == 1 ? "PRESSED" : "REPEAT");
        } else if(latest.type==4 && latest.code==4) {
            add("\nSCAN "); hex((unsigned)latest.value); add("  ");
            add(is_reader[last] ? reader_name((unsigned)latest.value,1) : "scancode");
            add("\nNo key event yet");
        } else {
            add("\nType "); number(latest.type); add(" Code "); number(latest.code);
            add("\nValue "); signed_number(latest.value);
        }
        if(has_scan[last]) { add("\nScan: "); hex(scans[last]); }
        add("\n");
    }
    add("Events: "); number(event_count); add("\n\n"); add(power_message); add("\n");
    {
        char status[96];
        int fd=call(5,(int)"/run/reader-usb-status",0,0,0);
        if(fd>=0) {
            int n=call(3,fd,(int)status,sizeof(status)-1,0);
            call(6,fd,0,0,0);
            if(n>0) { status[n]=0; add(status); }
        }
    }
    text(1, output);
}
static int suspend_system(void) {
    int pid=call(2,0,0,0,0), status=0, result;
    if(pid<0) return 0;
    if(pid==0) {
        const char *args[]={"/usr/local/sbin/reader-power","suspend",0};
        const char *env[]={0};
        call(11,(int)args[0],(int)args,(int)env,0);
        call(1,127,0,0,0);
        for(;;) {}
    }
    do { result=call(7,pid,(int)&status,0,0); } while(result==-4);
    return result==pid && status==0;
}
static void power_event(unsigned device, const struct event *e) {
    unsigned i;
    struct event discarded;
    if(e->type!=1 || e->code!=116) return;
    if(power_block_until && (int)(milliseconds()-power_block_until)<0) { power_down[device]=0; return; }
    if(e->value==1) { power_down[device]=1; return; }
    if(e->value!=0 || !power_down[device]) return;
    power_down[device]=0;
    power_message="Sleeping... Press Power to wake."; screen();
    power_message=suspend_system() ? "Awake. Power: press and release to sleep" :
        "Sleep failed: /var/log/reader/power.log";
    /* Discard the wake press and duplicate AT/ACPI reports, not the next press. */
    for(i=0;i<64;i++) {
        power_down[i]=0;
        if(devices[i].fd>=0)
            while(call(3,devices[i].fd,(int)&discarded,sizeof(discarded),0)==sizeof(discarded)) {}
    }
    power_block_until=milliseconds()+1000;
    screen();
}
static void discover(void) {
    unsigned i;
    for (i=0;i<64;i++) if (devices[i].fd < 0) {
        char path[40]; unsigned p=0,j; const char *prefix="/dev/input/event";
        while (prefix[p]) { path[p]=prefix[p];p++; }
        if (i>=10) path[p++]='0'+i/10;
        path[p++]='0'+i%10; path[p]=0;
        int fd=call(5,(int)path,2050,0,0); /* O_RDWR | O_NONBLOCK for keymap ioctls */
        if (fd<0) continue;
        for(j=0;j<128;j++) names[i][j]=0;
        if(call(54,fd,0x80804506,(int)names[i],0)<0) {
            for(j=0;path[j];j++) names[i][j]=path[j];
        }
        names[i][127]=0;
        struct { u16 bus, vendor, product, version; } id;
        is_reader[i]=same(names[i],"AT Translated Set 2 keyboard") &&
            call(54,fd,0x80084502,(int)&id,0)==0 && id.bus==0x11;
        has_scan[i]=0; power_down[i]=0;
        if(is_reader[i]) bind_reader_keys(fd);
        call(55,fd,2,1,0); /* FD_CLOEXEC: power helper must not inherit input grabs. */
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
        for (i=0;i<READER_KEY_COUNT;i++) {
            unsigned j;
            if (!reader_keys[i].id || !reader_keys[i].key) return 1;
            for(j=0;j<i;j++)
                if(reader_keys[i].id==reader_keys[j].id ||
                   reader_keys[i].scan==reader_keys[j].scan ||
                   reader_keys[i].key==reader_keys[j].key) return 1;
            if (!same(reader_name(reader_keys[i].scan,1),reader_keys[i].name) ||
                !same(reader_name(reader_keys[i].key,0),reader_keys[i].name)) return 1;
        }
        text(1,"SELFTEST OK: i386 input_event=16 bytes; unique button IDs/scans/codes\n"); return 0;
    }
    logfd=call(5,(int)"/tmp/buttons.log",1|64|1024,0600,0);
    if(logfd>=0) call(55,logfd,2,1,0);
    for(i=0;i<64;i++) devices[i].fd=-1;
    discover(); screen();
    for (;;) {
        call(168,(int)devices,64,500,0);
        for(i=0;i<64;i++) if(devices[i].fd>=0 && devices[i].revents) {
            struct event e;
            int got;
            while((got=call(3,devices[i].fd,(int)&e,sizeof(e),0))==(int)sizeof(e)) {
                if(e.type!=0) {
                    if(e.type==4 && e.code==4) { scans[i]=(unsigned)e.value; has_scan[i]=1; }
                    latest=e; last=i; event_count++; screen();
                    used=0; add("TIME "); number(e.sec); add("."); number(e.usec);
                    add(" TYPE "); number(e.type); add(" CODE "); number(e.code); add(" VALUE "); signed_number(e.value);
                    add(" DEVICE "); number(i); add("\n"); text(logfd,output);
                    power_event(i,&e);
                }
            }
            if(got==-19 || got==0) { call(6,devices[i].fd,0,0,0); devices[i].fd=-1; count--; }
        }
        if(++tick>=10) { discover(); screen(); tick=0; }
    }
}
__asm__(".global _start\n_start:\nxor %ebp,%ebp\npop %eax\nmov %esp,%edx\npush %edx\npush %eax\ncall main\nmov %eax,%ebx\nmov $1,%eax\nint $0x80\n");
