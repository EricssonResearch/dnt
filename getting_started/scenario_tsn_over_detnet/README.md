# Scenario 2: DNT TSN over DetNet

__Important: this scenario assumes background knowledge of the basics from Scenario TSN. Please take a look into Scenario TSN if you have not already.__ [Scenario TSN](../scenario_tsn/README.md)

In the following, we will use DNT as a DetNet router.
The Layer2 traffic of `talker` and `listener` nodes will be encapsulated in MPLS DetNet pseudowires, then sent through a PREF (Packet Replication and Elimination Functions).

We will use the following topology, which consists:

* a talker node called **talker** which will generate traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two DNT instances, running on the **nxp1** and **nxp2** nodes.

```
    talker              nxp1                         nxp2              listener
┌──────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌──────────┐
│          │    │      192.168.55.1│         │192.168.55.2      │    │          │
│          │    │           swp0  ─┼─────────┼─  swp0           │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│    eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0    │
│ 10.0.0.1 │    │                  │         │                  │    │ 10.0.0.2 │
│ fd10::1  │    │                  │         │                  │    │ fd10::2  │
│          │    │                  │         │                  │    │          │
│          │    │           swp1  ─┼─────────┼─  swp1           │    │          │
│          │    │      192.168.66.1│         │192.168.66.2      │    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘

                      PRF ───►                     ───► PEF

                      PEF ◄───                     ◄─── PRF

                      DNT                       DNT
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will be utilized by DNT for redundancy.
What is important here, unlike in the Layer2 TSN case (Scenario TSN) now DNT operates at Layer3 and the replicated packets will be routed to the DetNet peer.

In the scenario above however the routing is very simple, but the same configuration would work even when there are multiple routers between `nxp1` and `nxp2`.

## The DNT configurations

Now we have separate DNT config files for the two DetNet nodes: `nxp1.ini` and `nxp2.ini`.
This is required because even if the topology is symmetrical, we have different IP addresses on the two routers.


### Explanation of the configuration

For full details, please take a look at the DNT documentation.
Right now we are only explaining the actions in the `nxp1.ini` file, but everything except the addresses apply to the `nxp2.ini` too obviously.
Like before in Scenario TSN, this config also consists of three main sections: `[interfaces]`, `[objects]` and `[streams]`.

Take a look into the `[interfaces]` section, this is where this scenario differs mostly.

```
[interfaces]
uni = eth iface=swp2
uni:streams = prio-compound compound

nni1_in = udp-in iface=swp0 ipv=4
nni1_in:streams = member1 prio-members
nni2_in = udp-in iface=swp1 ipv=4
nni2_in:streams = member2 prio-members

nni1_out = udp-out iface=swp0 dstip=192.168.55.2
nni2_out = udp-out iface=swp1 dstip=192.168.66.2
```

As one can see there are separate interfaces defined for egress (`nni1_out` and `nni2_out`) and ingress (`nni1_in` and `nni2_in`) DetNet traffic.
Notice that on the egress interfaces there are no streams defined.
This makes sense since those interfaces cannot be used to receive traffic, they only send the encapsulated traffic to their counterparts.

We expect two streams on both UNI and NNI interfaces.
One stream matching for every layer 2 traffic (`cvlan vid=0`).
The other matches on priority tagged packets (VLAN `pcp=6`), which identified as a separate stream.


The `[objects]` section is very similar to Scenario TSN.
That is because the same semantics can be applied to sequence number generation, replication and elimination.
In DetNet terminology, we use __PRF__ to replication and __PEF__ to elimination.
We use the `prf` and `pef` names accordingly.
We also have a sequence generator object called `gen`.

```
[objects]
prf = Replicate
gen = SeqGen InitSeqStart=0
pef = SeqRcvy

genprio = SeqGen InitSeqStart=0
prfprio = Replicate
pefprio = SeqRcvy
```

We have two compound streams `compound` and `prio-compound` and good practice to use separate sequence number space for both.
Therefore we defined separate sequence generator, PRF and PEF objects to the `prio-compound` stream as well.

#### Stream configuration

The `[streams]` section looks like the following:

```
[streams]
compound:packet = eth, cvlan
compound:match = cvlan vid=0
compound:actions = gen, edit cvlan.vid=99, before eth add mpls, after mpls add dcw, writeseq dcw, prf prf-member1 prf-member2

