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
│    eth0 ─┼────┼─ eno0            │         │            eno0 ─┼────┼─ eth0    │
10.0.100.11│    │ 10.0.100.1       │         │       10.0.200.1 │    10.0.200.22│
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
uni_eth = eth iface=eno0
uni_eth:streams = stream_uni_eth
uni_ip = ip iface=eno0
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

A workaround to avoid the misleading destination unreachable ICMP errors, is to drop them.
For example we can drop any __type 3 (destination unreachable)__ error before sending them to the talker at the UNI interface (`eno0`):

```
nxp1 iptables -A OUTPUT -o eno0 -p icmp --icmp-type destination-unreachable -j DROP
```

After that `iptables` drop rule, the misleading ICMP errors will be dropped.

__Important__: one can notice that in R2DTWO there are packets with the following error `no pipeline found for packet on uni_eth, unknown stream`.
This is normal, since R2DTWO receive a __copy from the ARP packets__, however we let the Linux handle the original packet (and generate the ARP reply eventually).
If one can ever experience duplicate ping replies or such kind of unknown stream packets, there are high chance R2DTWO and Linux both handled the packet.


## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before source a new environment in for a new test scenario!__
