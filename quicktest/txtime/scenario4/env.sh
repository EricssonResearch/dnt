CNTFILE=/tmp/r2dtwo_test_envs_tsnodn.count
alias talker="ip netns exec talker"
alias listener="ip netns exec listener"
alias nxp1="ip netns exec nxp1"
alias nxp2="ip netns exec nxp2"

ALIASES='alias talker="ip netns exec talker"; alias listener="ip netns exec listener"; alias nxp1="ip netns exec nxp1"; alias nxp2="ip netns exec nxp2"'

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
  
  # Create the test namespace
  ip netns add talker 2>/dev/null
  ip netns add listener 2>/dev/null
  ip netns add nxp1 2>/dev/null
  ip netns add nxp2 2>/dev/null

  ip link add swp2 numtxqueues 4 netns nxp1 type veth peer eth0 numtxqueues 4 netns talker
  ip link add swp0 numtxqueues 4 netns nxp1 type veth peer swp0 numtxqueues 4 netns nxp2
  ip link add swp2 numtxqueues 4 netns nxp2 type veth peer eth0 numtxqueues 4 netns listener

  # Configure the test environment inside the namespace
  talker ip link set dev lo up
  listener ip link set dev lo up
  nxp1 ip link set dev lo up
  nxp2 ip link set dev lo up

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp0 mtu 1600 up
  nxp1 ip link set dev swp2 up
  nxp2 ip link set dev swp0 mtu 1600 up
  nxp2 ip link set dev swp2 up

  # Configure the addresses
  talker ip address add 10.0.100.11/24 dev eth0
  listener ip address add 10.0.200.22/24 dev eth0

  nxp1 ip address add 192.168.55.1/24 dev swp0
  nxp1 ip address add 10.0.100.1/24 dev swp2

  nxp2 ip address add 192.168.55.2/24 dev swp0
  nxp2 ip address add 10.0.200.1/24 dev swp2

  # Configure routing
  nxp1 sysctl -w net.ipv4.ip_forward=1
  nxp2 sysctl -w net.ipv4.ip_forward=1
  talker ip route add default via 10.0.100.1
  listener ip route add default via 10.0.200.1

  # Configure ETF qdisc
  nxp1 tc qdisc add dev swp0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
  nxp1 tc qdisc replace dev swp0 parent 100:2 etf clockid CLOCK_TAI delta 1000000000 skip_sock_check
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

/bin/bash --init-file <(echo "$ALIASES; PS1='(ip over detnet) \u:\W# '")

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then # last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  # Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
  ip netns del nxp1 2>/dev/null
  ip netns del nxp2 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi
