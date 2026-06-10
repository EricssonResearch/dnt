CNTFILE=/tmp/dnt_test_env.count
SCENNAME="scenario_tsn"
alias talker="ip netns exec talker"
alias listener="ip netns exec listener"
alias nxp1="ip netns exec nxp1"
alias nxp2="ip netns exec nxp2"

ALIASES='alias talker="ip netns exec talker"; alias listener="ip netns exec listener"; alias nxp1="ip netns exec nxp1"; alias nxp2="ip netns exec nxp2"'

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source env.sh' as root"
  return -1
fi

if which dnt > /dev/null ; then true ; else
  echo "dnt executable not found."
  echo "Compile and install dnt first."
  return -2
fi

configure_networkenv() {
  echo "Initialize dnt test environment"
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

  talker ethtool -K eth0 rxvlan off txvlan off tx off rx off
  listener ethtool -K eth0 rxvlan off txvlan off tx off rx off

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp0 up
  nxp1 ip link set dev swp1 up
  nxp1 ip link set dev swp2 up
  nxp2 ip link set dev swp0 up
  nxp2 ip link set dev swp1 up
  nxp2 ip link set dev swp2 up

  listener sysctl -w net.ipv4.conf.eth0.accept_local=1

  # Configure the addresses
  talker ip address add 10.0.0.1/24 dev eth0
  listener ip address add 10.0.0.2/24 dev eth0
  talker ip address add fd10::1/64 dev eth0
  listener ip address add fd10::2/64 dev eth0
}

# This is totally unsafe
# TODO: use flock to avoid races
if [ -f "$CNTFILE" ]; then
  read scenname cntvalue < $CNTFILE
  if [ "$scenname" != "$SCENNAME" ] ; then
    echo "scenario '$scenname' is already running, stop it before starting $SCENNAME"
    return -2
  fi
  newvalue=`expr $cntvalue + 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
else
  echo "$SCENNAME 1" > $CNTFILE
  configure_networkenv
fi

/bin/bash --init-file <(echo "$ALIASES; PS1='(tsn test) \u:\W# '")

read scenname cntvalue < $CNTFILE
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup dnt test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
  ip netns del nxp1 2>/dev/null
  ip netns del nxp2 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
fi