prf-member1 = edit mpls.label=100 mpls.bos=1, send nni1_out
prf-member2 = edit mpls.label=200 mpls.bos=1, send nni2_out

member1:packet = mpls, dcw, eth, cvlan
member1:match = mpls label=100
member1:actions = readseq dcw, pef pef-compound

member2:packet = mpls, dcw, eth, cvlan
member2:match = mpls label=200
member2:actions = readseq dcw, pef pef-compound

pef-compound = del dcw, del mpls, del cvlan, send uni
```

For the full list of the supported DNT actions, their parameters and behavior please consult with the documentation.

Right now, the packets matching in `compound` stream will be processed as described below as described in the `:actions` line:

0. The switch receives a packet on `swp2` interface, and since its an ethernet interface, DNT applies a VLAN 0 tag (named as `cvlan` in the config) on it by default
1. The `gen` action gives a unique sequence number for each packet. DNT smart enough to guess the action type from the object type.
2. We change the VLAN ID to 99, while this is not necessary for the operation, in Wireshark we can confirm the packet belongs to the stream.
3. Now we have to prepare the DetNet encapsulation. For that we will add an MPLS header `before eth add mpls` and a DetNet control word `after mpls add dcw`.
4. Optional, but might be useful to show: `writeseq dcw` will write the sequence number of the packet (given from `gen`) into the `dcw` header. DNT is smart enough to do that if we push new DetNet CW header.
5. That was the common part of the pipeline. Now we perform a branching with the `prf prf-member1 prf-member2` action, and continue the execution of the pipeline differently with two copies of the packet.
6. We set the MPLS Label field to 100 and 200 values, and sending the replica packets on `nni1_out` and `nni2_out` interfaces.
Before the sending we also set the MPLS Bottom of Stack bit to 1, since we dont do label stacking, that is the only MPLS label we have right now.

Now let's see the NNI part, where we process the incoming packets from the member streams.
The NNIs currently UDP sockets for both ingress and egress: we will check it with `tshark` later.
We defined two NNI streams: `member1` and `member2`.
The `:match` statements of the NNI streams identify the packets based to the MPLS labels (100 and 200).
No other identification needed, the labels 

In the NNI `:packet` line we have to define the expected encapsulation, which is `mpls, dcw, eth, cvlan`.
__Important__: as you can see, the packet header definition does not contain the underlying network's encapsulation (Ethernet, IP, UDP).
That's because we are only interested in the traffic in the pseudowires.

The steps in `member1` and `member2`:

1. After the matching of VLAN ID 99 we will read the DetNet CW sequence number: `readseq dcw`. That is required for the PEF
2. Now we can do the elimination with the PEF, which is performed by the `pef` action.
Notice that `pef` is defined in the `[objects]` section, but until their names are not ambiguous we can use an object name in the action pipeline and the action type implicitly guessed by the DNT.
3. The PEF drop the replica packets and the one passing packet's processing continues on the `pef-compound` action pipeline.
4. Now we decapsulate the headers until the Layer2 payload, as received by the UNI, since the `talker` will expect the same encapsulation that it used when sent the packet.
So right now we will delete the MPLS, DetNet CW and VLAN headers: `del dcw, del mpls, del cvlan`.
5. Then in the last action of the pipeline, we send the decapsulated packet on the UNI for the `talker`: `send uni`.


Another stream `prio-compound` defined similarly to `compound`.
It has its own stream definitions for UNI and NNI.
DNT tries to match the streams sequentially: the `compound` not matched if `prio-compound` matches on the packet.
It might worth to mention, there are no separate member streams defined, since we identify the stream with the same label (500) on both paths.
So `prio-members` stream is enough for the identification, it must be configured on both NNI interfaces.

As mentioned before, the `nxp2.ini` is very similar however, the UNI is connected to the `listener` in that case.


## Run the DNT and generate traffic

Let's try out DNT with this scenario.
For that we need at least three terminal window: one for generate traffic (`talker`) and two for run DNT instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo -s

source env.sh
```

If everything OK, the prompt should be changed to `(tsn over detnet) root:scenario_tsn_over_detnet# ` which tells right now we are in the test network environment.
Now we should have all the networking (nodes, interfaces and IP addresses) configured and helper commands to execute commands on the nodes.
To run a command on a node (e.g. `talker` or `nxp1`, etc.) just prefix the command with its name:

