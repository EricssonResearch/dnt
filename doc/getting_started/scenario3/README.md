# Scenario #3: R2DTWO IPv4/IPv6 over DetNet PseudoWire

__Important: this scenario assumes background knowledge of the basics from Scenario #1 and #2. Please take a look into Scenarios #1 and #2 if you have not already.__ [Scenario #1](../scenario1/README.md), [Scenario #2](../scenario2/README.md)

In the following, we will use R2DTWO as a DetNet router.
The Layer3 traffic of `talker` and `listener` nodes will be encapsulated in MPLS DetNet pseudowires, then sent through a PREF (Packet Replication and Elimination Functions).

__Important: if this test running on the NXP boards, use the setup scripts for configuring the network properly!__: `nxp1_setup.sh` and `nxp2_setup.sh`

We will use the following topology, which consists:

* a talker node called **talker** which will generate IPv4 and IPv6 traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO instances, running on the **nxp1** and **nxp2** nodes.

```
    talker              nxp1                         nxp2              listener
┌──────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌──────────┐
│          │    │      192.168.55.1│         │192.168.55.2      │    │          │
│          │    │           swp0  ─┼─────────┼─  swp0           │    │          │
│          │    │         fc0a::1  │         │ fc0a::2          │    │          │
│          │    │                  │         │                  │    │          │
│2001::11  │    │ 2001::1          │         │       2002::2    │    │2002::22  │
│    eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0    │
10.0.100.11│    │ 10.0.100.1       │         │       10.0.200.1 │    10.0.200.22│
│          │    │                  │         │                  │    │          │
│          │    │         fc0b::1  │         │ fc0b::2          │    │          │
│          │    │           swp1  ─┼─────────┼─  swp1           │    │          │
│          │    │      192.168.66.1│         │192.168.66.2      │    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘

                      PRF ───►                     ───► PEF

                      PEF ◄───                     ◄─── PRF

                      R2DTWO                       R2DTWO
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will be utilized by R2DTWO for redundancy.

As one can notice, now the talker and listener are placed into different subnets, so __nxp1__ and __nxp2__ must act as a router.

Unlike in the TSN over DetNet case (Scenario #2) now R2DTWO operates at Layer3 on __both UNI and NNI__ interfaces.
That means the IP packets from the talker will be encapsulated into MPLS DetNet PW and routed towards the PEF.
The PEF handles the duplicates, then after decapsulating the packet __until the IPv4 or IPv6 header__ sent by the talker, it will send the packet to the listener over a native IP interface.
The reverse path from listener to talker is similar.

The benefit of IP over DetNet that we can leverage the Linux's network stack ARP and ND resolution (among other Layer2 services).

Also, in some cases, we don't have Layer2 at all (IP tunnel interfaces, IP PDU sessions) so TSN in DetNet not suitable for such use-cases.

In the scenario above the routing is very simple, but the same configuration would work even when there are multiple routers between `nxp1` and `nxp2`.

## The R2DTWO configurations

Now we have separate R2DTWO config files for the two DetNet nodes: `nxp1.ini` and `nxp2.ini`.
This is required because even if the topology is symmetrical, we have different IP addresses on the two routers.

In the following we only take a closer look to `nxp1.ini` since `nxp2.ini` almost identical expect the IP addressing (they mirrored basically).


### Explanation of the configuration

Most of the configuration is similar to the TSN over DetNet case (Scenario #2).

One difference is right now we use IPv4 and IPv6 underlay network for the DetNet PWs.
As one can see on the topology every interface has IPv4 and IPv6 addresses assigned.
In the following config, we will use the IPv4 underlay for the `talker`'s IPv4 traffic.
Likewise, we use IPv6 underlay for its IPv6 traffic.
We will take a closer look later in this guide.

Another difference is now we have a new egress interface: `uni_out`.
This interface must be specified in order to send IP packets, but it cannot be used to receive packets.
__Important:__ the IP protocol identified by the encapsulation of the packet.
So if we send traffic on it, the header stack must starting `ipv4` or `ipv6`.

Since we can receive Ethernet packets and there is the `del` action to remove the Ethernet header, this is not an issue.

```
uni_in = eth iface=swp2
uni_out = ip iface=swp2

v4nni_in0 = udp-in iface=swp0 ipv=4
v6nni_in0 = udp-in iface=swp0 ipv=6
v4nni_in1 = udp-in iface=swp1 ipv=4
v6nni_in1 = udp-in iface=swp1 ipv=6

