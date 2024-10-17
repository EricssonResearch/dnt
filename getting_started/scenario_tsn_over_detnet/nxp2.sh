#!/bin/bash
#
~/setup_if.sh
sysctl -w net.ipv4.ip_forward=1

ip addr add 192.168.55.2/24 dev swp0
ip addr add 192.168.66.2/24 dev swp1

ip link add veth0-ovs0 type veth peer name veth0-r2dtwo
ip link set veth0-ovs0 mtu 2000 up
ip link set veth0-r2dtwo mtu 2000 up

ovs-vsctl --if-exists del-br br0
ovs-vsctl --if-exists del-br ovs0

ovs-vsctl add-br ovs0
ovs-vsctl add-port ovs0 swp2
ovs-vsctl add-port ovs0 veth0-ovs0
ip link set ovs0 mtu 2000 up
ip addr add 10.0.0.22/24 dev ovs0
ovs-vsctl set bridge ovs0 protocols=OpenFlow10,OpenFlow11,OpenFlow12,OpenFlow13
ovs-ofctl -O OpenFlow13 add-flow ovs0 in_port=1,dl_type=0x800,nw_src=10.0.0.2,nw_dst=10.0.0.1,actions=output=2
