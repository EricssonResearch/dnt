#!/bin/bash

sysctl -w net.ipv4.ip_forward=1
sysctl -w net.ipv6.conf.all.forwarding=1

ip addr add 192.168.55.1/24 dev swp0
ip addr add 192.168.66.1/24 dev swp1
ip address add 10.0.100.1/24 dev swp2

ip address add fc0a::1/64 dev swp0
ip address add fc0b::1/64 dev swp1
ip address add 2001::1/64 dev swp2

ip link add r2veth0 type veth peer name r2veth1
ip link set r2veth0 mtu 2000 up
ip link set r2veth1 mtu 2000 up

#ip route add blackhole 10.0.200.0/24
#ip route add blackhole 2002::/64

function configure_tc() {
  tc qdisc add dev swp2 handle ffff: ingress
  tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.100.11 dst_ip 10.0.200.22 action mirred egress redirect dev r2veth0
  tc filter add dev swp2 parent ffff: protocol ipv6 flower src_ip 2001::11 dst_ip 2002::22 action mirred egress redirect dev r2eth0
}

export -f configure_tc
