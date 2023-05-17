# Scenario #2: R2DTWO TSN over DetNet

__Important: this scenario assumes background knowledge of the basics from Scenario #1. Please take a look into Scenario #1 if you have not already.__ [Scenario #1](../scenario1/README.md)

In the following, we will use R2DTWO as a DetNet router.
The Layer2 traffic of `talker` and `listener` nodes will be encapsulated in MPLS DetNet pseudowires, then sent through a PREF (Packet Replication and Elimination Functions).

We will use the following topology, which consists:

* a talker node called **talker** which will generate traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO instances, running on the **nxp1** and **nxp2** nodes.

```
    talker              nxp1                         nxp2              listener
┌──────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌──────────┐
│          │    │      192.168.55.1│         │192.168.55.2      │    │          │
│          │    │           swp0  ─┼─────────┼─  swp0           │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│    eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0    │
│ 10.0.0.1 │    │                  │         │                  │    │ 10.0.0.1 │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│          │    │           swp1  ─┼─────────┼─  swp1           │    │          │
│          │    │      192.168.66.1│         │192.168.66.2      │    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘

                      PRF ───►                     ───► PEF

                      PEF ◄───                     ◄─── PRF

                      R2DTWO                       R2DTWO
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will be utilized by R2DTWO for redundancy.
What is important here, unlike in the Layer2 TSN case (Scenario #1) now R2DTWO operates at Layer3 and the replicated packets will be routed to the DetNet peer.

In the scenario above however the routing is very simple, but the same configuration would work even when there are multiple routers between `nxp1` and `nxp2`.

## The R2DTWO configurations

Now we have separate R2DTWO config files for the two DetNet nodes: `nxp1.ini` and `nxp2.ini`.
This is required because even if the topology is symmetrical, we have different IP addresses on the two routers.


### Explanation of the configuration

For full details, please take a look at the R2DTWO documentation.
Right now we are only explaining the actions in the `nxp1.ini` file, but everything except the addresses apply to the `nxp2.ini` too obviously.
Like before in Scenario #1, this config also consists of three main sections: `[interfaces]`, `[objects]` and `[streams]`.

Take a look into the `[interfaces]` section, this is where this scenario differs mostly.

```
[interfaces]
uni = eth iface=swp2
uni:streams = stream_uni
nni1_in = udp-in iface=swp0 ipv=4
nni1_in:streams = stream_nni
nni2_in = udp-in iface=swp1 ipv=4
nni2_in:streams = stream_nni
nni1_out = udp-out iface=swp0 dstip=192.168.55.2
nni2_out = udp-out iface=swp1 dstip=192.168.66.2
```

As one can see there are separate interfaces defined for egress (`nni1_out` and `nni2_out`) and ingress (`nni1_in` and `nni2_in`) DetNet traffic.
Notice that on the egress interfaces there are no streams defined.
This makes sense since those interfaces cannot be used to receive traffic, they only send the encapsulated traffic to their counterparts.

The `[objects]` section is very similar to Scenario #1.
That is because the same semantics can be applied to sequence number generation, replication and elimination.
In DetNet terminology, we use __PRF__ to replication and __PEF__ to elimination.
We use the `prf` and `pef` names accordingly.

```
[objects]
prf = SeqGen InitSeqStart=0
pef = SeqRcvy
```

The `[streams]` section looks like the following:

```
[streams]
stream_uni:packet = eth, cvlan
stream_uni:match = cvlan vid=0
stream_uni:actions = prf, edit cvlan.vid=99, before eth add mpls, after mpls add dcw, writeseq dcw, replicate nni1_out nni2_out

nni1_out = edit mpls.label=100 mpls.bos=1, send nni1_out
nni2_out = edit mpls.label=200 mpls.bos=1, send nni2_out

stream_nni:packet = mpls, dcw, eth, cvlan
stream_nni:match = cvlan vid=99
stream_nni:actions = readseq dcw, pef, del dcw, del mpls, del cvlan, send uni
```

For the full list of the supported R2DTWO actions, their parameters and behavior please consult with the documentation.

Right now, the packets matching in `stream_uni` stream will be processed as described below as described in the `:actions` line:

0. The switch receives a packet on `swp2` interface, and since its an ethernet interface, R2DTWO applies a VLAN 0 tag (named as `cvaln` in the config) on it by default
1. The `prf` action gives a unique sequence number for each packet: `prf`
2. We change the VLAN ID to 99, so at the PEF side we can match to that regardless of the paths. This is an R2DTWO extra feature, normally one can define two NNI streams and match to the outer MPLS labels: `edit cvlan.vid=99`
3. Now we have to prepare the DetNet encapsulation. For that we will add an MPLS header `before eth add mpls` and a DetNet control word `after mpls add dcw`.
4. Optional, but might be useful to show: `writeseq dcw` will write the sequence number of the packet (given from `prf`) into the `dcw` header. R2DTWO is smart enough to do that if we push new DetNet CW header.
5. That was the common part of the pipeline. Now we perform a branching with the `replicate nni1_out nni2_out` action, and continue the execution of the pipeline differently with two copies of the packet.
6. We set the MPLS Label field to 100 and 200 values, and sending the replica packets on `nni1_out` and `nni2_out` interfaces. Before the sending we also set the MPLS Bottom of Stack bit to 1, since we dont do label stacking, that is the only MPLS label we have right now.

Now let's see the NNI part.
The NNIs currently UDP sockets for both ingress and egress: we will check it with `tshark` later.
Right now we only defined one NNI stream for purpose: regardless of the ingress NNI interfaces, we only match the VLAN ID 99, which is set at step 2. of the `stream_uni` action pipeline.

In the NNI `:packet` line we have to define the expected encapsulation, which is `mpls, dcw, eth, cvlan`.
__Important__: as you can see, the packet header definition does not contain the underlying network's encapsulation (Ethernet, IP, UDP).
That's because we are only interested in the traffic in the pseudowires.

The steps in `stream_nni`:

1. After the matching of VLAN ID 99 we will read the DetNet CW sequence number: `readseq dcw`. That is required for the PEF
2. Now we can do the elimination with the PEF, which is performed by the `pef` action. Notice that `pef` is defined in the `[objects]` section, but until their names are not ambiguous we can use an object name in the action pipeline and the action type implicitly guessed by the R2DTWO.
3. Now we decapsulate the headers until the Layer2 payload, as received by the UNI, since the `talker` will expect the same encapsulation that it used when sent the packet. So right now we will delete the MPLS, DetNet CW and VLAN headers: `del dcw, del mpls, del cvlan`.
4. Then in the last action of the pipeline, we send the decapsulated packet on the UNI for the `talker`: `send uni`.

As mentioned before, the `nxp2.ini` is very similar however, the UNI is connected to the `listener` in that case.


## Run the R2DTWO and generate traffic

Let's try out R2DTWO with this scenario.
For that we need at least three terminal window: one for generate traffic (`talker`) and two for run R2DTWO instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo su

source env.sh
```

If everything OK, the prompt should be changed to `(tsn over detnet) root:scenario2# ` which tells right now we are in the test network environment.
Now we should have all the networking (nodes, interfaces and IP addresses) configured and helper commands to execute commands on the nodes.
To run a command on a node (e.g. `talker` or `nxp1`, etc.) just prefix the command with its name:

```
talker ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eth0@if2         UP             10.0.0.1/24 fe80::f83d:8ff:fec6:527d/64 

nxp1 ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
swp2@if2         UP             fe80::a0e3:8bff:fe7b:860a/64 
swp0@if2         UP             192.168.55.1/24 fe80::e8aa:eff:fe0a:76f1/64 
swp1@if3         UP             192.168.66.1/24 fe80::c494:d1ff:fe1b:b38/64
```

Now we can start the R2DTWO instances on `nxp1` and `nxp2`:

```
# in one terminal:
nxp1 r2dtwo nxp1.ini

# in another terminal window:
nxp2 r2dtwo nxp2.ini
```

If everything OK, the `r2dtwo` instances are up and running.
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


## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide is sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__

