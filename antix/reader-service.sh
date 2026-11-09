#!/bin/sh
# A standard PAM login session, supervised by runit. Apps inherit its identity.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
console=/dev/tty1
case " $(cat /proc/cmdline) " in *' console=ttyS0,'*) console=/dev/ttyS0 ;; esac
exec <"$console" >"$console" 2>&1
until loginctl list-sessions >/dev/null 2>&1; do sleep 1; done
exec /usr/bin/setsid --ctty /usr/bin/su --login --shell /usr/local/sbin/start-reader root
