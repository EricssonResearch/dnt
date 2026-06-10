# Scenario 10: DNT for Segment Routing over IPv6

__Important: this assumes background knowledge of the basic from the doc/srv6. Please take a look into the SRv6 documentation if you have not already.__ [SRv6](../../doc/srv6.md)

This scenario demonstrates the SRv6 based DetNet operation. It supports 3 subcases: IPv6 over SRv6, IPv4 over SRv6 and TSN (Ethernet) over SRv6.
Although the same script sets up the environment for all subcases, for better understanding the different subcases have different DNT configurations


We will use the following topology, which consists:

* a talker node called **T1** which will generate IPv6/IPv4/TSN traffic
* a node called **L5** which receive the traffic coming from the **T1**
* three routers (**R2**, **R3**, **R4**) in a topology providing 2 separate paths
* two DNT instances, running on the **R2** and **R4** nodes.

he network topology is the same for all 3 scenarios. For IPv6/IPv4 scenario the T1 and L5 have IPv6/IPv4 addresses, and for the TSN scenario a VLAN is used. VLAN 10 is used to send TSN traffic.
To differentiate the TSN/DetNet traffic from background, IP DSCP 6 (TOS 0xC0) is used. For TSN traffic, the DSCP 6 traffic is automatically mapped to Ethernet PCP 6 when sent out.

```
                                                   ┌─────────────┐                                                   
                                                   │fd13:fade::0 │                                                   
                                                   └─────────────┘                                                   
                                                   ┏━━━━━━━━━━━━━┓                                                   
                                                   ┃             ┃                                                   
                                    ┌──────────────┨     R3      ┠─────────────┐                                     
                                    │  fd02:a1fa::3┃             ┃fd04:a1fa::3 │                                     
                                    │              ┗━━━━━━━━━━━━━┛             │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                        fd02:a1fa::2│                                          │fd04:a1fa::4                         
┏━━━━━━━━━━━━━┓              ┏━━━━━━┷━━━━━━┓                            ┏━━━━━━┷━━━━━━┓               ┏━━━━━━━━━━━━━┓
┃             ┃              ┃             ┃                            ┃             ┃               ┃             ┃
┃     T1      ┠──────────────┨     R2      ┠────────────────────────────┨     R4      ┠───────────────┨     L5      ┃
┃             ┃  fd01:a1fa::2┃             ┃fd03:a1fa::2    fd03:a1fa::4┃             ┃fd05:a1fa::4   ┃             ┃
┗━━━━━━━━━━━━━┛    10.0.1.2  ┗━━━━━━━━━━━━━┛                            ┗━━━━━━━━━━━━━┛  10.0.5.2     ┗━━━━━━━━━━━━━┛
 fd01:a1fa::1                ┌─────────────┐                            ┌─────────────┐                 fd05:a1fa::5
 10.0.1.1                    │fd12:fade::0 │                            │fd14:fade::0 │                     10.0.5.1
 Vlan 10: 10.10.0.1          └─────────────┘                            └─────────────┘           Vlan 10: 10.10.0.2

```



We use 2 SID blocks for DetNet
In our example we use several IP address ranges:
* fd0N:a1fa::M/64  infrastructure IPv6 addresses, where M is the originating node ID, and N is the destination node ID
* fd1N:fade::0/64  SID addresses used for Linux SRv6 tunnels, where N is the router node ID
* fd00:a2d2:0:000N/64    PREOF.SID IPv6 addresses block, where N is the destination node ID

## SRv6 operation intro

The SRv6 support builds on the existing Linux SRv6 functionality. Therefore, first we configure Linux SRv6 tunnels between nodes **R2** and **R4**.
To set up these tunnels, the `fade` SID addresses used. Although the Linux SRv6 implementation supports automatic SRv6 tunnel termination, we can not use it because counters will not be created for the tunnel. Thus, we use explicit SRv6 termination.

