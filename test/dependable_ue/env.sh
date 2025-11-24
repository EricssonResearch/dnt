CNTFILE=/tmp/r2dtwo_test_env.count
SCENNAME="scenario_dependable_ue"
function talker() { ip netns exec talker $@ ; }
function listener() { ip netns exec listener $@ ; }
function nxp1() { ip netns exec nxp1 $@ ; }
function nxp2() { ip netns exec nxp2 $@ ; }
function oam() { ip netns exec oam $@ ; }
export -f talker
export -f listener
export -f nxp1
export -f nxp2
export -f oam

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

  nxp1 ip link add r2eth0 type veth peer name r2eth1
  nxp1 ip link set dev r2eth0 up
  nxp1 ip link set dev r2eth1 up
  # by default no ingress filtering
  nxp1 tc qdisc add dev swp0 handle ffff: ingress
  # redirect IP egress traffic to R2DTWO
  nxp1 tc filter add dev swp0 parent ffff: protocol ip flower src_ip 10.0.100.11 dst_ip 10.0.200.22 action mirred egress redirect dev r2eth0
  nxp1 tc filter add dev swp0 parent ffff: protocol ipv6 flower src_ip 2001::11 dst_ip 2002::22 action mirred egress redirect dev r2eth0
  # redirect R2DTWO UNI traffic back to the node (talker) if no suitable UNI interface with IP address
  # nxp1 tc qdisc add dev r2eth0 handle 0: root prio
  # nxp1 tc filter add dev r2eth0 parent 0: matchall action mirred egress redirect dev swp2

  nxp2 ip link add r2eth0 type veth peer name r2eth1
  nxp2 ip link set dev r2eth0 up
  nxp2 ip link set dev r2eth1 up
  nxp2 tc qdisc add dev swp0 handle ffff: ingress
  # redirect IP egress traffic to R2DTWO
  nxp2 tc filter add dev swp0 parent ffff: protocol ip flower src_ip 10.0.200.22 dst_ip 10.0.100.11 action mirred egress redirect dev r2eth0
  nxp2 tc filter add dev swp0 parent ffff: protocol ipv6 flower src_ip 2002::22 dst_ip 2001::11 action mirred egress redirect dev r2eth0
  # direct R2DTWO UNI traffic back to the node (listener) if no suitable UNI interface with IP address
  # nxp2 tc qdisc add dev r2eth0 handle 0: root prio
  # nxp2 tc filter add dev r2eth0 parent 0: matchall action mirred egress redirect dev swp2
}

export -f configure_tc