v4nni_out0 = udp-out iface=swp0 dstip=192.168.55.2
v6nni_out0 = udp-out iface=swp0 dstip=fc0a::2
v4nni_out1 = udp-out iface=swp1 dstip=192.168.66.2
v6nni_out1 = udp-out iface=swp1 dstip=fc0b::2

uni_in:streams = compound4 compound6
v4nni_in0:streams = pw4
v4nni_in1:streams = pw4
v6nni_in0:streams = pw6
v6nni_in1:streams = pw6
```

On `spw2` which is the UNI we keep receiving Ethernet packets.
As discussed above, for sending we utilize it as a Layer3 interface.
This is practical, since that way Linux will do the ARP or ND resolution.
Otherwise this should be handled or configured within R2DTWO.

On `spw0` and `spw1` we have four PWs defined, two for ingress and two for egress traffic.
For example, the two IPv6 egress PW named as `v6nni_out0` and `v6nni_out1`.
For demonstration sake, we have separated PWs for IPv4 and IPv6 underlay.
However it can be done with a shared PW for IPv4 and IPv6 streams, since we identify them by their MPLS label.
That means the NNI interfaces does not need to be dual-stacked, either IPv4 or IPv6 only should works.

At the end of the section, there are two streams defined on the `uni_in`.
We use the trick of R2DTWO's implicit VLAN tag, where the _tag protocol identifier_ (`cvlan.tpid`) filled correctly with IPv4 or IPv6 ethertype.
Therefore in `compound4` we match IPv4 and with `compound6` we match IPv6 traffic.

The `[objects]` section is similar as we seen in Scenario #2.
There we had separate PREF objects for the two TSN streams, here we have for the IPv4 and IPv6 streams.


The `[streams]` section can be divided into two parts.
The first part is the actions related to the UNI traffic, the defined in the `compound4` and `compound6` stream for IPv4 and IPv6 UNI traffic respectively.
The second part is the NNI stream definitions, `pw4` and `pw6` for IPv4 and IPv6 respectively.

Lets start with the first part:

```
compound4:packet = eth, cvlan, ipv4
compound4:match = cvlan tpid=ipv4, ipv4 src=10.0.100.0/24
compound4:actions = gen4, del eth, del cvlan, before ipv4 add dcw, before dcw add mpls bos=1, prf4 prf4-member1 prf4-member2

prf4-member1 = edit mpls.label=400 mpls.bos=1, send v4nni_out0
prf4-member2 = edit mpls.label=400 mpls.bos=1, send v4nni_out1

compound6:packet = eth, cvlan, ipv6
compound6:match = cvlan tpid=ipv6, ipv6 src=2001::/64
compound6:actions = gen6, del eth, del cvlan, before ipv6 add dcw, before dcw add mpls bos=1, prf6 prf6-member1 prf6-member2

prf6-member1 = edit mpls.label=600 mpls.bos=1, send v6nni_out0
prf6-member2 = edit mpls.label=600 mpls.bos=1, send v6nni_out1
...
```

There are two useful tricks to match packets on streams.
Ethertypes can be identified with name in the `:match` section like `tpid=ipv4`.
The second trick, for IP addresses we can defined prefix lengths.
This can be done for IPv4 and IPv6 as well e.g.: `ipv4 src=10.0.100.0/24`.
With that, we can implement site-to-site DetNet routing between subnets as well as individual hosts.

For the full list of the supported R2DTWO actions, their parameters and behavior please consult with the documentation.
We have the `compound4` and `compound6` streams, which can match to the traffic received by the UNI interface.
If we found matching packets, we do the DetNet encapsulation __but we should remove all Layer2 headers like Ethernet and VLAN__.
This is important since the other R2DTWO endpoint terminating the pseudowire expects IP over DetNet encapsulated packets.


For IPv4 traffic we set MPLS 400 for both member streams likewise label 600 for IPv6.
With that, as mentioned before, one DetNet PW would be enough for both IPv4 and IPv6 since the PWs identified by their labels.

The second part of the `[streams]` section define the NNI streams.
As we see in the interface stream definitions, we try to match the same PWs on both paths.
With other words, we dont have separate member stream definitions for ingress.
We do have separate NNI stream definitions for IPv4 and IPv6 traffic.

```
...
pw4:packet = mpls, dcw, ipv4
pw4:match = mpls label=400
pw4:actions = readseq dcw, pef4 compound

