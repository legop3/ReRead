#!/bin/sh
# Started and stopped with the existing USB management session.
export PYTHONPATH=/usr/lib/reread/vendor
export XDG_CONFIG_HOME=/var/lib/reader/file-manager/config
export PYTHONDONTWRITEBYTECODE=1
mkdir -p /var/lib/reader/files /var/lib/reader/apps /root/.cache /root/.config \
  /etc/reread/apps /usr/lib/reread/apps /usr/share/reread/apps \
  /var/lib/reader/file-manager/home "$XDG_CONFIG_HOME"
exec python3 -m copyparty -c /etc/reader/files.conf
