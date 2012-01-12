#!/bin/sh
#
#----------------------------------------------------------------------------#
# HP-UX client for Xymon                                                     #
#                                                                            #
# Copyright (C) 2005-2011 Henrik Storner <henrik@hswn.dk>                    #
#                                                                            #
# This program is released under the GNU General Public License (GPL),       #
# version 2. See the file "COPYING" for details.                             #
#                                                                            #
#----------------------------------------------------------------------------#
#
# $Id: xymonclient-hp-ux.sh 6712 2011-07-31 21:01:52Z storner $

echo "[date]"
date
echo "[uname]"
uname -a
echo "[uptime]"
uptime
echo "[who]"
who
echo "[df]"
# The sed stuff is to make sure lines are not split into two.
df -Pk | sed -e '/^[^ 	][^ 	]*$/{
N
s/[ 	]*\n[ 	]*/ /
}'
echo "[mount]"
mount
echo "[memory]"
# $XYMONHOME/bin/hpux-meminfo
# From Earl Flack  http://lists.xymon.com/archive/2010-December/030100.html 
FREE=`/usr/sbin/swapinfo |grep ^memory |awk {'print $4'}`
FREEREPORT=`echo $FREE / 1024 |/usr/bin/bc`
TOTAL=`/usr/sbin/swapinfo |grep ^memory |awk {'print $2'}`
TOTALREPORT=`echo $TOTAL / 1024 |/usr/bin/bc`
echo Total:$TOTALREPORT
echo Free:$FREEREPORT
echo "[swapinfo]"
/usr/sbin/swapinfo -tm
echo "[ifconfig]"
netstat -in
echo "[route]"
netstat -rn
echo "[netstat]"
netstat -s
echo "[ifstat]"
/usr/sbin/lanscan -p | while read PPA; do /usr/sbin/lanadmin -g mibstats $PPA; done
echo "[ports]"
netstat -an | grep "^tcp"
echo "[ps]"
UNIX95=1 ps -Ax -o pid,ppid,user,stime,state,pri,pcpu,time,vsz,args

# $TOP must be set, the install utility should do that for us if it exists.
if test "$TOP" != ""
then
    if test -x "$TOP"
    then
        echo "[top]"
	# Cits Bogajewski 03-08-2005: redirect of top fails
	$TOP -d 1 -f $XYMONHOME/tmp/top.OUT
	cat $XYMONHOME/tmp/top.OUT
	rm $XYMONHOME/tmp/top.OUT
    fi
fi

# vmstat
nohup sh -c "vmstat 300 2 1>$XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ 2>&1; mv $XYMONTMP/xymon_vmstat.$MACHINEDOTS.$$ $XYMONTMP/xymon_vmstat.$MACHINEDOTS" </dev/null >/dev/null 2>&1 &
sleep 5
if test -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; then echo "[vmstat]"; cat $XYMONTMP/xymon_vmstat.$MACHINEDOTS; rm -f $XYMONTMP/xymon_vmstat.$MACHINEDOTS; fi

exit

