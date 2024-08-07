
# SRv6 support

SRv6 support makes possible the use of SRv6 as transport for DetNet tunnels.
The R2DTWO support for SRv6 relies on the Linux SRv6 functions. As currently the Linux SRv6 implementation does not support DetNet, we use an outer IPv6 encapsulation which uses a PREOF SID as destination address. This IPv6 encapsulated packet will be directed into a predefined SRv6 tunnel.
```
┌─────────────────────────────────────┐                                  
│ Original packet (IPv6, IPv4 or TSN) │                                  
└─────────────────────────────────────┘                                  

┌────────────────────────────┬─────────────────────────────────────┐                                  
│IPv6 header, dest=PREOF.SID │ Original packet (IPv6, IPv4 or TSN) │    R2DTWO                        
└────────────────────────────┴─────────────────────────────────────┘                                  

┌──────────────────────┬────────────────────────────┬─────────────────────────────────────┐                                
│IPv6 header + SRH     │IPv6 header, dest=PREOF.SID │ Original packet (IPv6, IPv4 or TSN) │    Linux SRv6 encap seg6            
└──────────────────────┴────────────────────────────┴─────────────────────────────────────┘                                 
```
To support the PREOF SID structure, the following header fields have been added to the IPv6 header:

* `loc`, as a 64 bit Locator
* `func`, as a 16 bit Functon
* `flowid` as a 20 bit Flow Id
* `seq`, as a 28 bit DetNet sequence field.

The DetNet sequence field is similar to the TSN or DetNet sequence field, but it's only 28 bits.
```
┌──────────────┬──────────────┬────────────────────────────────────────┐                       
│ 4 bit flags  │8 bit reserved│           16 bit sequence number       │                       
└──────────────┴──────────────┴────────────────────────────────────────┘                       
```
At the UNI interface, the incoming traffic will be identified and directed to R2DTWO. R2DTWO will encapsulate the incoming packet (which can be IPv6, IPv4 or TSN), and sets the destination PREOF SID. The encapsulated packet will be routed to an SRv6 encap Lightweight Tunnel, which encapsulates again the packet within a new IPv6 header containing the SRH.
At the egress node, incoming packets will be directed to an End.DT6 termination, which decapsulates the SRv6 headers. The decapsulated IPv6 packet containing the PREOF SID will be routed to the R2DTWO. The R2DTWO performs identification, elimination, and decapsulates the original packet to send out on the UNI.

## Linux configuration for SRv6

The SRv6 interworking requies several internal interfaces:
* First of all, all interfaces need IPv6 addresses to work. For this, we use IP addresses in the `alfa` range. Also, the routing should also use these addresses.
* All nodes should also have a loopback interface, in our case `sr0`, which should have an address from the `fade` range. Since the default loopback interface does not work with SRv6, we use `dummy` interface for loopback.
* At the UNI interface, a TC filter should intercept the DetNet/TSN traffic and direct it to the vet1/2 veth interface pair. This is needed because the UNI interface holds the IP address, and it will answer any ARP/NDP requests.
* R2DTWO will use a vrf interface to send and receive IP packets on NNI interfaces. The choice for VRF type interface was that it behaves as an IP interface, not requiring ARP/NDP support. SRv6 routing entries will be used to direct traffic from the VRF interface towards the tunnels, and routing will be used to direct the incoming decapsulated traffic to the VRF interface. The VRF interface must also have an IPv6 address, from range `bel2`. This has local meaning, a routing entry will use it to route the /64 PREOF SID range through this interface, specifying a next hop from the `bel2` range.

