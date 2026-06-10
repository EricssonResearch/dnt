CNTFILE=/tmp/dnt_test_env.count
SCENNAME="scenario_dynip"
function server() { ip netns exec server $@ ; }
function mobile() { ip netns exec mobile $@ ; }
function gateway1() { ip netns exec gateway1 $@ ; }
function gateway2() { ip netns exec gateway2 $@ ; }
export -f server
export -f mobile
export -f gateway1
export -f gateway2

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
  ip netns add server 2>/dev/null
  ip netns add mobile 2>/dev/null
  ip netns add gateway1 2>/dev/null
  ip netns add gateway2 2>/dev/null

  ip link add swp0 netns gateway1 type veth peer eth0 netns server
  ip link add swp0 netns gateway2 type veth peer eth1 netns server
  ip link add swp1 netns gateway1 type veth peer wwan0 netns mobile
  ip link add swp1 netns gateway2 type veth peer wwan1 netns mobile

  ip link add vrf0 netns server type vrf table 254
  ip link add vrf0 netns mobile type vrf table 254

  # Configure the test environment inside the namespace
  server ip link set dev lo up
  mobile ip link set dev lo up
  gateway1 ip link set dev lo up
  gateway2 ip link set dev lo up

  server ip link set eth0 mtu 1600 up
  server ip link set eth1 mtu 1600 up
  mobile ip link set wwan0 mtu 1600 up
  mobile ip link set wwan1 mtu 1600 up
  gateway1 ip link set dev swp0 mtu 1600 up
  gateway1 ip link set dev swp1 mtu 1600 up
  gateway2 ip link set dev swp0 mtu 1600 up
  gateway2 ip link set dev swp1 mtu 1600 up
  server ip link set vrf0 mtu 1500 up
  mobile ip link set vrf0 mtu 1500 up
  server ethtool -K vrf0 tx off rx off
  mobile ethtool -K vrf0 tx off rx off

  # Configure the addresses
  server ip address add fd11::2/64 dev eth0
  server ip address add fd21::2/64 dev eth1
  gateway1 ip address add fd11::1/64 dev swp0
  gateway2 ip address add fd21::1/64 dev swp0

  gateway1 ip address add fd12::1/64 dev swp1
  gateway2 ip address add fd22::1/64 dev swp1
  #mobile ip address add fd12::2/64 dev wwan0
  #mobile ip address add fd22::2/64 dev wwan1

  server ip address add fd55::1/64 dev vrf0
  mobile ip address add fd66::1/64 dev vrf0

  # Enable IP forwarding
  gateway1 sysctl -w net.ipv4.ip_forward=1
  gateway1 sysctl -w net.ipv6.conf.all.forwarding=1
  gateway2 sysctl -w net.ipv4.ip_forward=1
  gateway2 sysctl -w net.ipv6.conf.all.forwarding=1

  # Configure routing
  server ip route add fd10::/12 via fd11::1 dev eth0
  server ip route add fd20::/12 via fd21::1 dev eth1
  #mobile ip route add fd10::/12 via fd12::1 dev wwan0
  #mobile ip route add fd20::/12 via fd22::1 dev wwan1

  server ip route add fd66::/64 dev vrf0
  mobile ip route add fd55::/64 dev vrf0

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

/bin/bash --init-file <(echo "PS1='(dynamic ip) \u:\W# '")

read scenname cntvalue < $CNTFILE
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup dnt test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del server 2>/dev/null
  ip netns del mobile 2>/dev/null
  ip netns del gateway1 2>/dev/null
  ip netns del gateway2 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
fi