```
talker ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eth0@if2         UP             10.0.0.1/24 fd10::1/64 fe80::f83d:8ff:fec6:527d/64 

nxp1 ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
swp2@if2         UP             fe80::a0e3:8bff:fe7b:860a/64 
swp0@if2         UP             192.168.55.1/24 fe80::e8aa:eff:fe0a:76f1/64 
swp1@if3         UP             192.168.66.1/24 fe80::c494:d1ff:fe1b:b38/64
```

Now we can start the DNT instances on `nxp1` and `nxp2`:

```
# in one terminal:
nxp1 dnt nxp1.ini

# in another terminal window:
nxp2 dnt nxp2.ini
```

If everything OK, the `dnt` instances are up and running.
But we have to generate some traffic right now with `ping` so run it on the `talker` node:

```
talker ping -c 4 10.0.0.2
PING 10.0.0.2 (10.0.0.2) from 10.0.0.1 teth0: 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=0.319 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=0.170 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=0.201 ms
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=0.198 ms

--- 10.0.0.2 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3050ms
rtt min/avg/max/mdev = 0.170/0.222/0.319/0.057 ms
```

```
talker ping -c 4 fd10::2
PING fd10::2 (fd10::2) 56 data bytes
64 bytes from fd10::2: icmp_seq=1 ttl=64 time=0.151 ms
64 bytes from fd10::2: icmp_seq=2 ttl=64 time=0.177 ms
64 bytes from fd10::2: icmp_seq=3 ttl=64 time=0.176 ms
64 bytes from fd10::2: icmp_seq=4 ttl=64 time=0.166 ms

--- fd10::2 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3052ms
rtt min/avg/max/mdev = 0.151/0.167/0.177/0.010 ms
```

To observe the VLAN and R-tags, start `tshark` packet tracer on some of the __NNI__ interfaces (`swp0` or `swp1` either on `nxp1` or `nxp2`).
For that open a new root terminal, source the environment file (`source env.sh`) and run the following:

