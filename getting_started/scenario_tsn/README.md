# Scenario TSN: R2DTWO TSN operation, layer 2 bridge mode

In the following, we will use R2DTWO as a Layer2 TSN switch with IEEE 802.1CB function.

R2DTWO can protect the traffic by using redundant network paths simultaneously.
Every packet is duplicated (or more generally replicated, since it can support more than two paths) to the network paths, called _replication_.
The receiving node only accepts the first copy of the packet and drops the rest, that's called _elimination_.

We will use the following topology, which consists:

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
│  eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0   │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          swp1   ─┼─────────┼─  swp1           │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```
As you can see, there are redundant paths between **nxp1** and **nxp2**.
These paths will be utilized by R2DTWO for redundancy.

## The R2DTWO configuration

In the first scenario, we try out R2DTWO's IEEE 802.1CB FRER implementation.
This utilizes the FRER Redundancy Tag header with sequence numbers and operates in Layer 2.

The configuration file for R2DTWO is prepared in this folder: `r2dtwo.ini`.
This is a symmetrical configuration, reflecting to the network topology above.
In cases where the network setup is not symmetrical, separate configs might require on each switch.

The FRER standard uses the term stream for packets with common header field values of interest.
For example, a packet with a VLAN ID of 10 can be referred to as a stream.
After the packet is identified as part of the stream, actions are performed sequentially on the packet.

A replication action makes a copy of the packets that can be sent on multiple interfaces.
Before sending them, we can modify the packet headers, for example, set different VLAN IDs for packets sent on one path or the other.
The packets sent on the different paths are called _member streams_ by the standard.
The original stream is called a _compound stream_.

A compound stream can be split into any number of member streams.
However, on the listener side, we need to identify each member stream to merge them into one compound stream.
For this we also need an elimination action.

For the rest of this guide, we will stick to the terminology of member streams and compound streams.
Since the stream names are defined by the user in R2DTWO, meaningful names can make it clearer which stream is member and which is compound.

### Explanation of the configuration

For full details, please take a look into the R2DTWO documentation.

Right now we are only explaining the actions in the `r2dtwo.ini` file.
Like other R2DTWO configurations, this consists three main sections: `[interfaces]`, `[objects]` and `[streams]`.

The `[interfaces]` section describes the network interfaces for R2DTWO for sending and receiving traffic.
It's completely normal, if an interface is only used for just sending or just receiving - it depends on the scenario we have.

```
[interfaces]
uni = eth iface=swp2
uni:streams = compound_uni
nni1 = eth iface=swp0
nni1:streams = member1_uni
nni2 = eth iface=swp1
nni2:streams = member2_uni
```

The interfaces are usually described with two lines: the first one define the interface itself, and the next one with the `:streams` suffix is a list of streams, we are trying to identify.
Every received packet will be tested on the defined streams.
The matches of the listed streams (described in `streamname:match` line) tested on the packet sequentially, the first applicable match win, and the packet identified as part of that stream.

__If none of the streams are identified, the packet will be dropped!__

__Important:__ R2DTWO receives a copy of each packet! This is not a problem in many cases, but keep in mind in that case only the copy dropped by R2DTWO and the original packet continue its way in the Linux network stack.

For each interface, we can define a meaningful custom name, that will used in the rest of the config, type (in this case all of them `eth`), and their real names in Linux (`ip link`).
In this example we use the names `uni`, `nni1`, and `nni2` since these properly refer to their roles: User-Network Interface (`uni`) and Network-Network Interface (`nni1` and `nni2`).

In the `r2dtwo.ini`'s `[interfaces]` section above we only have one stream candidate for each interface: `compound_uni`, `member1_uni` and `member2_uni`.
As one can guess from these names, we identify the packets received from the `uni` interface as the compound stream.
These streams defined in the `[streams]` of the config see below:

```
[streams]
compound_uni:packet = eth, cvlan
compound_uni:match = cvlan vid=0
compound_uni:actions = Gen, after cvlan add rtag, writeseq rtag, Repl Repl-member1 Repl-member2

Repl-member1 = edit cvlan.vid=100, send nni1
Repl-member2 = edit cvlan.vid=200, send nni2

member1_uni:packet = eth, cvlan, rtag
member1_uni:match = cvlan vid=100
member1_uni:actions = readseq rtag, Elim Elim-compound

member2_uni:packet = eth, cvlan, rtag
member2_uni:match = cvlan vid=200
member2_uni:actions = readseq rtag, Elim Elim-compound

Elim-compound = del rtag, del cvlan, send uni
```

Each stream can described with three lines in `streamname:suffix` format. The stream names are custom identifiers, while the suffixes are the following:

* `:packet` - the expected header structure in the frame. See the documentation for the supported headers.
* `:match` - the expected header field values in the frame. See the documentation for the supported fields and their format.
Matching of multiple headers and fields supported, but make sure all of them are described in the `:packet` line!
* `:actions` - the action pipeline. This described the actions executed on each matching packet. Some actions can drop the packets! The last action is usually the `send` which transmits the packet on the interface given as a parameter.
It is supported to split a longer pipeline up to multiple pipelines.
Also, like in the example above, we can continue the execution of the pipeline with multiple copies of the packet on different pipelines.
For example the `Repl Relp-member1 Repl-member2` action above branching the pipeline and continue the execution with two copies of the same packet on the `Repl-member1` and `Repl-member2` pipelines:

```
                                                    Repl-member1
                                                   ┌────────────────┐
                                                   │                │
 compound_uni:actions                             ─┼─►edit──►send--*│
