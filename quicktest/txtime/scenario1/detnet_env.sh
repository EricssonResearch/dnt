CNTFILE=/tmp/r2dtwo_test_envs_tsnodn.count
alias tx="ip netns exec talker"
alias lx="ip netns exec listener"
alias nsx="ip netns exec r2dtwoenv"

ALIASES='alias tx="ip netns exec talker"; alias lx="ip netns exec listener"; alias nsx="ip netns exec r2dtwoenv"'

if [ $(id -u) -ne 0 ]; then
  echo "Usage: run 'source detnet_env.sh' as root"
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
  ip netns add talker
  ip netns add listener
  ip netns add r2dtwoenv

  NETNSES="talker listener r2dtwoenv"
  for item in $NETNSES; do
     ip netns exec $item sysctl -w net.ipv6.conf.all.disable_ipv6=1
     ip netns exec $item ip link set dev lo up
  done

  ip link add teth0 netns talker type veth peer name aeth0 netns r2dtwoenv
  ip link add leth0 netns listener type veth peer name beth0 netns r2dtwoenv

  IFNAMES_ALL="enp3s0 enp4s0 enp6s0 enp7s0"
  for item in $IFNAMES_ALL; do
    ip link set $item netns r2dtwoenv
    # nsx sh -c "echo 1 > /sys/class/net/$item/threaded"
    nsx ip link set dev $item promisc on
    nsx ethtool -K $item gro on
    nsx ip link set dev $item up
    nsx ip link set dev $item mtu 1500
    nsx ethtool -s $item autoneg on speed 2500 duplex full
    nsx ethtool -K $item rxvlan off txvlan off
    nsx ethtool -K $item rx off tx off
  done

  IFNAMES="aeth0 beth0"
  for item in $IFNAMES; do
    nsx ip link set dev $item mtu 1500
    nsx ip link set dev $item up
    nsx ethtool -K $item gro on
    nsx ethtool -K $item rxvlan off txvlan off
    nsx ethtool -K $item rx off tx off
  done

  tx ip link set dev teth0 up
  tx ethtool -K teth0 gro on
  tx ethtool -K teth0 rxvlan off txvlan off tx off rx off
  
  lx ip link set dev leth0 up
  lx ethtool -K leth0 gro on
  lx ethtool -K leth0 rxvlan off txvlan off tx off rx off

  # Add VLAN interface to test encap
  tx ip link add link teth0 name teth0.10 type vlan id 10
  tx ip link set teth0.10 address 00:00:00:01:01:01
  tx ip nei add 10.0.0.2 dev teth0.10 lladdr 00:00:00:02:02:02
  tx ip link set dev teth0.10 up
  tx ip link set dev teth0 mtu 1490

  lx ip link add link leth0 name leth0.10 type vlan id 10
  lx ip link set leth0.10 address 00:00:00:02:02:02
  lx ip nei add 10.0.0.1 dev leth0.10 lladdr 00:00:00:01:01:01
  lx ip link set dev leth0.10 up
  lx ip link set dev leth0 mtu 1490

  tx ip addr add 10.0.0.1/24 dev teth0.10
  lx ip addr add 10.0.0.2/24 dev leth0.10

  # Configure ETF qdisc
  nsx tc qdisc add dev enp3s0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
  nsx tc qdisc replace dev enp3s0 parent 100:2 etf clockid CLOCK_TAI delta 300000 skip_sock_check offload
  nsx tc qdisc add dev enp6s0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
  nsx tc qdisc replace dev enp6s0 parent 100:2 etf clockid CLOCK_TAI delta 300000 skip_sock_check offload
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

/bin/bash --init-file <(echo "$ALIASES; PS1='(detnet env) \u:\W# '")

cntvalue=`cat $CNTFILE`
if [ $cntvalue -eq 1 ]; then #last bash instance in the env, do cleanup
  echo "Cleanup r2dtwo test environment"
  rm $CNTFILE

  nsx ip link set enp4s0 netns 1
  nsx ip link set enp3s0 netns 1
  ip netns del listener 2>/dev/null
  ip netns del talker 2>/dev/null
  ip netns del r2dtwo 2>/dev/null
  rmmod igc
  modprobe igc
else
  newvalue=`expr $cntvalue - 1`
  echo $newvalue > $CNTFILE
fi