```
Running as user "root" and group "root". This could be dangerous.
Frame 1: 152 bytes on wire (1216 bits), 152 bytes captured (1216 bits) on interface swp0, id 0
    Section number: 1
    Interface id: 0 (swp0)
        Interface name: swp0
    Encapsulation type: Ethernet (1)
    Arrival Time: May  5, 2023 13:22:01.658158820 CEST
    [Time shift for this packet: 0.000000000 seconds]
    Epoch Time: 1683285721.658158820 seconds
    [Time delta from previous captured frame: 0.000000000 seconds]
    [Time delta from previous displayed frame: 0.000000000 seconds]
    [Time since reference or first frame: 0.000000000 seconds]
    Frame Number: 1
    Frame Length: 152 bytes (1216 bits)
    Capture Length: 152 bytes (1216 bits)
    [Frame is marked: False]
    [Frame is ignored: False]
    [Protocols in frame: eth:ethertype:ip:udp:mpls:pwethheuristic:pwethcw:eth:ethertype:vlan:ethertype:ip:icmp:data]
Ethernet II, Src: ea:aa:0e:0a:76:f1 (ea:aa:0e:0a:76:f1), Dst: e2:d3:a8:95:be:9c (e2:d3:a8:95:be:9c)
    Destination: e2:d3:a8:95:be:9c (e2:d3:a8:95:be:9c)
        Address: e2:d3:a8:95:be:9c (e2:d3:a8:95:be:9c)
        .... ..1. .... .... .... .... = LG bit: Locally administered address (this is NOT the factory default)
        .... ...0 .... .... .... .... = IG bit: Individual address (unicast)
    Source: ea:aa:0e:0a:76:f1 (ea:aa:0e:0a:76:f1)
        Address: ea:aa:0e:0a:76:f1 (ea:aa:0e:0a:76:f1)
        .... ..1. .... .... .... .... = LG bit: Locally administered address (this is NOT the factory default)
        .... ...0 .... .... .... .... = IG bit: Individual address (unicast)
    Type: IPv4 (0x0800)
Internet Protocol Version 4, Src: 192.168.55.1, Dst: 192.168.55.2
    0100 .... = Version: 4
    .... 0101 = Header Length: 20 bytes (5)
    Differentiated Services Field: 0x00 (DSCP: CS0, ECN: Not-ECT)
        0000 00.. = Differentiated Services Codepoint: Default (0)
        .... ..00 = Explicit Congestion Notification: Not ECN-Capable Transport (0)
    Total Length: 138
    Identification: 0x7938 (31032)
    010. .... = Flags: 0x2, Don't fragment
        0... .... = Reserved bit: Not set
        .1.. .... = Don't fragment: Set
        ..0. .... = More fragments: Not set
    ...0 0000 0000 0000 = Fragment Offset: 0
    Time to Live: 64
    Protocol: UDP (17)
    Header Checksum: 0xd1d6 [validation disabled]
    [Header checksum status: Unverified]
    Source Address: 192.168.55.1
    Destination Address: 192.168.55.2
User Datagram Protocol, Src Port: 44985, Dst Port: 6635
    Source Port: 44985
    Destination Port: 6635
    Length: 118
    Checksum: 0xefdb [unverified]
    [Checksum Status: Unverified]
    [Stream index: 0]
    [Timestamps]
        [Time since first frame: 0.000000000 seconds]
        [Time since previous frame: 0.000000000 seconds]
    UDP payload (110 bytes)
MultiProtocol Label Switching Header, Label: 100, Exp: 0, S: 1, TTL: 0
    0000 0000 0000 0110 0100 .... .... .... = MPLS Label: 100 (0x00064)
    .... .... .... .... .... 000. .... .... = MPLS Experimental Bits: 0
    .... .... .... .... .... ...1 .... .... = MPLS Bottom Of Label Stack: 1
    .... .... .... .... .... .... 0000 0000 = MPLS TTL: 0
PW Ethernet Control Word
    Sequence Number: 2064
Ethernet II, Src: fa:3d:08:c6:52:7d (fa:3d:08:c6:52:7d), Dst: 1e:55:3d:b1:9c:91 (1e:55:3d:b1:9c:91)
    Destination: 1e:55:3d:b1:9c:91 (1e:55:3d:b1:9c:91)
        Address: 1e:55:3d:b1:9c:91 (1e:55:3d:b1:9c:91)
        .... ..1. .... .... .... .... = LG bit: Locally administered address (this is NOT the factory default)
        .... ...0 .... .... .... .... = IG bit: Individual address (unicast)
    Source: fa:3d:08:c6:52:7d (fa:3d:08:c6:52:7d)
        Address: fa:3d:08:c6:52:7d (fa:3d:08:c6:52:7d)
        .... ..1. .... .... .... .... = LG bit: Locally administered address (this is NOT the factory default)
        .... ...0 .... .... .... .... = IG bit: Individual address (unicast)
    Type: 802.1Q Virtual LAN (0x8100)
802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 99
    000. .... .... .... = Priority: Best Effort (default) (0)
    ...0 .... .... .... = DEI: Ineligible
    .... 0000 0110 0011 = ID: 99
    Type: IPv4 (0x0800)
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
    0100 .... = Version: 4
    .... 0101 = Header Length: 20 bytes (5)
    Differentiated Services Field: 0x00 (DSCP: CS0, ECN: Not-ECT)
        0000 00.. = Differentiated Services Codepoint: Default (0)
        .... ..00 = Explicit Congestion Notification: Not ECN-Capable Transport (0)
    Total Length: 84
    Identification: 0xcb6c (52076)
    010. .... = Flags: 0x2, Don't fragment
        0... .... = Reserved bit: Not set
        .1.. .... = Don't fragment: Set
        ..0. .... = More fragments: Not set
    ...0 0000 0000 0000 = Fragment Offset: 0
    Time to Live: 64
    Protocol: ICMP (1)
    Header Checksum: 0x5b3a [validation disabled]
    [Header checksum status: Unverified]
    Source Address: 10.0.0.1
    Destination Address: 10.0.0.2
Internet Control Message Protocol
    Type: 8 (Echo (ping) request)
    Code: 0
    Checksum: 0x3bc9 [correct]
    [Checksum Status: Good]
    Identifier (BE): 15976 (0x3e68)
    Identifier (LE): 26686 (0x683e)
    Sequence Number (BE): 1958 (0x07a6)
    Sequence Number (LE): 42503 (0xa607)
    Timestamp from icmp data: May  5, 2023 13:22:01.000000000 CEST
    [Timestamp from icmp data (relative): 0.658158820 seconds]
    Data (48 bytes)

0000  7f 0a 0a 00 00 00 00 00 10 11 12 13 14 15 16 17   ................
0010  18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27   ........ !"#$%&'
0020  28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37   ()*+,-./01234567
        Data: 7f0a0a0000000000101112131415161718191a1b1c1d1e1f202122232425262728292a2b…
        [Length: 48]
```

