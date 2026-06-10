CNTFILE=/tmp/dnt_test_env.count
SCENNAME="scenario_ip_over_detnet"
function talker() { ip netns exec talker $@ ; }
function listener() { ip netns exec listener $@ ; }
function nxp1() { ip netns exec nxp1 $@ ; }
function nxp2() { ip netns exec nxp2 $@ ; }
export -f talker
export -f listener
export -f nxp1
export -f nxp2

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source env.sh' as root"
  return -1
fi

if which dnt > /dev/null ; then true ; else
  echo "dnt executable not found."
  echo "Compile and install dnt first."
  return -2
fi

function configure_tc() {

  nxp1 ip link add dnteth0 type veth peer name dnteth1
  nxp1 ip link set dev dnteth0 up
  nxp1 ip link set dev dnteth1 up
  # by default no ingress filtering
  nxp1 tc qdisc add dev swp2 handle ffff: ingress
  # redirect IP egress traffic to DNT
  nxp1 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.100.11 dst_ip 10.0.200.22 action mirred egress redirect dev dnteth0
  nxp1 tc filter add dev swp2 parent ffff: protocol ipv6 flower src_ip fd01::11 dst_ip fd02::22 action mirred egress redirect dev dnteth0
  # redirect DNT UNI traffic back to the node (talker) if no suitable UNI interface with IP address
  # nxp1 tc qdisc add dev dnteth0 handle 0: root prio
  # nxp1 tc filter add dev dnteth0 parent 0: matchall action mirred egress redirect dev swp2

  nxp2 ip link add dnteth0 type veth peer name dnteth1
  nxp2 ip link set dev dnteth0 up
  nxp2 ip link set dev dnteth1 up
  nxp2 tc qdisc add dev swp2 handle ffff: ingress
  # redirect IP egress traffic to DNT
  nxp2 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.200.22 dst_ip 10.0.100.11 action mirred egress redirect dev dnteth0
  nxp2 tc filter add dev swp2 parent ffff: protocol ipv6 flower src_ip fd02::22 dst_ip fd01::11 action mirred egress redirect dev dnteth0
  # direct DNT UNI traffic back to the node (listener) if no suitable UNI interface with IP address
  # nxp2 tc qdisc add dev dnteth0 handle 0: root prio
  # nxp2 tc filter add dev dnteth0 parent 0: matchall action mirred egress redirect dev swp2
}

export -f configure_tc

function configure_blackhole() {
  nxp1 ip route add blackhole 10.0.200.0/24
  nxp1 ip route add blackhole fd02::/64
  nxp2 ip route add blackhole 10.0.100.0/24
  nxp2 ip route add blackhole fd01::/64
}

export -f configure_blackhole

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

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp0 mtu 1600 up      # bigger MTU for NNI interfaces
  nxp1 ip link set dev swp1 mtu 1600 up
  nxp1 ip link set dev swp2 up
  nxp2 ip link set dev swp0 mtu 1600 up
  nxp2 ip link set dev swp1 mtu 1600 up
  nxp2 ip link set dev swp2 up

  # disable path MTU discovery - done per socket, so not needed
  #nxp1 sysctl -w net.ipv4.ip_no_pmtu_disc=1
  #nxp2 sysctl -w net.ipv4.ip_no_pmtu_disc=1

  # Configure the addresses, IPv4 and IPv6
  talker ip address add 10.0.100.11/24 dev eth0
  talker ip address add fd01::11/64 dev eth0

  listener ip address add 10.0.200.22/24 dev eth0
  listener ip address add fd02::22/64 dev eth0

  nxp1 ip address add 192.168.55.1/24 dev swp0
  nxp1 ip address add 192.168.66.1/24 dev swp1
  nxp1 ip address add 10.0.100.1/24 dev swp2
  nxp1 ip address add fd0a::1/64 dev swp0
  nxp1 ip address add fd0b::1/64 dev swp1
  nxp1 ip address add fd01::1/64 dev swp2

  nxp2 ip address add 192.168.55.2/24 dev swp0
  nxp2 ip address add 192.168.66.2/24 dev swp1
  nxp2 ip address add 10.0.200.1/24 dev swp2
  nxp2 ip address add fd0a::2/64 dev swp0
  nxp2 ip address add fd0b::2/64 dev swp1
  nxp2 ip address add fd02::2/64 dev swp2

  # Enable IP forwarding
  nxp1 sysctl -w net.ipv4.ip_forward=1
  nxp1 sysctl -w net.ipv6.conf.all.forwarding=1
  nxp2 sysctl -w net.ipv4.ip_forward=1
  nxp2 sysctl -w net.ipv6.conf.all.forwarding=1

  # Configure routing
  talker ip route add default via 10.0.100.1
  talker ip -6 route add default via fd01::1
  listener ip route add default via 10.0.200.1
  listener ip -6 route add default via fd02::2
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

/bin/bash --init-file <(echo "PS1='(ip over detnet) \u:\W# '")

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
