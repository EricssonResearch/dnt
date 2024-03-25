#!/bin/bash
#
sysctl -w net.ipv4.ip_forward=1

ip addr add 192.168.55.2/24 dev swp0
ip addr add 192.168.66.2/24 dev swp1

ip route add 10.0.100.0/24 via 192.168.55.1 dev swp0
ip route add 10.0.100.0/24 via 192.168.66.1 dev swp1 metric 5

ip link add r2veth0 type veth peer name r2veth1
ip link set r2veth0 mtu 2000 up
ip link set r2veth1 mtu 2000 up

ovs-vsctl --if-exists del-br br0
ovs-vsctl --if-exists del-br r2br0
ovs-vsctl --if-exists del-br ovs0

ip addr add 10.0.200.1/24 dev swp2
tc qdisc add dev swp2 handle ffff: ingress;
tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.200.22 dst_ip 10.0.100.11 action mirred egress redirect dev r2veth0
