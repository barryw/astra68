#!/bin/sh
# Check that the DE25's Linux has every driver docs/FILESYSTEM_PRODUCTION_PRD.md
# ("Supported sources") requires, registered, built in, or loadable. Run on
# the board; loads nothing. Exits 1 if any is missing.
status=0
report() { # ok-or-not name how
    if [ "$1" = 0 ]; then echo "ok      $2 ($3)"; else echo "MISSING $2"; status=1; fi
}
filesystem() {
    if grep -qw "$1" /proc/filesystems; then report 0 "$1" registered
    else modprobe -n -q "fs-$1"; report $? "$1" "built in or loadable"; fi
}
module() {
    if [ -d "/sys/module/$1" ]; then report 0 "$1" "built in or loaded"
    else modprobe -n -q "$1"; report $? "$1" "built in or loadable"; fi
}

filesystem ext4     # system SD, fixed disks, USB
filesystem vfat     # USB FAT32
filesystem exfat    # USB exFAT
filesystem iso9660  # optical (Joliet, Rock Ridge, zisofs)
filesystem udf      # optical
filesystem tmpfs    # /ram, /tmp
module nls_utf8     # iocharset=utf8 for vfat and Joliet
module usb_storage  # USB mass storage (bulk-only)
module uas          # USB Attached SCSI
module sd_mod       # disks
module sr_mod       # optical drives (cdrom layer)
exit $status
