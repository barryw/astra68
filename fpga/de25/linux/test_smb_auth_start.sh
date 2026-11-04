#!/bin/sh
set -eu
apk add --no-cache samba >/dev/null
id astratest >/dev/null 2>&1 || adduser -D -H astratest
printf 'p,a=s\np,a=s\n' | smbpasswd -a -s astratest >/dev/null
chown astratest:astratest /srv/astra
printf 'unicode' > '/srv/astra/0Café.txt'
printf 'private' > /srv/astra/server-denied
chmod 000 /srv/astra/server-denied
exec /usr/sbin/smbd -F --no-process-group -s /etc/samba/smb.conf