configure_networkenv() {
  echo "Initialize r2dtwo test environment"
  # Create the test namespace
  ip netns add talker 2>/dev/null
  ip netns add listener 2>/dev/null
  ip netns add nxp1 2>/dev/null
  ip netns add nxp2 2>/dev/null
  ip netns add oam 2>/dev/null

  ip link add swp0 netns nxp1 type veth peer eth0 netns talker
  ip link add swp1 netns nxp1 type veth peer swp1 netns nxp2
  ip link add swp2 netns nxp1 type veth peer swp2 netns nxp2
  ip link add swp3 netns nxp1 type veth peer swp3 netns nxp2
  ip link add swp0 netns nxp2 type veth peer eth0 netns listener

  ip link add eno0 netns nxp1 type veth peer eth0 netns oam        # for OAM
  ip link add eno0 netns nxp2 type veth peer eth1 netns oam        # for OAM

  # Configure the test environment inside the namespace
  talker ip link set dev lo up
  listener ip link set dev lo up
  nxp1 ip link set dev lo up
  #nxp1 ip address add 192.168.1.1 dev lo
  nxp2 ip link set dev lo up
  #nxp2 ip address add 192.168.2.1 dev lo

  talker ip link set eth0 up
  listener ip link set eth0 up
  nxp1 ip link set dev swp1 mtu 1600 up      # bigger MTU for NNI interfaces
  nxp1 ip link set dev swp2 mtu 1600 up
  nxp1 ip link set dev swp3 mtu 1600 up
  nxp1 ip link set dev eno0 mtu 1600 up
  nxp1 ip link set dev swp0 up
  nxp2 ip link set dev swp1 mtu 1600 up
  nxp2 ip link set dev swp2 mtu 1600 up
  nxp2 ip link set dev swp3 mtu 1600 up
  nxp2 ip link set dev eno0 mtu 1600 up
  nxp2 ip link set dev swp0 up

  oam ip link add name br0 type bridge
  oam ip link set dev eth0 master br0
  oam ip link set dev eth1 master br0
  oam ip addr add 192.168.111.3/24 dev br0
  oam ip link set dev eth0 up
  oam ip link set dev eth1 up
  oam ip link set dev br0 up

  # disable path MTU discovery - done per socket, so not needed
  #nxp1 sysctl -w net.ipv4.ip_no_pmtu_disc=1
  #nxp2 sysctl -w net.ipv4.ip_no_pmtu_disc=1

  # Configure the addresses, IPv4 and IPv6
  talker ip address add 10.0.100.11/24 dev eth0
  talker ip address add 2001::11/64 dev eth0

  listener ip address add 10.0.200.22/24 dev eth0
  listener ip address add 2002::22/64 dev eth0

  nxp1 ip address add 192.168.55.1/24 dev swp1
  nxp1 ip address add 192.168.66.1/24 dev swp2
  nxp1 ip address add 192.168.77.1/24 dev swp3
  nxp1 ip address add 10.0.100.1/24 dev swp0
  nxp1 ip address add fc0a::1/64 dev swp1
  nxp1 ip address add fc0b::1/64 dev swp2
  nxp1 ip address add fc0c::1/64 dev swp3
  nxp1 ip address add 2001::1/64 dev swp0

  nxp2 ip address add 192.168.55.2/24 dev swp1
  nxp2 ip address add 192.168.66.2/24 dev swp2
  nxp2 ip address add 192.168.77.2/24 dev swp3
  nxp2 ip address add 10.0.200.1/24 dev swp0
  nxp2 ip address add fc0a::2/64 dev swp1
  nxp2 ip address add fc0b::2/64 dev swp2
  nxp2 ip address add fc0c::2/64 dev swp3
  nxp2 ip address add 2002::2/64 dev swp0

  nxp1 ip address add 192.168.111.1/24 dev eno0
  nxp2 ip address add 192.168.111.2/24 dev eno0


  # Enable IP forwarding
  nxp1 sysctl -w net.ipv4.ip_forward=1
  nxp1 sysctl -w net.ipv6.conf.all.forwarding=1
  nxp2 sysctl -w net.ipv4.ip_forward=1
  nxp2 sysctl -w net.ipv6.conf.all.forwarding=1

  # Configure routing
  talker ip route add default via 10.0.100.1
  talker ip -6 route add default via 2001::1
  listener ip route add default via 10.0.200.1
  listener ip -6 route add default via 2002::2
}