```
        ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓                        ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓      
        ┃                             R2DTWO ┃                        ┃ R2DTWO                             ┃      
        ┃       H.Encap IPv6 outer           ┃                        ┃          End.Decap IPv6 outer      ┃      
        ┃           ┌────────┐               ┃                        ┃               ┌────────┐           ┃      
        ┃         ┌─│████████│─┐             ┃                        ┃             ┌─│████████│──┐        ┃      
        ┃         │ └────────┘ │             ┃                        ┃             │ └────────┘  │        ┃      
        ┃      ┏━━┷━┓       ┏━━┷━┓           ┃                        ┃           ┏━┷━━┓          │        ┃      
        ┗━━━━━━┃    ┃━━━━━━━┃    ┃━━━━━━━━━━━┛                        ┗━━━━━━━━━━━┃    ┃━━━━━━━━━━┿━━━━━━━━┛      
        ╔══════┃vet2┃═══════┃vrf1┃═══════════╗                        ╔═══════════┃vrf1┃══════════╪════════╗      
        ║      ┃    ┃       ┃    ┃           ║                        ║           ┃    ┃          │        ║      
        ║      ┗━━━━┛       ┗━┯━━┛           ║                        ║           ┗━━┯━┛          │        ║      
        ║      ┏━━━━┓         │  bel2        ║                        ║         bel2 │            │        ║      
        ║      ┃    ┃         │              ║                        ║              │            │        ║      
        ║      ┃vet1┃         │              ║                        ║              │            │        ║      
        ║      ┃    ┃         │              ║                        ║              │            │        ║      
        ║      ┗━━┯━┛         │              ║                        ║              │            │        ║      
        ║         │           │              ║                        ║           Routing         │        ║      
        ║         │        Routing           ║                        ║              │            │        ║      
        ║         │           │              ║                        ║              │            │        ║      
        ║         │           │              ║                        ║              │            │        ║      
        ║         │           │              ║                        ║              │            │        ║      
        ║  TC filter          │              ║  alfa           alfa   ║              │            │        ║      
        ║         │       ┌───┴───┐    ┏━━━━━━━━━━┓             ┏━━━━━━━━━━┓     ┌───┴───┐        │        ║      
        ║         │       │███████│ ┌──┨   r2r3   ┃             ┃   r4r3   ┠──┐  │███████│        │        ║      
        ║         │       │███████├─┘  ┗━━━━━━━━━━┛             ┗━━━━━━━━━━┛  └──┤███████│        │        ║      
        ║         │       │H.Encap│          ║                        ║          │End.DT6│        │        ║      
        ║         │       │███████│          ║                        ║          │███████│        │        ║      
  ┏━━━━━━━━━━┓    │       │███████├─┐  ┏━━━━━━━━━━┓             ┏━━━━━━━━━━┓  ┌──┤███████│        │  ┏━━━━━━━━━━┓
  ┃   t1r2   ┠────┘       │███████│ └──┨   r2r4   ┃             ┃   r4r2   ┠──┘  │███████│        └──┨   r4l5   ┃
  ┗━━━━━━━━━━┛            └───────┘    ┗━━━━━━━━━━┛             ┗━━━━━━━━━━┛     └───────┘           ┗━━━━━━━━━━┛
alfa    ║                                    ║  alfa           alfa   ║                                    ║  alfa
        ║                                    ║                        ║                                    ║      
        ║    PREOF.SID: bela                 ║                        ║    PREOF.SID: bela                 ║      
        ║    Local SID: fade         Linux   ║                        ║    Local SID: fade          Linux  ║      
        ╚════════════════════════════════════╝                        ╚════════════════════════════════════╝      
```
The operation is the following: the incoming IP traffic from talker t1 is identified by the TC filter, and it is redirected to the veth interface. As the `t1r2` interface has IP address, it handles the ARP/NDP messages locally. The R2DTWO listens on the veth interface, and receives the packets. (Note that in case of TSN over SRv6 the veth interface pair is not needed, and also no IP is needed on the UNI interfac.e) After flow identification, R2DTWO adds the outer IPv6 header which has the destination address the PREOF SID containing the `locator`, `func`, `flowid` and `seq` fields. The encapsulated packet is sent on the VRF interface.
The SRv6 tunnels encap rules will identify the incoming packet, and further encapsulate into an IPv6 packet with SRH headers set accordingly. The `func` field is used to direct the traffic to different SRv6 tunnels: for a given DetNet flow the `locator` is the same, but the `func` value varies to select different outgoing tunnels. The tunnel endpoint IP addresses are the `lo` interface addresses.

On the other end, packets are received at the NNI interfaces. The Linux SRv6 endpoint rules identify these packets, and perform an End.DT6 decapsulation. A routing rule with /64 prefix matches on the `locator`, and forwards the packet to the VRF interface.
Caveat: blackhole routing is needed to prevent the decapsulated packet from further routing after being sent on VRF interface, which actually leads to an internal loop. R2DTWO receives the packets, identifies the flows, performs elimination if needed, and forwards the packets directly to the UNI interface.

## R2DTWO configuration for SRv6

The R2DTWO configuration must be aware of the outer IPv6 SID locators, and also the IPv6 addresses if IP address based identification is used for flow separation.

In the `[interfaces]` section the following interfaces are needed:
* `eth` interface for the incoming traffic on UNI
* `ip` interface for outgoing traffic on UNI (in case of DetNet traffic, not needed for TSN traffic)
* `ip` interface for NNI traffic, opened on the VRF interface.
No further interfaces are needed, as for all SRv6 tunnels the packets will be sent through the VRF interface.

If replication/elimination is used, the objects must be created in the usual way.

In the `[streams]` section we need
* one entry per incoming flow
* one entry per flow and per member stream

The UNI incoming flow entries perform identification, remove unnecessary headers, add the outer IPv6 header, set the `locator` and the `hoplimit` and any other IPv6 field if needed (the IPv6 source address will be automatically set, but can be overriden by the config), writes the sequence field, and does the replication. For each replicated packet, the `func` field must be set for tunnel selection, and the `flowid` field must be set for identification before sending out on the VRF interface.

The  NNI flow entries will identify the flows based on the `flowid`. After identification we read the sequence number from the `seq` field of the SID, remove the outer IPv6 header and after elimination it is sent to the egress UNI interface.


## Examples

An SRv6 test script is available in the 'test' directory. The associated configuration files are in the `test/srv6` directory. There is a Readme too explaining the test scenario and srv6 script usage.
