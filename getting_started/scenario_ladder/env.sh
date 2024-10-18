CNTFILE=/tmp/r2dtwo_test_env.count
SCENNAME="scenario_ladder"
NETNSES="A B C D E F talker listener"
function A() { ip netns exec A $@ ; }
function B() { ip netns exec B $@ ; }
function C() { ip netns exec C $@ ; }
function D() { ip netns exec D $@ ; }
function E() { ip netns exec E $@ ; }
function F() { ip netns exec F $@ ; }
function talker() { ip netns exec talker $@ ; }
function listener() { ip netns exec listener $@ ; }
export -f A
export -f B
export -f C
export -f D
export -f E
export -f F
export -f talker
export -f listener

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source env.sh' as root"
  return -1
fi

if which r2dtwo > /dev/null ; then true ; else
  echo "r2dtwo executable not found."
  echo "Compile and install r2dtwo first."
  return -2
fi

configure_networkenv() {
  echo "Initialize r2dtwo test environment"

  for item in $NETNSES; do
    ip netns add $item 2>/dev/null
    ip netns exec $item ip link set dev lo up
    ip netns exec $item sysctl -w net.ipv4.ip_forward=1
  done

  ip link add eth0 netns talker type veth peer eth0 netns A
  ip link add eth0 netns listener type veth peer eth0 netns F

  ip link add a1 netns A type veth peer b1 netns B
  ip link add a2 netns A type veth peer c1 netns C
  ip link add b2 netns B type veth peer c2 netns C
  ip link add b3 netns B type veth peer d1 netns D
  ip link add c3 netns C type veth peer e1 netns E
  ip link add d2 netns D type veth peer e2 netns E
  ip link add d3 netns D type veth peer f1 netns F
  ip link add e3 netns E type veth peer f2 netns F

  IFACES="A,a1;10.0.12.1 A,a2;10.0.13.1 A,eth0;192.168.1.2
  B,b1;10.0.12.2 B,b2;10.0.23.2 B,b3;10.0.24.2
  C,c1;10.0.13.3 C,c2;10.0.23.3 C,c3;10.0.35.3
  D,d1;10.0.24.4 D,d2;10.0.45.4 D,d3;10.0.46.4
  E,e1;10.0.35.5 E,e2;10.0.45.5 E,e3;10.0.56.5
  F,f1;10.0.46.6 F,f2;10.0.56.6 F,eth0;192.168.2.1
  talker,eth0;192.168.1.1
  listener,eth0;192.168.2.2"
  for i in $IFACES; do
    ns=${i%,*}
    rest=${i#*,}
    iface=${rest%;*}
    ip=${rest#*;}
    ip netns exec $ns ip link set dev $iface mtu 1600 up
    ip netns exec $ns ip address add $ip/24 dev $iface
  done

  ip netns exec talker ip route add default via 192.168.1.2
  ip netns exec listener ip route add default via 192.168.2.1
  ip netns exec F ip route add default via 192.168.2.2

  ip netns exec talker ip link set eth0 address 00:00:00:01:01:01
  ip netns exec listener ip link set eth0 address 00:00:00:02:02:02

  ip netns exec talker ethtool -K eth0 tx off rx off
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

/bin/bash --init-file <(echo "PS1='(ladder redundancy) \u:\W# '")

read scenname cntvalue < $CNTFILE
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  for item in $NETNSES; do
    ip netns del $item 2>/dev/null
  done
else
  newvalue=`expr $cntvalue - 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
fi