This is a very very verbose output, showing all of the fields decoded from every protocol.

If we clean up the not relevant bits, a little bit, and show a shorter summary for every protocol header except the DetNet Control Word, use the following command:

```
nxp1 tshark -Q -l -O pwethcw -i swp0

Running as user "root" and group "root". This could be dangerous.
Frame 1: 152 bytes on wire (1216 bits), 152 bytes captured (1216 bits) on interface swp0, id 0
Ethernet II, Src: ea:aa:0e:0a:76:f1 (ea:aa:0e:0a:76:f1), Dst: e2:d3:a8:95:be:9c (e2:d3:a8:95:be:9c)
Internet Protocol Version 4, Src: 192.168.55.1, Dst: 192.168.55.2
User Datagram Protocol, Src Port: 45344, Dst Port: 6635
MultiProtocol Label Switching Header, Label: 100, Exp: 0, S: 1, TTL: 0
PW Ethernet Control Word
    Sequence Number: 4
Ethernet II, Src: fa:3d:08:c6:52:7d (fa:3d:08:c6:52:7d), Dst: 1e:55:3d:b1:9c:91 (1e:55:3d:b1:9c:91)
802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 99
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
Internet Control Message Protocol

```

As one can see, we have the router IPv4 addresses in the outermost IP header, then UDP with the standard `6635` destination port.

After that, we have the DetNet encapsulation: MPLS and DetNet CW, in this packet for example the sequence number is 4.
Inside that, we have the talker's traffic tunneled: Ethernet, CVLAN, IPv4 and ICMP (ping).

For better visibility, please use Wireshark.

Since DNT 6.3 there is a `PACKETTRACE` module, which give a brief overview about the packets processed by DNT.
To enable that, start DNT with the module enabled on max verbosity level.
For example we use it on `nxp1`:

