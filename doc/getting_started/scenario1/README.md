# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

# Scenario #1: R2DTWO TSN operation, layer 2 bridge mode

In the following we will use R2DTWO as a Layer2 TSN switch with IEEE 802.1CB function.

R2DTWO can protect the traffic by using redundant network paths simultaneously.
Every packet duplicated (or more generally replicated, since it can supports more than two paths) to the network paths, called _replication_.
The receiving node only accept the first copy of the packet and drop the rest, that's called _elimination_.

We will use the following topology, which consist:
* a talker node called **talker** which will generate traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO switches, called **nxp1** and **nxp2** switching the traffic between the __talker__ and __listener__ (as mentioned before, those can be physical switches, but this guide will use virtual ones)
```
  talker              nxp1                         nxp2              listener
┌────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌─────────┐
│        │    │                  │         │                  │    │         │
│        │    │          swp0   ─┼─────────┼─  swp0           │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│  eth0 ─┼────┼─ eno0            │         │            eno0 ─┼────┼─ eth0   │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          swp1   ─┼─────────┼─  swp1           │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will utilized by R2DTWO for redundancy.

## The R2DTWO configuration

In the first scenario, we try out R2DTWO's IEEE 802.1CB FRER implementation.
This utilize the FRER Redundancy Tag header with sequence numbers and operates in Layer 2.

The configuration file for R2DTWO prepared in the this folder: `r2dtwo.ini`.
This is a symmetrical configuration, reflecting the to the network topology above.
In cases where the network setup not symmetrical, separate configs might required on each switch.

### Explanation of the configuration

For full details, please take a look into the R2DTWO documentation.
Right now we are only explaining the actions in the `r2dtwo.ini` file.
Like other R2DTWO configurations, this consists three main sections: `[interfaces]`, `[objects]` and `[streams]`.

The `[interfaces]` section describes the network interfaces for R2DTWO for sending and receiving traffic.
Its completely normal, if an interface only used for just sending or just receiving - it depends on the scenario we have.

```
[interfaces]
uni = eth iface=eno0
uni:streams = stream_uni
nni1 = eth iface=swp0
nni1::streams = stream_nni1
nni2 = eth iface=swp1
nni2:streams = stream_nni2
```

The interfaces usually described by two lines: the first one define the interface itself, the next one with the `:streams` suffix is list of streams, we are trying to identify.
Every received packet will be tested on the defined streams.
The matches of the listed streams (described in `streamname:match` line) tested on the packet sequentially, the first applicable match win, and the packet identified as part of that stream.
If none of the streams identified, the packet will be dropped.

__Important:__ R2DTWO receive a copy of each packet! This is not a problem in many cases, but keep in mind in that case only the copy dropped by R2DTWO and the original packet continue its way in the Linux network stack.

For each interface, we can define a meaningful custom name, will used in the rest of the config, type (in this case all of them `eth`), and their real names in Linux (`ip link`).
In this example we use the names `uni`, `nni1` and `nni2` since these properly referring to their roles: User-Network Interface (`uni`) and Network-Network Interface (`nni1` and `nni2`).

In the `r2dtwo.ini`'s `[interfaces]` section above we only have one stream candidate for each interface: `stream_uni`, `stream_nni1` and `stream_nni2`.
These streams defined in the `[streams]` section of the config see below:

```
[streams]
stream_uni:packet = eth, cvlan
stream_uni:match = cvlan vid=0
stream_uni:actions = seqgen, after cvlan add rtag, writeseq rtag, replicate tx_nni1, tx_nni2

tx_nni1 = edit cvlan.vid=100, send nni1
tx_nni2 = edit cvlan.vid=200, send nni2

stream_nni1:packet = eth, cvlan, rtag
stream_nni1:match = cvlan vid=100
stream_nni1:actions = readseq rtag, seqrcvy, del rtag, del cvlan, send uni

stream_nni2:packet = eth, cvlan, rtag
stream_nni2:match = cvlan vid=200
stream_nni2:actions = readseq rtag, seqrcvy, del rtag, del cvlan, send uni
```

Each stream can described with three lines in `streamname:suffix` format. The stream names are custom identifiers, while the suffixes are the following:

* `:packet` - the expected header structure in the frame. See the documentation for the supported headers.
* `:match` - the expected header field values in the frame. See the documentation for the supported fields and their format.
Matching of multiple headers and fields supported, but make sure all of them described int the `:packet` line!
* `:actions` - the action pipeline. This described the actions executed on each matching packet. Some actions can drop the packets! The last action usually the `send` which transmits the packet on the interface given as a parameter.
It is supported to split a longer pipeline up to multiple pipelines. Also, like in the example above, we can continue the execution of the pipeline with multiple copies of the packet on different pipelines.
For example the `replicate tx_nni1 tx_nni2` action above branching the pipeline and continue the execution with two copies of the same packet on the `tx_nni1` and `tx_nni2` pipelines:

```
                                                    tx_nni1
                                                   ┌────────────────┐
                                                   │                │
 stream_uni:actions                               ─┼─►edit──►send--*│
