#!/bin/sh
set -eu
apk add --no-cache samba >/dev/null
adduser -D -H astratest
printf 'p,a=s\np,a=s\n' | smbpasswd -a -s astratest >/dev/null
chown astratest:astratest /srv/astra
exec /usr/sbin/smbd -F --no-process-group -s /etc/samba/smb.conf
