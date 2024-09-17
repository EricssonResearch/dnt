#!/bin/bash

CNTFILE=/tmp/r2dtwo_test_envs_oam.count
function talker() { ip netns exec talker $@ ; }
function listener() { ip netns exec listener $@ ; }
function admin() { ip netns exec admin $@ ; }
function nxp1() { ip netns exec nxp1 $@ ; }
function nxp2() { ip netns exec nxp2 $@ ; }
export -f talker
export -f listener
export -f admin
export -f nxp1
export -f nxp2

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source env.sh' as root"
  return -1
fi

if [ ! -f "/usr/local/bin/r2dtwo" ]; then
  echo "r2dtwo executable not found."
  echo "Compile r2dtwo and copy to /usr/local/bin"
  return -2
fi


function configure_tc() {

  nxp1 ip link add r2eth0 type veth peer name r2eth1
  nxp1 ip link set dev r2eth0 up
  nxp1 ip link set dev r2eth1 up
  nxp1 tc qdisc add dev swp2 handle ffff: ingress
  nxp1 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.100.11 dst_ip 10.0.200.22 action mirred egress redirect dev r2eth0

  nxp2 ip link add r2eth0 type veth peer name r2eth1
  nxp2 ip link set dev r2eth0 up
  nxp2 ip link set dev r2eth1 up
  nxp2 tc qdisc add dev swp2 handle ffff: ingress
  nxp2 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.200.22 dst_ip 10.0.100.11 action mirred egress redirect dev r2eth0
}

export -f configure_tc

configure_networkenv() {
  echo "Initialize r2dtwo test environment"
  # Create the test namespace
  ip netns add talker 2>/dev/null
  ip netns add listener 2>/dev/null
  ip netns add admin 2>/dev/null
  ip netns add nxp1 2>/dev/null
  ip netns add nxp2 2>/dev/null

  ip link add swp2 netns nxp1 type veth peer eth0 netns talker
  ip link add swp0 netns nxp1 type veth peer swp0 netns nxp2
  ip link add swp1 netns nxp1 type veth peer swp1 netns nxp2
  ip link add swp2 netns nxp2 type veth peer eth0 netns listener

  ip link add mgmt0 netns nxp1 type veth peer mgmt1 netns admin
  ip link add mgmt0 netns nxp2 type veth peer mgmt2 netns admin

  # Configure the test environment inside the namespace
  talker ip link set dev lo up
  listener ip link set dev lo up
  admin ip link set dev lo up
  nxp1 ip link set dev lo up
  nxp2 ip link set dev lo up

  configure_tc

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp0 mtu 1600 up      # bigger MTU for NNI interfaces
  nxp1 ip link set dev swp1 mtu 1600 up
  nxp1 ip link set dev swp2 up
  nxp1 ip link set dev mgmt0 up
  nxp2 ip link set dev swp0 mtu 1600 up
  nxp2 ip link set dev swp1 mtu 1600 up
  nxp2 ip link set dev swp2 up
  nxp2 ip link set dev mgmt0 up

  admin ip link add mgmt type bridge
  admin ip link set mgmt1 master mgmt
  admin ip link set mgmt2 master mgmt
  admin ip link set mgmt up
  admin ip link set mgmt1 up
  admin ip link set mgmt2 up
  admin ip a a 172.16.0.254/24 dev mgmt

  # Configure the addresses, IPv4 and IPv6
  talker ip address add 10.0.100.11/24 dev eth0
  listener ip address add 10.0.200.22/24 dev eth0
  nxp1 ip address add 192.168.55.1/24 dev swp0
  nxp1 ip address add 192.168.66.1/24 dev swp1
  nxp1 ip address add 10.0.100.1/24 dev swp2
  nxp1 ip a a 172.16.0.1/24 dev mgmt0
  nxp2 ip address add 192.168.55.2/24 dev swp0
  nxp2 ip address add 192.168.66.2/24 dev swp1
  nxp2 ip address add 10.0.200.1/24 dev swp2
  nxp2 ip a a 172.16.0.2/24 dev mgmt0

  # Enable IP forwarding
  nxp1 sysctl -w net.ipv4.ip_forward=1
  nxp2 sysctl -w net.ipv4.ip_forward=1

  # Configure routing
  talker ip route add default via 10.0.100.1
  listener ip route add default via 10.0.200.1
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

/bin/bash --init-file <(echo "PS1='(oam) \u:\W# '")

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
  ip netns del admin 2>/dev/null
  ip netns del nxp1 2>/dev/null
  ip netns del nxp2 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi
