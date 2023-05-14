# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

__Important: this scenario assumes background knowledge of the basics from Scenario #1 and #2. Please take a look into Scenario #1 and #2 if you have not already.__ [Scenario #1](../scenario1/README.md), [Scenario #2](../scenario2/README.md)

# Scenario #2: R2DTWO IPv4 over DetNet

In the following we will use R2DTWO as a DetNet router.
The Layer3 traffic of `talker` and `listener` nodes will be encapsulated in MPLS DetNet pseudowires, then sent through a PREF (Packet Replication and Elimination Functions).

We will use the following topology, which consist:
* a talker node called **talker** which will generate IPv4 traffic
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
10.0.100.11│    │ 10.0.100.1       │         │       10.0.200.1 │    10.0.200.22│
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│          │    │           swp1  ─┼─────────┼─  swp1           │    │          │
│          │    │      192.168.66.1│         │192.168.66.2      │    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘

                      PRF ───>                     ───> PEF

                      PEF <───                     <─── PRF

                      R2DTWO                       R2DTWO
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will utilized by R2DTWO for redundancy.

As one can notice, now the talker and listener are placed into different subnets, so __nxp1__ and __nxp2__ must act as a router.

Unlike in the TSN over DetNet case (Scenario #2) now R2DTWO operates at Layer3 on __both UNI and NNI__ intefaces.
That means the IP packets from the talker will be encapsulated into MPLS DetNet CW and routed towards the PEF.
The PEF handle the duplicates, then after decapsulate the packet __until the IPv4 header__ sent by the talker, it will send the packet to the listener over a native IP interface.
The reverse path from listener to talker is similar.

The benefit of IP over DetNet that we can leverage the Linux's network stack ARP resolution (among other Layer2 services).
Also, in some cases we dont really have Layer2 at all (IP tunnel interfaces, IP PDU sessions) so TSN in DetNet not suitable for such usecases.


In the scenario above however the routing is very simple, but the same configuration would work even when there are multiple routers between `nxp1` and `nxp2`.

## The R2DTWO configurations

Now we have separate R2DTWO config files for the two DetNet nodes: `nxp1.ini` and `nxp2.ini`.
This is required because even if the topology symmetrical, we have different IP addresses on the two routers.


### Explanation of the configuration

For full details, please take a look into the R2DTWO documentation.
Also, most of the configuration similar to the TSN over DetNet case (Scenario #2).

One of the main difference is now we have a new egress interface: `uni_ip`.
This interface must be specified in order to send IP packets, but it cannot be used to receive packets.
Since we can receive Ethernet packets and there is the `del` action to remove the Ethernet header, this is not an issue.

```
[interfaces]
uni_eth = eth iface=swp2
uni_eth:streams = stream_uni_eth
uni_ip = ip iface=swp2
...
```

The `[objects]` section is same as we seen in Scenario #2.

The `[streams]` section looks like the following:

```
[streams]
stream_uni_eth:packet = eth, cvlan, ipv4
stream_uni_eth:match = ipv4 src=10.0.100.11
stream_uni_eth:actions = prf, before eth add mpls, after mpls add dcw, writeseq dcw, del eth, del cvlan, replicate nni1_out nni2_out

nni1_out = edit mpls.label=100 mpls.bos=1, send nni1_out
nni2_out = edit mpls.label=200 mpls.bos=1, send nni2_out

stream_nni:packet = mpls, dcw, ipv4
stream_nni:match = ipv4 dst=10.0.100.11
stream_nni:actions = readseq dcw, pef, del dcw, del mpls, send uni_ip
```

For the full list of the supported R2DTWO actions, their parameters and behavior please consult with the documentation.

We have the `stream_uni_eth` stream, which can match to the traffic received by the UNI interface.
Right now we match for the IP address of the talker: `stream_uni_eth:match = ipv4 src=10.0.100.11`.
If we found matching packets, we do the DetNet encapsulation __but we should remove all Layer2 headers like Ethernet and VLAN__.
This is important since the other R2DTWO endpoint terminating the pseudowire expect IP over DetNet encapsulated packets.

At the NNI ingress, as one can see in the `:packet` line, we will define the `ipv4` header after the `dcw`.
After removing `mpls` and `dcw` we have a Layer3 packet which is the IPv4 packet originally sent by the talker.

At the end of the pipeline, we will send the packet on the native IP interface.
Now since the routing is quite trivial (we only have one UNI interface) the Linux network stack only helps us in the ARP resolution of the listener's IP address.


## Run the R2DTWO and generate traffic

Lets try out R2DTWO with this scenario.

For that we need at least three terminal window: one for generate traffic (`talker`) and two for run R2DTWO instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo su

source env.sh
```

If everything OK, the prompt should be changed to `(ip over detnet) root:scenario2# ` which tells right now we are in the test network environment.
Now we should have all the networking (nodes, interfaces, IP addresses and routing) configured and helper commands to execute commands on the nodes.

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
talker ping -c 4 10.0.200.22
PING 10.0.200.22 (10.0.200.22) 56(84) bytes of data.
From 10.0.100.1 icmp_seq=1 Destination Net Unreachable
64 bytes from 10.0.200.22: icmp_seq=1 ttl=64 time=0.296 ms
From 10.0.100.1 icmp_seq=2 Destination Net Unreachable
64 bytes from 10.0.200.22: icmp_seq=2 ttl=64 time=0.321 ms

--- 10.0.200.22 ping statistics ---
2 packets transmitted, 2 received, +2 errors, 0% packet loss, time 1012ms
rtt min/avg/max/mdev = 0.296/0.308/0.321/0.012 ms
```

As one can see, we have __Destination Net Unreachable__ errors.
This is expected: __nxp1__ dont have any routing entry for `10.0.200.0/24` network.
That means it will generate an ICMP error message and send it back to the talker.

However, since the Layer2 frame of the ping packet sent by the talker handled by R2DTWO and properly transmitted to the listener, we will receive a reply for that.

In fact, the root of the problem is the bad configuration.
If one can read the document carefully, the recommended way to use R2DTWO in IP over DetNet scenario is to use together with OvS or Linux TC.
With those tools, we can do the following:

1. Configure TC or OvS rules on the ingress interface, matching on the traffic expected in stream(s) defined in R2DTWO
2. Redirect the matching packets to a virtual interface, like veth which defined as UNI in the R2DTWO config
3. Let the Linux network stack handle the background traffic

This can be visualized on the figure below:

```
                                R2DTWO
                              ┌────────────────────────────┐
                              │                            │
                              │                            │
                              │                            │
                              │ ┌───────┐                  │
                              │ │r2veth1│ <─── uni         │
                              │ └───┬───┘                  │
                              │     │                      │
                              │     │                      │
                              └─────┼──────────────────────┘
                                    │
                                    │
                                ┌───┴───┐
                                │r2veth0│
                                └───────┘
                                    ^
                                    │Stream(s)
  Streams +   ┌──────────────┐      │traffic
  background  │              ├──────┘
   traffic    │     swp2     │
──────────────>  10.0.100.1  ├────────────────>
              └──────────────┘   Background
                                   traffic
```

So we have to edit the config files to use the interfaces dedicated to R2DTWO traffic only:

```
uni_eth = eth iface=r2veth1
```

Then modify the networking accordingly:

```
# Add veth UNI interfaces
nxp1 ip link add r2veth0 type veth peer name r2veth1
nxp2 ip link add r2veth0 type veth peer name r2veth1

# Turn on ingress filtering to the UNI interfaces
nxp1 tc qdisc add dev swp2 handle ffff: ingress
nxp2 tc qdisc add dev swp2 handle ffff: ingress

# Redirect stream traffic (IPv4 with the good source/destination) to R2DTWO
nxp1 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.100.11 action mirred egress redirect dev r2veth0
nxp2 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.200.22 action mirred egress redirect dev r2veth0
```

With this configuration, the ICMP errors will disappears, since the `r2veth1` interface dont have IP address so no routing or any other Layer3 operation will be skipped by the Linux networking.

In the example above, we used the Linux's built-in _Traffic Control_ subsystem and `tc filter` to separate the background and time sensitive traffic, however one can do it with Open vSwitch as well.

__Note that__ this idea can be used in other cases as well, when we want to pre-filter the traffic.
This is beneficial, since background traffic will by dropped by R2DTWO anyway, lack of matching packets, consuming CPU resources.

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before source a new environment in for a new test scenario!__