DNT instance at **R2** will encapsulate the incoming packet (which can be IPv6, IPv4 or Ethernet/TSN), and sets the destination PREOF SID. Since the packet will be replicated over 2 different tunnels, in the DNT config, the `func` field of the PREOF SID is used to direct the traffic to different SRv6 tunnels: for a given DetNet flow the `locator` is the same, but the PREOF SID `func` value varies to select different outgoing tunnels. The `func` value of 1 is selecting the direct tunnel to **R4**, while the value of 2 selects the alternate path through **R3**. The encapsulated packets then will be sent on the `vrf1` interface, and it will be routed to an SRv6 TE-Tunnel. Thus, on the `vrf1` interface we can see both replicas of the encapsulated packet, with PREOF SID as the destination address.

At the egress node, incoming packets will be directed to an End.DT6 termination, which decapsulates the SRv6 headers. The decapsulated IPv6 packet containing the PREOF SID will be routed to the DNT. The DNT performs identification, elimination, and decapsulates the original packet to send out on the UNI.

To set up an unidirectional tunnel from **R2** to **R4**, we need the following commands:

```
R2 # ip -6 route add fd00:a2d2:0:4:1::/80 encap seg6 mode encap segs fd14:fade:0:0:1:: dev eth_r2r4
R2 # ip -6 route add fd00:a2d2:0:4:2::/80 encap seg6 mode encap segs fd13:fade::0,fd14:fade:0:0:1:: dev eth_r2r3

R4 # ip -6 route add fd14:fade:0:0:1::/128 encap seg6local action End.DT6 count table 254 dev ve1
```
Note that the `count` parameter must be added to te End.DT6 command, to get SRv6 tunnel statistics.
We can see the decapsulated SRv6 packets at the **R4** `ve1` interface (tcpdump does not see the encapsulated packets at node **R2** `vrf1` interface):

```
R4 # tcpdump -ni ve1
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on ve1, link-type EN10MB (Ethernet), snapshot length 262144 bytes
09:57:06.266360 IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:1:1111:1000:13: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 36913, seq 9, length 64
09:57:06.266421 IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:2:1222:2000:13: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 36913, seq 9, length 64
09:57:07.290412 IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:1:1111:1000:14: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 36913, seq 10, length 64
09:57:07.290471 IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:2:1222:2000:14: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 36913, seq 10, length 64
```

As we can see, the source address is fd00:a2d2::1:0:2, which is the local IP address of the **R2** `ve1` interface. The destination is the DetNet SID, for example fd00:a2d2:0:4:1:1111:1000:13, where the `locator` is fd00:a2d2:0:4, the `func` is 1 and 2, the `flowid` is 1111 and 2222, the sequence number is the last (13, 14).  
Each sequence number can be seen twice, the DNT will do the elimination.


We can also see the SRv6 TE traffic as well, on the **R2** eth_r2r4 (and eth_r2r3) interface. Here, we can see the SRv6 tunnel header, the DetNet SID, and the internal ping6 headers:

```
# tcpdump -ni eth_r2r4
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on eth_r2r4, link-type EN10MB (Ethernet), snapshot length 262144 bytes
10:11:33.298196 IP6 fd03:a1fa::2 > fd14:fade:0:0:1::: RT6 (len=2, type=4, segleft=0, last-entry=0, tag=0, [0]fd14:fade:0:0:1::) IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:1:1111:1000:3b: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 64505, seq 1, length 64
10:11:33.298519 IP6 fd03:a1fa::4 > fd12:fade:0:0:1::: RT6 (len=2, type=4, segleft=0, last-entry=0, tag=0, [0]fd12:fade:0:0:1::) IP6 fd00:a2d2::1:0:4 > fd00:a2d2:0:2:1:1555:5000:3b: IP6 fd05:a1fa::5 > fd01:a1fa::1: ICMP6, echo reply, id 64505, seq 1, length 64
```