pw6:packet = mpls, dcw, ipv6
pw6:match = mpls label=600
pw6:actions = readseq dcw, pef6 compound

compound = del mpls, del dcw, send uni_out
```

Here we can use a trick for the PEF part: both `pef4` and `pef6` jumps to the `compound` pipeline.
This is not necessary, but since we do the same actions for both streams after elimination this is doable.
After removing `mpls` and `dcw` we have a Layer3 packet which is the IP packet originally sent by the talker.

At the end of the pipeline, we will send the packet on the native IP interface.
Now since the routing is quite trivial (we only have one UNI interface) the Linux network stack only helps us in the ARP or ND resolution of the listener's IP address.


## Run the R2DTWO and generate traffic

Let's try out R2DTWO with this scenario.

For that we need at least three terminal windows: one for generating traffic (`talker`) and two for run R2DTWO instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo su

source env.sh
```

If everything OK, the prompt should be changed to `(ip over detnet) root:scenario3# ` which tells right now we are in the test network environment.
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
This is expected: `nxp1` dont have any routing entry for the `10.0.200.0/24` network, therefore can't route the packet properly.
That means it will generate an ICMP error message and send it back to the talker.
But why is this the case?

R2DTWO working with the user-space copy of the packet.
That means the packet continue its way in the Linux packet processing pipeline regardless if R2DTWO processed it or not.
Essentially, since the Layer2 frame of the ping packet sent by the talker is handled by R2DTWO and properly transmitted to the listener, we will receive a reply for that.
Beside that, the Linux also process the frame, which goes up to the IP protocol handler which generate the net unreachable ICMP error.

In fact, the root of the problem is the bad configuration.
If one can read the document carefully, the recommended way to use R2DTWO in IP over DetNet scenario is to use it together with OvS or Linux TC.
With those tools, we can do the following:

1. Configure TC or OvS rules on the ingress interface, matching on the traffic expected in stream(s) defined in R2DTWO
2. Redirect the matching packets to a virtual interface, like veth which defined as UNI in the R2DTWO config
3. Let the Linux network stack handle the background traffic (which is not redirected hence invisible to R2DTWO)

This can be visualized in the figure below:

```
                                R2DTWO
                              ┌────────────────────────────┐
                              │                            │
                              │                            │
                              │                            │
                              │ ┌───────┐                  │
                              │ │r2veth1│ ◄─── uni_i       │
                              │ └───┬───┘                  │
                              │     │                      │
                              │     │                      │
                              └─────┼──────────────────────┘
                                    │
                                    │
                                ┌───┴───┐
                                │r2veth0│
                                └───────┘
                                    ▲
                                    │Stream(s)
  Streams +   ┌──────────────┐      │traffic
  background  │  2001::1     ├──────┘
   traffic    │     swp2     │
──────────────►  10.0.100.1  ├────────────────►
              └──────────────┘   Background
                                   traffic
```


To do that in this particular scenario there is a predefined shell function `configure_tc` for convenience:

```
(ip over detnet) root:scenario3# configure_tc
```

This function setup the veth interfaces and apply the Linux TC filters and redirections.
We can check the commands with the `declare -f configure_tc` command:

```
(ip over detnet) root:scenario3# declare -f configure_tc
configure_tc () 
{ 
    ip netns exec nxp1 ip link add r2eth0 type veth peer name r2eth1;
    ip netns exec nxp1 ip link set dev r2eth0 up;
    ip netns exec nxp1 ip link set dev r2eth1 up;
    ip netns exec nxp1 tc qdisc add dev swp2 handle ffff: ingress;
    ip netns exec nxp1 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.100.11 dst_ip 10.0.200.22 action mirred egress redirect dev r2eth0;
    ip netns exec nxp1 tc filter add dev swp2 parent ffff: protocol ipv6 flower src_ip 2001::11 dst_ip 2002::22 action mirred egress redirect dev r2eth0;
    ip netns exec nxp2 ip link add r2eth0 type veth peer name r2eth1;
    ip netns exec nxp2 ip link set dev r2eth0 up;
    ip netns exec nxp2 ip link set dev r2eth1 up;
    ip netns exec nxp2 tc qdisc add dev swp2 handle ffff: ingress;
    ip netns exec nxp2 tc filter add dev swp2 parent ffff: protocol ip flower src_ip 10.0.200.22 dst_ip 10.0.100.11 action mirred egress redirect dev r2eth0;
    ip netns exec nxp2 tc filter add dev swp2 parent ffff: protocol ipv6 flower src_ip 2002::22 dst_ip 2001::11 action mirred egress redirect dev r2eth0
}
```