```
nxp1 dnt nxp1.ini -v PACKETTRACE:DEBUG
Info: Logging to standard output.
2024.08.23 09:57:17 [MAIN] [INFO] DNT - Dependable Networking Toolkit 6.3
2024.08.23 09:57:17 [MAIN] [INFO] Reading config 'nxp1.ini'
2024.08.23 09:57:17 [STATE] [INFO] adding stream member1 to interface nni1_in
2024.08.23 09:57:17 [STATE] [INFO]   compiling new pipeline
2024.08.23 09:57:17 [STATE] [INFO] adding stream member2 to interface nni2_in
2024.08.23 09:57:17 [STATE] [INFO]   compiling new pipeline
2024.08.23 09:57:17 [STATE] [INFO] adding stream compound to interface uni
2024.08.23 09:57:17 [STATE] [INFO]   compiling new pipeline
2024.08.23 09:57:17 [INTERFACE] [INFO] Udp-out interface nni1_out on device swp0 destination 192.168.55.2 port 6635
2024.08.23 09:57:17 [INTERFACE] [INFO] Udp-out interface nni2_out on device swp1 destination 192.168.66.2 port 6635
2024.08.23 09:57:17 [INTERFACE] [INFO] Udp-in interface nni2_in on device swp1 ip 192.168.66.1 port 6635
2024.08.23 09:57:17 [INTERFACE] [INFO] Udp-in interface nni1_in on device swp0 ip 192.168.55.1 port 6635
2024.08.23 09:57:17 [INTERFACE] [INFO] iface address monitoring nni1_in ifname swp0 ifindex 3
2024.08.23 09:57:17 [INTERFACE] [INFO] iface address monitoring nni2_in ifname swp1 ifindex 4
2024.08.23 09:57:17 [INTERFACE] [INFO] Eth interface uni on device swp2
2024.08.23 10:03:36 [PACKETTRACE] [PACKET] [id=1 oid=0] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 10:03:36 [PACKETTRACE] [PACKET] [id=0 oid=0] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out 
2024.08.23 10:03:36 [PACKETTRACE] [PACKET] [id=2 oid=2] nni1_in 110 member1 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (0 pass) FilterOAM Del Del Edit Del Send uni 
2024.08.23 10:03:36 [PACKETTRACE] [PACKET] [id=3 oid=3] nni2_in 110 member2 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (0 drop) 
2024.08.23 10:03:37 [PACKETTRACE] [PACKET] [id=5 oid=4] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 10:03:37 [PACKETTRACE] [PACKET] [id=4 oid=4] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out 
2024.08.23 10:03:37 [PACKETTRACE] [PACKET] [id=6 oid=6] nni1_in 110 member1 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (1 pass) FilterOAM Del Del Edit Del Send uni 
2024.08.23 10:03:37 [PACKETTRACE] [PACKET] [id=7 oid=7] nni2_in 110 member2 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (1 drop) 
2024.08.23 10:03:38 [PACKETTRACE] [PACKET] [id=9 oid=8] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 10:03:38 [PACKETTRACE] [PACKET] [id=8 oid=8] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out 
2024.08.23 10:03:38 [PACKETTRACE] [PACKET] [id=10 oid=10] nni1_in 110 member1 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (2 pass) FilterOAM Del Del Edit Del Send uni 
2024.08.23 10:03:38 [PACKETTRACE] [PACKET] [id=11 oid=11] nni2_in 110 member2 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (2 drop) 
2024.08.23 10:03:39 [PACKETTRACE] [PACKET] [id=13 oid=12] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 10:03:39 [PACKETTRACE] [PACKET] [id=12 oid=12] uni 102 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out 
2024.08.23 10:03:39 [PACKETTRACE] [PACKET] [id=14 oid=14] nni1_in 110 member1 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (3 pass) FilterOAM Del Del Edit Del Send uni 
2024.08.23 10:03:39 [PACKETTRACE] [PACKET] [id=15 oid=15] nni2_in 110 member2 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (3 drop) 
2024.08.23 10:03:41 [PACKETTRACE] [PACKET] [id=17 oid=16] uni 46 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 10:03:41 [PACKETTRACE] [PACKET] [id=16 oid=16] uni 46 compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out 
2024.08.23 10:03:41 [PACKETTRACE] [PACKET] [id=18 oid=18] nni1_in 54 member1 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (4 pass) FilterOAM Del Del Edit Del Send uni 
2024.08.23 10:03:41 [PACKETTRACE] [PACKET] [id=19 oid=19] nni2_in 54 member2 |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (4 drop) 
```

As one can see, this print one line per packet, summarizing the processing of it.
It shows an internal ID of the packet, ingress interface, size, the matching stream (if any), header stack and the executed actions, finally the egress interface if send action exists.
One can examine the decision of the `Eliminate` action if it drop or pass the packet (also the sequence number of the packet).
__Important to note__: the output can be differs in future versions of DNT. Also, DNT can insert implicit actions, generated automatically. These actions not defined in the config file.

To generate traffic matching on the `prio-compound` stream, there is a small python script prepared.
The script generate one packet with VLAN ID 0 and PCP 6.
After starting DNTs execute the script:

```
talker ./traffic.py
```

If DNT packet trace logging enabled, the output could look like the following:

```
# on nxp1
...
2024.08.23 13:32:39 [PACKETTRACE] [PACKET] [id=1 oid=0] uni 54 prio-compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni1_out 
2024.08.23 13:32:39 [PACKETTRACE] [PACKET] [id=0 oid=0] uni 54 prio-compound |eth|cvlan|payload| SeqGen Edit Add Add WriteSeq WriteSeq Replicate Edit Send nni2_out

# on nxp2
...
2024.08.23 13:32:39 [PACKETTRACE] [PACKET] [id=0 oid=0] nni1_in 62 prio-members |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (0 pass) FilterOAM Del Del Edit Send uni 
2024.08.23 13:32:39 [PACKETTRACE] [PACKET] [id=1 oid=1] nni2_in 62 prio-members |mpls|dcw|eth|cvlan|payload| TTLReduce ReadSeq Eliminate (0 drop)
```

As visible in the output, the packet from the UNI matches to the `prio-compound` stream and replicated with MPLS label 500.
At `nxp2` both packet identified by the same `prio-compound` stream, one passed and one eliminated.


## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide is sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__