Here we can see only one copy of the packets, as the other copy travels a different path. We can also see the reverse direction traffic (the ping6 reply) as well.
On the interface `eth_r2r3` we can see the other replicas of the traffic, where the source routed path contains 2 addresses: [0]fd14:fade:0:0:1::, [1]fd13:fade::

```
# tcpdump -ni eth_r2r3
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on eth_r2r3, link-type EN10MB (Ethernet), snapshot length 262144 bytes
10:17:28.927727 IP6 fd02:a1fa::2 > fd13:fade::: RT6 (len=4, type=4, segleft=1, last-entry=1, tag=0, [0]fd14:fade:0:0:1::, [1]fd13:fade::) IP6 fd00:a2d2::1:0:2 > fd00:a2d2:0:4:2:1222:2000:3f: IP6 fd01:a1fa::1 > fd05:a1fa::5: ICMP6, echo request, id 12240, seq 1, length 64
10:17:28.927965 IP6 fd04:a1fa::4 > fd12:fade:0:0:1::: RT6 (len=4, type=4, segleft=0, last-entry=1, tag=0, [0]fd12:fade:0:0:1::, [1]fd13:fade::) IP6 fd00:a2d2::1:0:4 > fd00:a2d2:0:2:2:1666:6000:3f: IP6 fd05:a1fa::5 > fd01:a1fa::1: ICMP6, echo reply, id 12240, seq 1, length 64
```


## Running the srv6.py script

The SRv6 scenario can be started by the `srv6.py` python script, which sets up a mininet scenario.
There are 3 test cases differentiated:
* IPv6 UNI - the incoming traffic is IPv6
* IPv4 UNI - the incoming traffic is IPv4
* TSN UNI - the incoming traffic is TSN

To start a given scenario, the following command can be used:


```
$ sudo python3 srv6.py <scenario name>
```

The script sets up the network, and runs the test cases sequentially. If everything went well, we get a similar output:

```
$ sudo python3 srv6.py
*** Adding hosts
*** Creating links
*** Configuring hosts
t1 r2 r3 r4 l5
*** Adding IPv6 addresses
*** Setting up SRv6 tunnels
*** Starting network
*** Starting controller

*** Starting 0 switches

*** Waiting for switches to connect

DNT SRv6 debug
*** Starting DNTs, scenario ipv6
*** Starting CLI:
mininet>

```

After running the debug scenario, a mininet prompt is presented and the traffic can be generated manually.
To test the scenario, ping command can be used. Depending on the scenario, different traffic is generated.

To test a scenario with ping, the following commands can be used:

For IPv6:

background traffic:
mininet> t1 ping6 fd05:a1fa::5
IPv6 high priority traffic:
mininet> t1 ping6 fd05:a1fa::5 -Q 0xc0

For IPv4:

background traffic:
mininet> t1 ping 10.0.5.1

for high priority traffic:
mininet> t1 ping  -Q 0xc0 10.0.5.1

For TSN:
background traffic:
mininet> t1 ping 10.10.0.2

for high priority traffic:
mininet> t1 ping  -Q 0xc0 10.10.0.2


To check the traffic at different locations in the network, first we need to start either Wireshark or start a terminal on a router node.
Running Wireshark on r2:

mininet> r2 wireshark &

Within Wireshark, we can monitor any interface of the r2 node. Also, opening a terminal on a router we can run `tcpdump` or `tshark` to check the traffic or check the SRv6 counters.

To check the SRv6 counters, we use the the `ip -6 -s route show dev ve1` command to display the statistics. For example:

```
$ ip -6 -s route show dev ve1
fd00:a2d2::/64 proto kernel metric 256 pref medium
fd14:fade:0:0:1::  encap seg6local action End.DT6 table 254 packets 20 bytes 4320 errors 0 metric 1024 pref medium
fe80::/64 proto kernel metric 256 pref medium
```


## Cleanup

The DNT logfiles will not be removed after running. These files need to be removed by the user, when they are not needed any more.
