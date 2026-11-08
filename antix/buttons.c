/* ReRead controls monitor: Linux evdev, poll and ordinary POSIX interfaces. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "reader-keys.h"
static void text(int fd, const char *s) { if(fd>=0) (void)write(fd,s,strlen(s)); }
static char output[2048];
static unsigned used;
static void add(const char *s) { while (*s && used < sizeof(output)-1) output[used++] = *s++; output[used] = 0; }
static void number(unsigned n) { char b[24]; snprintf(b,sizeof(b),"%u",n); add(b); }
static void signed_number(int n) { char b[24]; snprintf(b,sizeof(b),"%d",n); add(b); }
static void hex(unsigned n) { char b[24]; snprintf(b,sizeof(b),"0x%x",n); add(b); }
static int same(const char *a,const char *b) { return strcmp(a,b)==0; }
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
static struct input_event latest;
static unsigned power_down[64], power_block_until;
static unsigned last_volume_change;
static const char *power_message = "Power: press and release to sleep";
static unsigned milliseconds(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)<0) return 0; /* clock_gettime(CLOCK_MONOTONIC) */
    return (unsigned)ts.tv_sec*1000u+(unsigned)ts.tv_nsec/1000000u;
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
        int status=ioctl(fd,EVIOCGKEYCODE,pair); /* EVIOCGKEYCODE */
        used=0; add("MAP SCAN "); hex(pair[0]); add(" "); add(reader_keys[i].name);
        if(status<0) { add(" QUERY ERROR "); signed_number(-errno); }
        else if(pair[1]!=reader_keys[i].key) {
            pair[1]=reader_keys[i].key;
            status=ioctl(fd,EVIOCSKEYCODE,pair); /* EVIOCSKEYCODE */
            if(status==0) { binding_count++; mapped_count++; add(" BOUND KEY "); number(pair[1]); }
            else { add(" BIND ERROR "); signed_number(-errno); }
        } else { mapped_count++; add(" EXISTING KEY "); number(pair[1]); }
        add("\n"); text(logfd,output);
    }
}
static void screen(void) {
    used = 0;
    add("\033[2J\033[HReRead\nSystem controls\n\nPress a built-in button.\nInputs: "); number(count);
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
        const char *paths[]={"/run/reader-usb-status","/run/reader-battery-status","/run/reader-audio-status"};
        unsigned i;
        for(i=0;i<3;i++) {
            char status[128];
            int fd=open(paths[i],O_RDONLY|O_CLOEXEC);
            if(fd>=0) {
                int n=read(fd,status,sizeof(status)-1);
                close(fd);
                if(n>0) { status[n]=0; add(status); }
            }
        }
    }
    text(1, output);
}
static int suspend_system(void) {
    int pid=fork(), status=0, result;
    if(pid<0) return 0;
    if(pid==0) {
        const char *args[]={"/usr/local/sbin/reader-power","suspend",0};
        const char *env[]={0};
        execve(args[0],(char *const *)args,(char *const *)env);
        _exit(127);
    }
    do { result=waitpid(pid,&status,0); } while(result<0 && errno==EINTR);
    return result==pid && status==0;
}
static void volume_event(unsigned device, const struct input_event *e) {
    int pid, result, status=0;
    if(!is_reader[device] || e->type!=1 || (e->value!=1 && e->value!=2) ||
       (e->code!=115 && e->code!=114)) return;
    if(e->value==2 && (unsigned)(milliseconds()-last_volume_change)<200) return;
    pid=fork();
    if(pid==0) {
        const char *args[]={"/usr/local/sbin/reader-audio",e->code==115 ? "up" : "down",0};
        const char *env[]={0};
        int fd=open("/var/log/reader/audio.log",O_WRONLY|O_CREAT|O_APPEND,0600);
        if(fd>=0) { dup2(fd,STDOUT_FILENO); dup2(fd,STDERR_FILENO); close(fd); }
        execve(args[0],(char *const *)args,(char *const *)env);
        _exit(127);
    }
    if(pid>0) do { result=waitpid(pid,&status,0); } while(result<0 && errno==EINTR);
    last_volume_change=milliseconds();
    screen();
}
static void power_event(unsigned device, const struct input_event *e) {
    unsigned i;
    struct input_event discarded;
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
            while(read(devices[i].fd,&discarded,sizeof(discarded))==sizeof(discarded)) {}
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
        int fd=open(path,O_RDWR|O_NONBLOCK|O_CLOEXEC); /* O_RDWR | O_NONBLOCK for keymap ioctls */
        if (fd<0) continue;
        for(j=0;j<128;j++) names[i][j]=0;
        if(ioctl(fd,EVIOCGNAME(sizeof(names[i])),names[i])<0) {
            for(j=0;path[j];j++) names[i][j]=path[j];
        }
        names[i][127]=0;
        struct input_id id;
        is_reader[i]=same(names[i],"AT Translated Set 2 keyboard") &&
            ioctl(fd,EVIOCGID,&id)==0 && id.bustype==0x11;
        has_scan[i]=0; power_down[i]=0;
        if(is_reader[i]) bind_reader_keys(fd);
        fcntl(fd,F_SETFD,FD_CLOEXEC); /* FD_CLOEXEC: power helper must not inherit input grabs. */
        ioctl(fd,EVIOCGRAB,1); /* Release automatically when the input owner exits. */
        devices[i].fd=fd; devices[i].events=1; devices[i].revents=0; count++;
        text(logfd,"DEVICE "); text(logfd,path); text(logfd," "); text(logfd,names[i]); text(logfd,"\n");
    }
}
int main(int argc, char **argv) {
    unsigned i, tick=0;
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
        text(1,"SELFTEST OK: Linux evdev; unique button IDs/scans/codes\n"); return 0;
    }
    logfd=open("/var/log/reader/buttons.log",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
    if(logfd>=0) fcntl(logfd,F_SETFD,FD_CLOEXEC);
    for(i=0;i<64;i++) devices[i].fd=-1;
    discover(); screen();
    for (;;) {
        poll(devices,64,500);
        for(i=0;i<64;i++) if(devices[i].fd>=0 && devices[i].revents) {
            struct input_event e;
            int got;
            while((got=read(devices[i].fd,&e,sizeof(e)))==(int)sizeof(e)) {
                if(e.type!=0) {
                    if(e.type==4 && e.code==4) { scans[i]=(unsigned)e.value; has_scan[i]=1; }
                    latest=e; last=i; event_count++; screen();
                    used=0; add("TIME "); number((unsigned)e.time.tv_sec); add("."); number((unsigned)e.time.tv_usec);
                    add(" TYPE "); number(e.type); add(" CODE "); number(e.code); add(" VALUE "); signed_number(e.value);
                    add(" DEVICE "); number(i); add("\n"); text(logfd,output);
                    power_event(i,&e);
                    volume_event(i,&e);
                }
            }
            if((got<0 && errno==ENODEV) || got==0) { close(devices[i].fd); devices[i].fd=-1; count--; }
        }
        if(++tick>=10) { discover(); screen(); tick=0; }
    }
}
