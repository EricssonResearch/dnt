CNTFILE=/tmp/r2dtwo_test_env.count
SCENNAME="scenario_2R-2E"
NETNSES="A B C D admin talker listener"
function A() { ip netns exec A $@ ; }
function B() { ip netns exec B $@ ; }
function C() { ip netns exec C $@ ; }
function D() { ip netns exec D $@ ; }
function admin() { ip netns exec admin $@ ; }
function talker() { ip netns exec talker $@ ; }
function listener() { ip netns exec listener $@ ; }
export -f A
export -f B
export -f C
export -f D
export -f admin
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

function configure_tc() {
  # tc workaround for "Destination host unreachable"
  ip netns exec A ip link add pass2r2eth0 type veth peer name r2eth0
  ip netns exec A ip link set dev pass2r2eth0 up
  ip netns exec A ip link set dev r2eth0 up
  ip netns exec A tc qdisc add dev eth0 handle ffff: ingress;
  ip netns exec A tc filter add dev eth0 parent ffff: protocol ip flower src_ip 192.168.1.1 dst_ip 192.168.2.2 action mirred egress redirect dev pass2r2eth0
  ip netns exec D ip link add pass2r2eth0 type veth peer name r2eth0
  ip netns exec D ip link set dev pass2r2eth0 up
  ip netns exec D ip link set dev r2eth0 up
  ip netns exec D tc qdisc add dev eth0 handle ffff: ingress;
  ip netns exec D tc filter add dev eth0 parent ffff: protocol ip flower src_ip 192.168.2.2 dst_ip 192.168.1.1 action mirred egress redirect dev pass2r2eth0
}

export -f configure_tc

configure_networkenv() {
  echo "Initialize r2dtwo test environment"

  for item in $NETNSES; do
    ip netns add $item 2>/dev/null
    ip netns exec $item ip link set dev lo up
    ip netns exec $item sysctl -w net.ipv4.ip_forward=1
  done

  ip link add eth0 netns talker type veth peer eth0 netns A
  ip link add eth0 netns listener type veth peer eth0 netns D

  ip link add ab netns A type veth peer ba netns B
  ip link add ac netns A type veth peer ca netns C
  ip link add bc netns B type veth peer cb netns C
  ip link add b3 netns B type veth peer bd netns D
  ip link add db netns D type veth peer bd netns B
  ip link add dc netns D type veth peer cd netns C

  ip link add mgmt netns A type veth peer mgmt1 netns admin
  ip link add mgmt netns B type veth peer mgmt2 netns admin
  ip link add mgmt netns C type veth peer mgmt3 netns admin
  ip link add mgmt netns D type veth peer mgmt4 netns admin


  admin ip link add mgmt type bridge
  admin ip link set mgmt1 master mgmt
  admin ip link set mgmt2 master mgmt
  admin ip link set mgmt3 master mgmt
  admin ip link set mgmt4 master mgmt
  admin ip link set mgmt up
  admin ip link set mgmt1 up
  admin ip link set mgmt2 up
  admin ip link set mgmt3 up
  admin ip link set mgmt4 up


  configure_tc

  IFACES="A,ab;10.0.12.1 A,ac;10.0.13.1 A,eth0;192.168.1.2 A,mgmt;172.16.0.1
  B,ba;10.0.12.2 B,bc;10.0.23.2 B,bd;10.0.24.2 B,mgmt;172.16.0.2
  C,ca;10.0.13.3 C,cb;10.0.23.3 C,cd;10.0.35.3 C,mgmt;172.16.0.3
  D,db;10.0.24.4 D,dc;10.0.35.4 D,eth0;192.168.2.1 D,mgmt;172.16.0.4
  talker,eth0;192.168.1.1
  listener,eth0;192.168.2.2
  admin,mgmt;172.16.0.254
  "
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
  ip netns exec D ip route add default via 192.168.2.2

  ip netns exec talker ip link set eth0 address 00:00:00:01:01:01
  ip netns exec listener ip link set eth0 address 00:00:00:02:02:02

  ip netns exec talker ethtool -K eth0 tx off rx off
}

# Using flock to avoid race conditions
if [ -f "$CNTFILE" ]; then
  exec {CNTFD}<"$CNTFILE"
  flock -x -w 5 "$CNTFD"
  read scenname cntvalue < $CNTFILE
  if [ "$scenname" != "$SCENNAME" ] ; then
    echo "scenario '$scenname' is already running, stop it before starting $SCENNAME"
    return -2
  fi
  newvalue=`expr $cntvalue + 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
  exec {CNTFD}<&-
else
  echo "$SCENNAME 1" > $CNTFILE
  configure_networkenv
fi

/bin/bash --init-file <(echo "PS1='( 2R-2E ) \u:\W# '")

exec {CNTFD}<"$CNTFILE"
flock -x -w 5 "$CNTFD"
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
exec {CNTFD}<&-
