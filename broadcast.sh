#!/bin/bash
# UDP broadcast for Lap Counter host
# This script needs to start on boot (e.g. via SYSTEM D)

# network device
DEV=eth0
# id to broadcast
HOSTID="LC1"
# broadcast port
BC_PORT=2000
#broadcast interval in seconds
INTERVAL=10

#get the broadcast address
bc_address=`/bin/ip a s dev $DEV | awk '/inet / {print $4}'`
echo "Broadcasting on $bc_address"

# broadcast loop
while true
do
        echo "LC1" | /bin/nc -ub -w0 $bc_address $BC_PORT
        echo "Broadcast sent.."
        sleep $INTERVAL
done

echo "Broadcast exited"
