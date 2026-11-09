#!/bin/sh
# Standard login1 request. Completion is signaled by PrepareForSleep(false).
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
case "$1" in
    suspend) exec /usr/bin/loginctl suspend ;;
    *) exit 2 ;;
esac