┌─────────────────────────────────────────────┐  / │                │
│                                             │ /  └────────────────┘
│*──►seqgen──►add rtag──►writeseq──►replicate─┼─
│                                             │ \  ┌────────────────┐
└─────────────────────────────────────────────┘  \ │                │
                                                  ─┼─►edit──►send--*│
                                                   │                │
                                                   └────────────────┘
                                                    tx_nni2
```

For the full list of the supported R2DTWO actions, their parameters and behavior please consult with the documentation.

Right now, the packets matching in `stream_uni` stream will be processed as described below as described in the `:actions` line:

0. The switch receive a packet on `eno0` interface, and since its an ethernet interface, R2DTWO apply a VLAN 0 tag (named as `cvaln` in the config) on it by default
1. The `seqgen` action gives a unique sequence number for each packet
2. After the VLAN tag R2DTWO insert the FRER Redundancy-tag (R-tag) header
3. The `writeseq` action insert the packet's sequence number to the R-tag's sequence field. __This is optional, R2DTWO smart enough to put the sequence number implicitly into the R-tag__
4. The `replicate` action do the packet copy and branches the action pipeline into two different paths
5. On both branches, there is an `edit` action set the VLAN ID-s to 100 an 200 then a `send` action which transmit the packets. Note that after the send action we can still process the packet further, however currently that is the last action in this particular config.

Lastly, there is an `[objects]` section.

```
[objects]
seqgen = SeqGen InitSeqStart=0
seqrcvy = SeqRcvy
```

In the action pipelines, there are stateful actions and those functionality goes beyond simple packet header manipulations.
For example in the example above, we have two objects, `seqgen` and `seqrecv` (those are custom names).
One of them is a __SeqGen__ instance which is a _sequence generation function_ described in section 7.4.1 of IEEE 802.1CB-2017.
This object has its inner state, like the next sequence number, etc. which is maintained across multiple packets.

Similarly, there is a __SeqRcvy__ instance called `seqrcvy` implements recovery function of IEEE 802.1CB-2017, see section 7.4.2.
This maintain a history window which tells if a received packet's sequence number already seen or not. If not, accept it, if already seen drop it.
For the dropped packets, the rest of the action pipeline not executed.


## Run the R2DTWO and generate traffic

Lets try out R2DTWO with this scenario.
For that we need at least three terminal window: one for generate traffic (`talker`) and two for run R2DTWO instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo su

source env.sh
```

If everything OK, the prompt should be changed to `(tsn test) root:scenario1# ` which tells right now we are in the test network environment.
Now we should have all the networking (nodes, interfaces and IP addresses) configured and helper commands to execute commands on the nodes.
To run a command on a node (e.g. `talker` or `nxp1`, etc.) just prefix the command with its name:

```
talker ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eth0@if2         UP             10.0.0.1/24 fe80::8c59:3ff:fe99:6204/64 

nxp1 ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eno0@if2         UP             fe80::60a6:45ff:febf:df30/64 
swp0@if2         UP             fe80::9449:e9ff:fe8e:4c7c/64 
swp1@if3         UP             fe80::5466:b5ff:fea0:c6fc/64 
```

Now we can start the R2DTWO instances on `nxp1` and `nxp2`:

```
# in one terminal:
nxp1 r2dtwo r2dtwo.ini

# in another terminal window:
nxp2 r2dtwo r2dtwo.ini
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
nxp1 tshark -l -O 'ieee8021cb' -i swp0  | grep -e 802 -e "10.0.0.1"

Running as user "root" and group "root". This could be dangerous.
Capturing on 'sw1p1'
802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 100
802.1CB Redundancy Tag, SEQ: 0
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
1 802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 100
802.1CB Redundancy Tag, SEQ: 1
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
2 802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 100
802.1CB Redundancy Tag, SEQ: 2
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
3 802.1Q Virtual LAN, PRI: 0, DEI: 0, ID: 100
802.1CB Redundancy Tag, SEQ: 3
Internet Protocol Version 4, Src: 10.0.0.1, Dst: 10.0.0.2
...
```

On the other path VLAN ID will be `200`, to investigate that start `tshark` as above but on the other NNI interface (`swp1`).

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before source a new environment in for a new test scenario!__
