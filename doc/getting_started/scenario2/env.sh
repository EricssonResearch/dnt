CNTFILE=/tmp/r2dtwo_test_envs.count
alias r2exec="ip netns exec tsnodn"

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source env.sh' as root"
  return -1
fi

if [ ! -f "/usr/local/bin/r2dtwo" ]; then
  echo "r2dtwo executable not found."
  echo "Compile r2dtwo and copy to /usr/local/bin"
  return -2
fi

configure_networkenv() {
  echo "Initialize r2dtwo test environment"
  export PS1="TSNoDetNet# "
  # Create the test namespace
  ip netns add tsnodn 2>/dev/null

  # Configure the test environment inside the namespace
  r2exec ip link set dev lo up
  r2exec ip link add teth0 type veth peer name sw1p0
  r2exec ip link add leth0 type veth peer name sw2p0
  r2exec ip link add sw1p1 type veth peer name sw2p1
  r2exec ip link add sw1p2 type veth peer name sw2p2

  r2exec ip link set teth0 up
  r2exec ip link set leth0 up
  r2exec ip link set sw1p0 up
  r2exec ip link set sw1p1 up
  r2exec ip link set sw1p2 up
  r2exec ip link set sw2p0 up
  r2exec ip link set sw2p1 up
  r2exec ip link set sw2p2 up

  # Enable packet routing
  r2exec sysctl -w net.ipv4.ip_forward=1
  r2exec sysctl -w net.ipv4.conf.leth0.accept_local=1
  r2exec sysctl -w net.ipv4.conf.sw1p1.accept_local=1
  r2exec sysctl -w net.ipv4.conf.sw1p2.accept_local=1
  r2exec sysctl -w net.ipv4.conf.sw2p1.accept_local=1
  r2exec sysctl -w net.ipv4.conf.sw2p2.accept_local=1

  # Configure the addresses
  r2exec ip address add 10.0.0.1/24 dev teth0
  r2exec ip address add 10.0.0.2/24 dev leth0

  r2exec ip address add 192.168.55.1/24 dev sw1p1
  r2exec ip address add 192.168.55.2/24 dev sw2p1
  r2exec ip address add 192.168.66.1/24 dev sw1p2
  r2exec ip address add 192.168.66.2/24 dev sw2p2
}

# This is totally unsafe
# TODO: use flock to avoid races
if [ -f "$CNTFILE" ]; then
  cntvalue=`cat $CNTFILE`
  newvalue=`expr $cntvalue + 1`
  echo $newvalue > $CNTFILE
else
  configure_networkenv
  echo "1" > $CNTFILE
fi

ip netns exec tsnodn /bin/bash

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del tsnodn
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi
