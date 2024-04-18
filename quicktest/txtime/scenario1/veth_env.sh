CNTFILE=/tmp/r2dtwo_test_envs_tsnodn.count
alias tx="ip netns exec talker"
alias lx="ip netns exec listener"

ALIASES='alias tx="ip netns exec talker"; alias lx="ip netns exec listener"'

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source veth_env.sh' as root"
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

  ip link add enp3s0 numtxqueues 4 netns talker type veth peer enp4s0 numtxqueues 4 netns listener

  # Configure the test environment inside the namespace
  tx ip link set dev lo up
  lx ip link set dev lo up

  tx ip link set enp3s0 up
  lx ip link set enp4s0 up

  # Configure the addresses
  tx ip address add 192.168.0.220/24 dev enp3s0
  lx ip address add 192.168.0.221/24 dev enp4s0

  lx ip link set enp4s0 address 00:00:00:02:02:02
  lx ip nei add 192.168.0.220 dev enp4s0 lladdr 00:00:00:01:01:01
  tx ip link set enp3s0 address 00:00:00:01:01:01
  tx ip nei add 192.168.0.221 dev enp3s0 lladdr 00:00:00:02:02:02

  #tx tc qdisc add dev enp3s0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
  #tx tc qdisc replace dev enp3s0 parent 100:2 etf clockid CLOCK_TAI delta 1000000000 skip_sock_check
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

/bin/bash --init-file <(echo "$ALIASES; PS1='(veth env) \u:\W# '")

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE
  #Cleanup the test namespace
  ip netns del talker 2>/dev/null
  ip netns del listener 2>/dev/null
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi
