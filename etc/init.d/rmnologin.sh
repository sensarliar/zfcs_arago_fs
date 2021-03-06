#!/bin/sh
### BEGIN INIT INFO
# Provides:          rmnologin
# Required-Start:    $remote_fs $all
# Required-Stop: 
# Default-Start:     2 3 4 5
# Default-Stop:
# Short-Description: Remove /etc/nologin at boot
# Description:       This script removes the /etc/nologin file as the
#                    last step in the boot process, if DELAYLOGIN=yes.
#                    If DELAYLOGIN=no, /etc/nologin was not created by
#                    bootmisc earlier in the boot process.
### END INIT INFO

if test -f /etc/nologin.boot
then
	rm -f /etc/nologin /etc/nologin.boot
fi
ifconfig eth0 192.168.1.2
/home/root/led_flash.sh &
#/home/root/gps_test1 -qws -fn wenquanyi &
/home/root/gps_test1 -qws &
#/home/root/gps_test1 -qws &
: exit 0
