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

  ip link add swp2 netns nxp1 type veth peer eth0 netns talker
  ip link add swp0 netns nxp1 type veth peer swp0 netns nxp2
  ip link add swp1 netns nxp1 type veth peer swp1 netns nxp2
  ip link add swp2 netns nxp2 type veth peer eth0 netns listener

  # Configure the test environment inside the namespace
  talker ip link set dev lo up
  listener ip link set dev lo up
  nxp1 ip link set dev lo up
  nxp2 ip link set dev lo up

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp0 up
  nxp1 ip link set dev swp1 up
  nxp1 ip link set dev swp2 up
  nxp2 ip link set dev swp0 up
  nxp2 ip link set dev swp1 up
  nxp2 ip link set dev swp2 up
  # Enable packet routing
  nxp1 sysctl -w net.ipv4.ip_forward=1
  nxp2 sysctl -w net.ipv4.ip_forward=1

  # Configure the addresses
  talker ip address add 10.0.0.1/24 dev eth0
  listener ip address add 10.0.0.2/24 dev eth0

  nxp1 ip address add 192.168.55.1/24 dev swp0
  nxp1 ip address add 192.168.66.1/24 dev swp1
  nxp2 ip address add 192.168.55.2/24 dev swp0
  nxp2 ip address add 192.168.66.2/24 dev swp1
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

/bin/bash --init-file <(echo "$ALIASES; PS1='(tsn over detnet) \u:\W# '")

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
  ip netns del nxp1 2>/dev/null
  ip netns del nxp2 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi

