
# SRv6 support

SRv6 support makes possible the use of SRv6 as transport for DetNet tunnels.
The R2DTWO support for SRv6 relies on the Linux SRv6 functions. As currently the Linux SRv6 implementation does not support DetNet SID as defined in [Deterministic Networking specific SID](https://datatracker.ietf.org/doc/draft-varga-spring-preof-sid/ "DetNet SID") and  [Deterministic Networking SRv6 Data Plane](https://datatracker.ietf.org/doc/draft-varga-detnet-srv6-data-plane/ "SRv6 data plane"), we use an outer IPv6 encapsulation which uses a PREOF SID as destination address. This encapsulation is done by the R2DTWO. This IPv6 encapsulated packet will be directed into a predefined Linux SRv6 TE-Tunnel.
```
┌─────────────────────────────────────┐                                  
│ Original packet (IPv6, IPv4 or TSN) │                                  
└─────────────────────────────────────┘                                  

┌────────────────────────────┬─────────────────────────────────────┐                                  
│IPv6 header, dest=PREOF.SID │ Original packet (IPv6, IPv4 or TSN) │    R2DTWO: IPv6 encap with PREOF.SID                        
└────────────────────────────┴─────────────────────────────────────┘                                  

┌──────────────────────┬────────────────────────────┬─────────────────────────────────────┐                                
│IPv6 header + SRH     │IPv6 header, dest=PREOF.SID │ Original packet (IPv6, IPv4 or TSN) │    Linux: SRv6 TE-Tunnel        
└──────────────────────┴────────────────────────────┴─────────────────────────────────────┘                                 
```
To support the PREOF SID structure, the following header fields have been added to the IPv6 header:

* `loc`, as a 64 bit Locator
* `func`, as a 16 bit Functon
* `argument`, consisting of
    * `flowid` as a 20 bit Flow Id
    * `seq`, as a 28 bit DetNet sequence field.

The DetNet sequence field is similar to the TSN or DetNet sequence field, but it's only 28 bits.
```
┌──────────────┬──────────────┬────────────────────────────────────────┐                       
│ 4 bit flags  │8 bit reserved│           16 bit sequence number       │                       
└──────────────┴──────────────┴────────────────────────────────────────┘                       
```
At the UNI interface, the incoming traffic will be identified and directed to R2DTWO. R2DTWO will encapsulate the incoming packet (which can be IPv6, IPv4 or Ethernet/TSN), and sets the destination PREOF SID. The encapsulated packet will be routed to an SRv6 TE-Tunnel created by Linux using H.Encaps functionality, which encapsulates again the packet within a new IPv6 header containing the SRH.
At the egress node, incoming packets will be directed to an End.DT6 termination, which decapsulates the SRv6 headers. The decapsulated IPv6 packet containing the PREOF SID will be routed to the R2DTWO. The R2DTWO performs identification, elimination, and decapsulates the original packet to send out on the UNI.

## Linux configuration for SRv6

In our example we use several IP address ranges:
* a1fa: infrastructure IPv6 addresses
* fade: SID addresses used for Linux SRv6 tunnels
* fd00:a2d2:             PREOF.SID IPv6 addresses block
* fd00:a2d2:0000::/80    reserved IPv6 address range, used for R2DTWO
  * fd00:a2d2:0:0:0:1::1/96 internal address for the vrf interface, also used as source for inner IPv6 source
  * fd00:a2d2:0:0:0:2::2/96 internal address for the ve1 interface. This will be used as gateway for the PREOF.SID prefix range.
  * fd00:a2d2:0:0:0:3::3/96 internal address for the ve2 interface. This is only needed for R2DTWO IP interface.

From the fd00:a2d2: SID range we reserve the first block fd00:a2d2:0000::/80 as reserved IPv6 range for internal use on vrf and veth interfaces.

Linux and R2DTWO interaction in the context of SRv6 requires several internal interfaces:
* First of all, all NNI interfaces need IPv6 addresses to work. For this, we use IP addresses in the `a1fa` range. Also, the routing should also use these addresses.
* All nodes should also have a loopback interface, in our case `sr0`, which should have an address from the `fade` range. Since the default loopback interface does not work with SRv6 processing, we use `dummy` interface for loopback.
* At the UNI interface, a TC filter should intercept the DetNet/TSN traffic and direct it to the vet1/2 veth interface pair. This is needed because the UNI interface holds the IP address, and it answers any ARP/NDP requests. (Note that this is not needed for Ethernet/TSN traffic, as explained later.)
* R2DTWO uses a vrf interface to send IP packets to NNI interfaces. The choice for VRF type interface was that it behaves as an IP interface, not requiring ARP/NDP support. SRv6 routing entries will be used to direct traffic from the VRF interface towards the tunnels. The VRF interface must also have an IPv6 address, from range `fd00:a2d2:0:0:0:1::0/96`. This has local meaning, a routing entry will use it to route the /64 PREOF SID range through this interface to SRv6 tunnels (H.Encap).
* R2DTWO uses an additional `veth` interface pair (`ve1` and `ve2`) to receive the decapsulated IP packets. Interface `ve1` is configured with an address from range `fd00:a2d2:0:0:0:2::0/96`. To match the PREOF SIDs a /64 prefix network route is added to the routing table with a next hop in the `fd00:a2d2:0:0:0:2::0/96` range, for example `fd00:a2d2:0:0:0:2::99`. Therefore all traffic exiting the End.DT6 function will be routed through the `ve1` interface. Since `ve1` is an Ethernet type interface, a static neighbor entry is needed for the next hop `fd00:a2d2:0:0:0:2::99`. This is needed to resolve the MAC address of the next hop, and also helps to avoid local L2 loops as this MAC address is not a local address. At the same time, the R2DTWO listens on the `ve2` interface at IP level. The R2DTWO `ip` interface type requires that `ve2` has an IP address. To fulfill this, `ve2` is equipped with an address from `fd00:a2d2:0:0:0:3::0/96` range. (This IP is not used, so any local IP will do here.)


```
        ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                        ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓      
        ┃                             R2DTWO ┃                        ┃ R2DTWO                                ┃      
        ┃     R/E/O/D   H.Encap IPv6 outer   ┃                        ┃   End.Decap IPv6 outer    R/E/O/D     ┃      
        ┃       ┌───┐  ┌─────┐               ┃                        ┃                  ┌─────┐   ┌───┐      ┃      
        ┃       │   ├──▶█████│─┐             ┃                        ┃                ┌─▶█████│───▶   │      ┃      
        ┃       └─▲─┘  └─────┘ │             ┃                        ┃                │ └─────┘   └─┬─┘      ┃      
        ┃      ┏━━┷━┓       ┏━━▼━┓           ┃                        ┃              ┏━┷━━┓          │        ┃      
        ┗━━━━━━┃    ┃━━━━━━━┃    ┃━━━━━━━━━━━┛                        ┗━━━━━━━━━━━━━━┃    ┃━━━━━━━━━━┿━━━━━━━━┛      
        ╔══════┃vet2┃═══════┃vrf1┃═══════════╗                        ╔══════════════┃ve2 ┃══════════╪════════╗      
        ║      ┃    ┃       ┃    ┃           ║                        ║a2d2:0:0:0:2::┃    ┃          │        ║      
        ║      ┗━━━━┛       ┗━┯━━┛           ║                        ║              ┗━━━━┛          │        ║      
        ║      ┏━━━━┓         │a2d2:0:0:0:1::║                        ║              ┏━━━━┓          │        ║      
        ║      ┃    ┃         │              ║                        ║              ┃    ┃          │        ║      
        ║      ┃vet1┃         │              ║                        ║              ┃ve1 ┃          │        ║      
        ║      ┃    ┃         │              ║                        ║a2d2:0:0:0:3::┃    ┃          │        ║      
        ║      ┗━━▲━┛         │              ║                        ║              ┗━━▲━┛          │        ║      
        ║         │           │              ║                        ║                 │            │        ║      
        ║         │        Routing           ║                        ║              Routing         │        ║      
        ║         │           │              ║                        ║                 │            │        ║      
        ║         │           │              ║                        ║                 │            │        ║      
        ║         │           │              ║                        ║                 │            │        ║      
        ║  TC filter          │              ║a1fa/NNI        a1fa/NNI║                 │            │        ║      
        ║         │       ┌───▼───┐    ┏━━━━━━━━━━┓             ┏━━━━━━━━━━┓        ┌───┴───┐        │        ║      
        ║         │       │███████│ ┌──▶   r2r3   ┃             ┃   r4r3   ┠───┐    │███████│        │        ║      
        ║         │       │███████├─┘  ┗━━━━━━━━━━┛             ┗━━━━━━━━━━┛   └────▶███████│        │        ║      
        ║         │       │H.Encap│          ║                        ║             │End.DT6│        │        ║      
        ║         │       │███████│          ║                        ║             │███████│        │        ║      
  ┏━━━━━━━━━━┓    │       │███████├─┐  ┏━━━━━━━━━━┓             ┏━━━━━━━━━━┓   ┌────▶███████│        │  ┏━━━━━━━━━━┓
  ┃   t1r2   ┠────┘       │███████│ └──▶   r2r4   ┃             ┃   r4r2   ┠───┘    │███████│        └──▶   r4l5   ┃
  ┗━━━━━━━━━━┛            └───────┘    ┗━━━━━━━━━━┛             ┗━━━━━━━━━━┛        └───────┘           ┗━━━━━━━━━━┛
a1fa/UNI║                                    ║a1fa/NNI        a1fa/NNI║                                       ║a1fa/UNI
        ║                                    ║                        ║                                       ║      
        ║    PREOF.SID: fd00:a2d2:0000::     ║                        ║    PREOF.SID: fd00:a2d2:0000::        ║      
        ║    Local SID: fade         Linux   ║                        ║    Local SID: fade             Linux  ║      
        ╚════════════════════════════════════╝                        ╚═══════════════════════════════════════╝      
```
The operation is the following: the incoming IP traffic from talker t1 is identified by the TC filter, and it is redirected to the veth interface. As the `t1r2` interface has IP address, it handles the ARP/NDP messages locally. The R2DTWO listens on the veth interface, and receives the packets.  After flow identification + any Replication/Elimination/Ordering/Delay (R/E/O/D) functions, R2DTWO encapsulates the packet by adding the outer IPv6 header which has the destination address the PREOF SID containing the `locator`, `func`, `flowid` and `seq` fields. The encapsulated packet is sent on the VRF interface.
The SRv6 tunnels encap rules defined in the Linux (H.Encaps) identifies the incoming packet, and further encapsulate into an IPv6 packet with SRH headers set accordingly.  
At the NNI interfaces we need to support multiple disjoint paths (on different NNI interfaces) for the PREOF functionality. These paths are created by different Linux SRv6 TE tunnels. In the R2DTWO config, the `func` field of the PREOF SID is used to direct the traffic to different SRv6 tunnels: for a given DetNet flow the `locator` is the same, but the PREOF SID `func` value varies to select different outgoing tunnels. The tunnel endpoint IP addresses are the Linux tunnel SIDs, belonging to the `fade` address range.
Besides the PREOF SID `func` field, the `argument` field is also used. The `argument.flowid` is used for flow identification at the NNI ingress. Each member flow has an unique `flowid`, making flow identification simple.

The `seq` field is also filled with the generated PREOF sequence number. The `writeseq <header>` command must be used to write the sequence number to the `seq` field.

On the other end, packets are received at the NNI interfaces. The Linux SRv6 endpoint rules identify these packets, and perform an End.DT6 decapsulation. A routing rule with /64 prefix matches on the `locator`, and forwards the packet to the next hop, through the `ve1` interface.
These packets will be received on the `ve2` interface by R2DTWO, where End.Decap of the outer IPv6 packet is performed with other (optional) R/E/O/D functions, and forwarded to the UNI interface.

In case of a transit node, the operation is shown on the figure below. The packet enters on an NNI interface, and the Linux SRv6 terminates the TE tunnel with and End.DT6 function. The decapsulated packet is then sent to the R2DTWO via the `ve1`-`ve2` interface pair. The R2DTWO performs the R/E/O/D operations needed, and sends the packet back to the `vrf1` interface. The Linux routing will select the outgoing SRv6 TE tunnel, and sends the packet encapsulating with a H.Encap function.

```
              ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━═━━━━━━━━━━━━━━━━┓      
              ┃ R2DTWO                                       ┃      
              ┃                     R/E/O/D                  ┃      
              ┃     End.Decap┌───┐  ┌─────┐  ┌───┐ H.Encap   ┃      
              ┃    IPv6 outer│███├──▶     │──▶███│ IPv6 outer┃    
              ┃              └─▲─┘  └─────┘  └─┬─┘           ┃      
              ┃              ┏━┷━━┓         ┏━━▼━┓           ┃
              ┗━━━━━━━━━━━━━━┃    ┃━━━━━━━━━┃    ┃━━━━━━━━━━━┛   
              ╔══════════════┃ve2 ┃═════════┃vrf1┃═══════════╗       
              ║a2d2:0:0:0:2::┃    ┃         ┃    ┃           ║
              ║              ┗━━━━┛         ┗━┯━━┛           ║     
              ║              ┏━━━━┓           │a2d2:0:0:0:1::║     
              ║              ┃    ┃           │              ║      
              ║              ┃ve1 ┃           │              ║      
              ║a2d2:0:0:0:3::┃    ┃           │              ║      
              ║              ┗━━▲━┛           │              ║      
              ║                 │          Routing           ║      
              ║                 │             │              ║      
              ║                 │             │              ║      
              ║              Routing          │              ║      
              ║                 │             │              ║      
      a1fa/NNI║                 │             │              ║a1fa/NNI    
        ┏━━━━━━━━━━┓        ┌───┴───┐     ┌───▼───┐    ┏━━━━━━━━━━┓  
        ┃   r4r3   ┠───┐    │███████│     │███████│ ┌──▶   r2r3   ┃
        ┗━━━━━━━━━━┛   └────▶███████│     │███████├─┘  ┗━━━━━━━━━━┛   
              ║             │End.DT6│     │H.Encap│          ║                        
              ║             │███████│     │███████│          ║
        ┏━━━━━━━━━━┓   ┌────▶███████│     │███████├─┐  ┏━━━━━━━━━━┓
        ┃   r4r2   ┠───┘    │███████│     │███████│ └──▶   r2r4   ┃
        ┗━━━━━━━━━━┛        └───────┘     └───────┘    ┗━━━━━━━━━━┛   
      a1fa/NNI║                                              ║a1fa/NNI
              ║                                              ║      
              ║    PREOF.SID: fd00:a2d2:0000::               ║      
              ║    Local SID: fade                    Linux  ║      
              ╚══════════════════════════════════════════════╝      
```

For Ethernet/TSN over SRv6, the configuration is simpler, as the UNI interfaces do not need IP addresses, and also they do not need to answer any ARP/NDP requests. Thus the veth interface pair is not needed. In this case, the R2DTWO uses the UNI interface as the ingress directly, without directing the traffic with TC filter actions. The Ethernet frame is encpsulated in an IPv6 packet, and at the NNI side it is handled similarly to the IP/IPv6 case.

## Linux SRv6 counters

The Linux SRv6 implementation supports counters associated with each SRv6 SID. By default, these counters are disabled, the `count` parameter must be added to the End.DT6 routing entry, for example:

```
ip -6 route add fd12:fade:0:0:1::/128 encap seg6local action End.DT6 count table 10 dev ve1
```

If `count` parameter is configured, the `ip -6 -s route show dev ve1` command can be used to display the statistics. For example:

```
$ ip -6 -s route show dev ve1
fd00:a2d2::/64 proto kernel metric 256 pref medium
fd14:fade:0:0:1::  encap seg6local action End.DT6 table 10 packets 20 bytes 4320 errors 0 metric 1024 pref medium
fe80::/64 proto kernel metric 256 pref medium
```

## R2DTWO configuration for SRv6

The R2DTWO configuration must be aware of the outer PREOF.SID locators, and also the flow IP addresses at the UNI if IP address based identification is used for flow separation.

In the `[interfaces]` section the following interfaces are needed:
* `eth` interface for the incoming traffic on UNI
* `ip` interface for outgoing traffic on UNI (in case of DetNet traffic, not needed for TSN traffic)
* `ip` interface for outgoing NNI traffic, opened on the VRF interface (`vrf1`).
* `ip` interface for incoming NNI traffic, opened on the VETH interface (`ve2`).
No further interfaces are needed, as for all SRv6 tunnels the packets will be sent through the VRF interface.

If replication/elimination is used, the objects must be created in the usual way.

In the `[streams]` section we need
* one entry per incoming flow
* one entry per flow and per member stream

The UNI incoming flow entries perform identification, remove unnecessary headers, add the outer IPv6 header, set the `locator` and the `hoplimit` and any other IPv6 field if needed (the IPv6 source address will be automatically set, but can be overriden by the config), writes the sequence field, and does the replication. For each replicated packet, the locator `func` field must be set for tunnel selection, and the `flowid` field must be set for identification before sending out on the VRF interface.

```
s1:actions = sgen1, before ipv4 add ipv6_outer hoplimit=32 loc=0xfd00a2d200000004, writeseq ipv6_outer, del eth, del cvlan, replicate member11 member12
member11 = edit ipv6_outer.func=0 ipv6_outer.flowid=0x11111, send if3
member12 = edit ipv6_outer.func=1 ipv6_outer.flowid=0x12222, send if3
```

The  NNI flow entries will identify the flows based on the `flowid`. After identification we read the sequence number from the `seq` field of the SID, remove the outer IPv6 header and after elimination it is sent to the egress UNI interface.

```
s3:match = ipv6_outer flowid=0x15555        ; match Locator 64 bit, both functions
s3:actions = readseq ipv6_outer, del ipv6_outer, pef1 send_uni1
```

## Examples

An SRv6 test script is available in the 'test' directory. The associated configuration files are in the `test/srv6` directory. There is a Readme too explaining the test scenario and srv6 script usage.