start_r2dtwos() {
    # For debug, spawns r2dtw windows in gdb
    #nxp1 xterm -T nxp1 -e env -i gdb -nx --args ../../r2dtwo nxp1.ini -v ALL:ALL &
    #nxp2 xterm -T nxp2 -e env -i gdb -nx --args ../../r2dtwo nxp2.ini -v ALL:ALL &

    #nxp1 ../../r2dtwo -of nxp1.ini -v ALL:ALL &
    #nxp1 ../../r2dtwo -of nxp1.ini -v PACKETTRACE:PACKET &
    nxp1 ../../r2dtwo nxp1.ini -v ALL:NONE &

    #nxp2 ../../r2dtwo -of nxp2.ini -v ALL:ALL &
    #nxp2 ../../r2dtwo -of nxp2.ini -v PACKETTRACE:PACKET &
    nxp2 ../../r2dtwo nxp2.ini -v ALL:NONE  &
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
  start_r2dtwos
fi

# Usage: send_telnet_command <host> <port> "command_string" <namespace>
send_telnet_command() {
    # Function arguments are now host, port, command, and namespace
    local host="$1"
    local port="$2"
    local cmd="$3"
    local namespace="$4"

    # Use process substitution to feed input to the command run inside the namespace
    (
        sleep 0.5  # Wait for connection establishment
        echo "$cmd"
        sleep 0.5  # Wait for server response
        echo "quit"
    ) | ip netns exec "$namespace" telnet "$host" "$port" 2>&1 | \
        tr -d '\r' | \
        sed 's/\x1B\[[0-9;]*[JKmsu]//g' | \
        grep -v "Escape character is" | \
        grep -v "Connection closed by foreign host." | \
        grep -v "^Trying " | \
        grep -v "^Exiting." | \
        grep -v "Connected to " | \
        grep -v "^OAM 'conn" | \
        grep -v "$cmd" # Filter out the echoed command itself
}
export -f send_telnet_command

TELNET_RESPONSE=$(send_telnet_command "127.0.0.1" "8000" "notif_pull enable" "nxp1")
echo "nxp1: '$TELNET_RESPONSE'"
TELNET_RESPONSE=$(send_telnet_command "127.0.0.1" "8000" "notif_pull enable" "nxp2")
echo "nxp2: '$TELNET_RESPONSE'"

# start some background traffic
talker ping -i .2 10.0.200.22 > /dev/null 2 &

# Start xterm in background and record its PID
nxp1 xterm -T "TelnetControl" -hold -e ./telnet_control &
#nxp1 "xterm -T 'Telnet Control' -hold -e ./telnet_control &"

oam xterm -T "JsonReceiver" -hold -e python3 ../../json_receiver/multipart_json_udp_receiver.py 192.168.111.3 9000  &

# --- Table of loss/delay values (5 lines) ---
# Format: d1 l1 d2 l2 d3 l3
table=(
  "10 0  0 0  0 0"
  "20 0  0 0  0 0"
  "40 0 10 0  0 0"
  "50 0 20 0 10 0"
  "40 0 40 0 20 0"
  "20 0 50 0 40 0"
  "10 0 40 0 50 0"
  " 0 0 20 0 40 0"
  " 0 0 10 0 20 0"
  " 0 0  0 0 10 0"  
)

#function called by trap
brk=0
trap_handler() {
    echo "Exited the loop !"
    brk=1
}
trap trap_handler SIGINT SIGTERM

x=0
rows=${#table[@]}

echo "Starting traffic loop"

nxp1 tc qdisc add dev swp1 root netem loss 0% delay 0ms
nxp1 tc qdisc add dev swp2 root netem loss 0% delay 0ms
nxp1 tc qdisc add dev swp3 root netem loss 0% delay 0ms

# --- Main loop ---
while [ $brk -eq 0 ]; do
    # pick line from table
    idx=$(( x % rows ))
    read X1 Y1 X2 Y2 X3 Y3 <<< "${table[$idx]}"

    echo "Cycle $x (row $((idx+1))):"
    echo "  swp1: loss $X1% delay ${Y1}ms"
    echo "  swp2: loss $X2% delay ${Y2}ms"
    echo "  swp3: loss $X3% delay ${Y3}ms"

    # modify netem parameters
    nxp1 tc qdisc change dev swp1 root netem loss ${X1}% delay ${Y1}ms
    nxp1 tc qdisc change dev swp2 root netem loss ${X2}% delay ${Y2}ms
    nxp1 tc qdisc change dev swp3 root netem loss ${X3}% delay ${Y3}ms

    x=$((x+1))
    # wait N second, interruptible by Ctrl-C
    read -t 8 dummy
done

echo "Loop done."
# delete old qdiscs
nxp1 tc qdisc del dev swp1 root netem 2>/dev/null
nxp1 tc qdisc del dev swp2 root netem 2>/dev/null
nxp1 tc qdisc del dev swp3 root netem 2>/dev/null

#kill the ping to avoid interference
talker pkill -9 ping #2>/dev/null

/bin/bash --init-file <(echo "PS1='(ip over detnet) \u:\W# '")

read scenname cntvalue < $CNTFILE
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE

  sudo killall r2dtwo

  echo "Closing telnet_control window..."
  nxp1 pkill -9 xterm #2>/dev/null

  #Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
  ip netns del nxp1 2>/dev/null
  ip netns del nxp2 2>/dev/null
  ip netns del oam 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo "$SCENNAME $newvalue" > $CNTFILE
fi