As one can see, this setup the veths on both `nxp1` and `nxp2`, and redirect the IP traffic coming from the `talker` or `listener` to `r2eth1` interface.
R2DTWO receive this IP traffic, encapsulate it to DetNet pseudowires, replicate it to the NNI interfaces.
At the termination of the pseudowire, the IP packet decapsulated and sent with the IP header as a Layer3 packet on the UNI interface.
In this case, the UNI interface set to `swp2` which is also the default gateway's interface for the `talker` and `listener`.

So we have to edit the config files to use these new veth interfaces dedicated to R2DTWO UNI traffic only.
For that, set the `uni_in` interface's definition from `swp2` UNI to the `r2eth1` UNI.

```
[interfaces]
...
uni_in = eth iface=r2eth1
uni_out = ip iface=swp2
...
```
We can use the configs __after__ executing `configure_tc` command in one of the bash.
(If `configure_tc` not executed, R2DTWO will fail to start since no veth interfaces exists)

### Workaround without TC or OvS and debugging

For many different streams the TC redirect configuration can be difficult and hard to debug.
There is a workaround, which not correct but at least with that `nxp1` and `nxp2` does not generate network unreachable errors.
As explained before, the packet received from UNI continues its journey in the network stack processing after handled by R2DTWO.
As also explained before, with TC filters we can redirect the packets of a stream to a new interface, which dont have IP addresses configured.
Therefore only R2DTWO see it, then the processing not continued by the network stack.

A workaround without including TC filters and creation of new UNI interface is applying blackhole routes to the UNI routes.
In our example, this looks like the following (__note:__ before that, please restart the test environment, to delete the TC redirection rules):

```
(ip over detnet) root:scenario3# nxp1 ip route add blackhole 10.0.200.0/24
(ip over detnet) root:scenario3# nxp1 ip route add blackhole 2002::/64
(ip over detnet) root:scenario3# nxp2 ip route add blackhole 10.0.200.0/24
(ip over detnet) root:scenario3# nxp2 ip route add blackhole 2002::/64
```

With this, we cant see any network unreachable errors on the `talker`.
That is because Linux explicitly told to drop this traffic at the routing stack and that is intended, not generate any error.

But this is not a recommended solution, since if we start R2DTWO with packet traces enabled, we will see packets from interface `uni_in` with the `unknown stream` message.
That is because background traffic like ARP and ND in this case also forwarded to R2DTWO.
If we dont have too much background traffic, this is not and issue.
But if we, it cause CPU overhead to copy, match and then drop packets at userspace.
Even worse, a bad stream matching statement might match on these packets by mistake for a known stream.
So the recommended way to pre-filter streams for R2DTWO if there are background traffic on the network.

With that workaround, or the TC pre-filtering, we should see the following ping outputs:

```
(ip over detnet) root:scenario3# talker ping -c 4 10.0.200.22
PING 10.0.200.22 (10.0.200.22) 56(84) bytes of data.
64 bytes from 10.0.200.22: icmp_seq=1 ttl=64 time=0.390 ms
64 bytes from 10.0.200.22: icmp_seq=2 ttl=64 time=0.382 ms
64 bytes from 10.0.200.22: icmp_seq=3 ttl=64 time=0.374 ms
64 bytes from 10.0.200.22: icmp_seq=4 ttl=64 time=0.624 ms

--- 10.0.200.22 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3059ms
rtt min/avg/max/mdev = 0.374/0.442/0.624/0.104 ms


(ip over detnet) root:scenario3# talker ping -c 4 2002::22
PING 2002::22 (2002::22) 56 data bytes
64 bytes from 2002::22: icmp_seq=1 ttl=64 time=0.483 ms
64 bytes from 2002::22: icmp_seq=2 ttl=64 time=0.845 ms
64 bytes from 2002::22: icmp_seq=3 ttl=64 time=0.348 ms
64 bytes from 2002::22: icmp_seq=4 ttl=64 time=0.334 ms

--- 2002::22 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3072ms
rtt min/avg/max/mdev = 0.334/0.502/0.845/0.206 ms
```


## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
