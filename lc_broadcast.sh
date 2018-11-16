#!/bin/bash
#
# File: /usr/sbin/lc_broadcast.sh
#
# UDP broadcast for Lap Counter host
# This script needs to start on boot, see lc_broadcast.service

# network device (may need to be adjusted to suit)
DEV=eth0
# id to broadcast (the client identifies the correct host)
HOSTID="LC1"
# hostname
HOSTNAME=`hostname`
# broadcast port (the client listens for broadcast packets on this port)
BC_PORT=2000
#broadcast interval in seconds (must be less than the timneout specified in the client)
INTERVAL=10

# wait for system to settle down
sleep 20

#get the broadcast address for the specified ethernet device
bc_address=`/bin/ip a s dev $DEV | awk '/inet / {print $4}'`
echo "Broadcasting on $bc_address"

# broadcast endless loop
while true
do
        echo -e "LC1\t2006\t$HOSTNAME" | /bin/nc -ub -w0 $bc_address $BC_PORT
        echo "Broadcast sent.."
        sleep $INTERVAL
done

echo "Broadcast exited"