┌─────────────────────────────────────────────┐  / │                │
│                                             │ /  └────────────────┘
│*──►seqgen──►add rtag──►writeseq──►Repl     ─┼─
│                                             │ \  ┌────────────────┐
└─────────────────────────────────────────────┘  \ │                │
                                                  ─┼─►edit──►send--*│
                                                   │                │
                                                   └────────────────┘
                                                    Repl-member2
```

Similarly the `Elim Elim-compound` tells "continue the execution of the `Elim-compound` pipeline after the elimination".
Its mandatory to have a common pipeline after the elimination action:

```
 member1_uni:actions
┌─────────────────────────────────────────┐
│                                         │
│●───▶readseq rtag────▶Elim Elim-compound─┼──┐  Elim-compound
│                                         │  │ ┌─────────────────────────────────────┐
└─────────────────────────────────────────┘  │ │                                     │
                                             ├─┼─▶ del rtag───▶del cvlan───▶send uni │
┌─────────────────────────────────────────┐  │ │                                     │
│                                         │  │ └─────────────────────────────────────┘
│●───▶readseq rtag────▶Elim Elim-compound─┼──┘
│                                         │
└─────────────────────────────────────────┘
 member2_uni:actions
```

For the full list of the supported R2DTWO actions, their parameters, and behavior please consult with the documentation.

Right now, the packets matching in `compound_uni` stream will be processed as described below as described in the `:actions` line:

0. The switch receives a packet on `swp2` interface, and since its an ethernet interface, R2DTWO applies a VLAN 0 tag (named as `cvlan` in the config) on it by default
1. The `Gen` action gives a unique sequence number for each packet. Here `Gen` is a short form of `seqgen Gen`, where the action type is deduced by the type of the object argument.
2. After the VLAN tag R2DTWO insert the FRER Redundancy-tag (R-tag) header
3. The `writeseq` action inserts the packet's sequence number to the R-tag's sequence field. __This is optional, R2DTWO is smart enough to put the sequence number implicitly into the R-tag__
4. The `Repl` action do the packet copy and branches the action pipeline into two different paths. There is a stateless version of that action, it would looks like this: `replicate Repl-member1 Repl-member2`. The name of the pipelines are arbitrary.
5. On both branches, there is an `edit` action set the VLAN ID-s to 100 an 200 then a `send` action that transmit the packets.
Note that after the send action we can still process the packet further however, currently that is the last action in this particular config.
Some actions such as `jump`, `eliminate` or `drop` forbids further actions on that pipeline.

Lastly, there is an `[objects]` section.

```
[objects]
Repl = Replicate
Gen = SeqGen InitSeqStart=0
Elim = SeqRcvy
```

In the action pipelines, there are stateful actions and that functionality goes beyond simple packet header manipulations.
For example in the example above, we have three objects, `Repl`, `Elim` and `Gen` (those are custom names).
One of them is a __SeqGen__ instance which is a _sequence generation function_ described in section 7.4.1 of IEEE 802.1CB-2017.
This object has its inner state, like the next sequence number, etc. which is maintained across multiple packets.

Similarly, there is a __SeqRcvy__ instance called `Elim` that implements recovery function of IEEE 802.1CB-2017, see section 7.4.2.
This maintains a history window that tells if a received packet's sequence number has already been seen or not.
If not, accept it, if already seen drop it.
For the dropped packets, the rest of the action pipeline (in this configuration, the `Elim-compound`) is not executed.

The `Repl` object is a stateful __Replicate__ instance.
The state of this object is the number of the replicated packets.
While in this particular config that information not used for anything, its useful with the OAM function (not covered here).
If that information not required, one can simply use the stateless `replicate` action, and for that no object definition required.

The stream and pipeline names are arbitrary in the configuration, but the recommended naming convention for the pipelines is the __ObjectName-PipelineName__.
Here the __ObjectName__ is the name of the object after the pipeline starts, like `Elim` or `Repl`.
The __PipelineName__ is recommended to be meaningful about the stream.
According to this conventions, our pipelines are named as `Repl-member1`, `Repl-member2` (after the replication) and `Elim-compound` (the common actions for the two member streams or simply the actions of the compound stream).


## Run the R2DTWO and generate traffic

Let's try out R2DTWO with this scenario.
For that, we need at least three terminal windows: one for generate traffic (`talker`) and two for run R2DTWO instances on `nxp1` and `nxp2`.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo -s

source env.sh
```

If everything OK, the prompt should be changed to `(tsn test) root:scenario_tsn# ` which tells right now we are in the test network environment.
Now we should have all the networking (nodes, interfaces and IP addresses) configured and helper commands to execute commands on the nodes.
To run a command on a node (e.g. `talker` or `nxp1`, etc.) just prefix the command with its name:

```
talker ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eth0@if2         UP             10.0.0.1/24 fe80::8c59:3ff:fe99:6204/64 

nxp1 ip -br a
lo               UNKNOWN        127.0.0.1/8 ::1/128 
swp2@if2         UP             fe80::60a6:45ff:febf:df30/64 
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

__Note:__ for more recent tshark version (after 4.1) use `rtag` filter instead `ieee8021cb`.

On the other path VLAN ID will be `200`, to investigate that start `tshark` as above but on the other NNI interface (`swp1`).

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__

