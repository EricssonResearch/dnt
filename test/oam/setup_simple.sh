#!/bin/bash
#
# TSN
#                                    s1
#                             ┌──────────────┐
#                             │              │
#                  ┌──────────┴────┐     ┌───▼───────────┐
# TSN in  ─────────►  ep2   ep1    │     │   ep3   ep4   │
#                  └─────────▲─────┘     └───────────┬───┘
#                            │                       │
#                            └───────────────────────┘
#                                     s2

ip link add ep1 type veth peer name ep2
ip link add ep3 type veth peer name ep4
ifconfig ep4 hw ether 00:fe:fe:ba:ba:01
arp -i ep3 -s 10.10.4.30 00:fe:fe:ba:ba:01

sudo ethtool -K ep1 tx off
sudo ethtool -K ep2 tx off

ip addr add 10.10.1.20 dev ep1
ip addr add 10.10.3.30 dev ep3
ip addr add 10.10.4.30 dev ep4

ip link add link ep2 name ep2.100 type vlan id 100

ip link set dev ep1 up
ip link set dev ep2.100 up
ip link set dev ep2 up
ip link set dev ep3 up
ip link set dev ep4 up
