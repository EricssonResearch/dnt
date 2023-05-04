# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

This guide only show how to compile and run R2DTWO in a very simple network environment.
**This guide for advanced users, who wants to understand the configuration of a virtual test sandbox network for R2DTWO!**
That means we configure the whole networking by hand, building a virtual sandbox network for our experiments.
__If you only want to experiment with R2DTWO, not interested in the network config, please check__ [scenario1's README.md](README.md)
Otherwise, the two guide very similar, since the building, installation and configuration of R2DTWO is the same, regardless of the test environment.

## Requirements

R2DTWO currently only supports GNU/Linux environments.
A fairly up-to-date GNU/Linux distribution like Ubuntu, Debian, Fedora, RHEL, Arch, etc. should works.
Install the dependencies for the compilation and to run the project.

On Ubuntu/Debian based distros:
```
sudo apt install build-essential iproute2 wireshark
```
On Fedora/RHEL:
```
sudo dnf groupinstall @development-tools @development-libraries
```

We use `veth` virtual ethernet interfaces in this guide, make sure its supported by your kernel

```
grep VETH /boot/config-$(uname -r)
CONFIG_VETH=m
```
If `CONFIG_VETH` is `=m` or `=y` that means your kernel has `veth` support.
If `veth` not supported, consider switch to a recent major GNU/Linux distro.

__If you running R2DTWO on real network, e.g. on the NXP boards, veth support not required.__

## Compilation and install of R2DTWO

The compilation of the project can be done with a `make` command

```
cd r2dtwo
make

# verify if r2dtwo executable successfully created
file r2dtwo 
r2dtwo: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, with debug_info, not stripped
```

_Optional_: to run `r2dtwo` from any working directory, consider installing it to system wide.
To do that, execute the following

```
sudo cp r2dtwo /usr/local/bin/

# verify if r2dtwo installed on the system
cd $HOME
r2dtwo
R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation
Version 6.0
usage: r2dtwo configfile
```

# Test R2DTWO in a simple emulated network environment

## Prepare the test namespace and execute commands in that namespace

In the following, we create a Linux network namespace for our tests.
That's intended to separate every network configuration and traffic from our host's normal networking.
This helps not to interfere with the host networking and also helps to clean up everything after the testing.
For simplicity, we call our network namespace as `r2` in this guide. Create it with the command below:

```
sudo ip netns add r2
```

Normally every shell command runs in the host namespace.
In order to execute a command in that new `r2` namespace, we have to do it explicitly:

```
sudo ip netns exec r2 ip link show
1: lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

Right now we only have one loopback interface named `lo` in the `r2` network, created by the kernel automatically.
For convenience, we create an alias command to run other commands inside the `r2` namespace:

```
alias r2exec="ip netns exec r2"
r2exec ip link
1: lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

## Create the network topology

R2DTWO can protect the traffic by using redundant network paths simultaneously.
Every packet duplicated (or more generally replicated, since it can supports more than two paths) to the network paths, called _replication_.
The receiving node only accept the first copy of the packet and drop the rest, that's called _elimination_.

We will create the following topology, which consist:
* a talker node called **talker** which will generate traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO switches, called **sw1** and **sw2** switching the traffic between the __talker__ and __listener__.
Normally, without virtual ethernet interfaces this would require 4 physical machines and 8 NICs.
```
  talker               sw1                         sw2              listener
┌────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌─────────┐
│        │    │                  │         │                  │    │         │
│        │    │          sw1p1  ─┼─────────┼─  sw2p1          │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│ teth0 ─┼────┼─ sw1p0           │         │           sw2p0 ─┼────┼─ leth0  │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          sw1p2  ─┼─────────┼─  sw2p2          │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```
As you can see, there are redundant paths between **sw1** and **sw2**.
These paths will utilized by R2DTWO for redundancy.

Note that, the nodes __talker__, __listener__, __sw1__ and __sw2__ configured implicitly by the interface configs and R2DTWO configs.

Lets create the topology with the following commands.
```
sudo r2exec ip link add teth0 type veth peer name sw1p0
sudo r2exec ip link add leth0 type veth peer name sw2p0
sudo r2exec ip link add sw1p1 type veth peer name sw2p1
sudo r2exec ip link add sw1p2 type veth peer name sw2p2
```
After their creation, the `veth` interfaces (and the `lo` loopback) all disabled.
To turn them all on, we can execute the following commands:

```
sudo r2exec ip link set teth0 up
sudo r2exec ip link set leth0 up
sudo r2exec ip link set sw1p0 up
sudo r2exec ip link set sw1p1 up
sudo r2exec ip link set sw1p2 up
sudo r2exec ip link set sw2p0 up
sudo r2exec ip link set sw2p1 up
sudo r2exec ip link set sw2p2 up
sudo r2exec ip link set lo up
```

## Scenario #1: R2DTWO TSN operation, layer 2 bridge mode

In the first scenario, we try out R2DTWO's IEEE 802.1CB FRER implementation.
This utilize the FRER Redundancy Tag header with sequence numbers and operates in Layer 2.

The configuration files for R2DTWO prepared in the `scenario1` folder: `sw1.ini` and `sw2.ini`.
These are symmetrical configurations, reflecting the to the network topology above.
That means the __talker__ and __listener__ can communicate both directions with each other, so we can test the operation with `ping` tool.

### Explanation of the configuration

For full details, please take a look into the R2DTWO documentation.
Right now we are only explaining the `sw1.ini` file.
Like other R2DTWO configurations, this consists three main sections: `[interfaces]`, `[objects]` and `[streams]`.

The `[interfaces]` describes the network interfaces for R2DTWO for sending and receiving traffic.
Its completely normal, if an interface only used for just sending or just receiving - it depends on the scenario we have.

```
[interfaces]
uni = eth iface=sw2p0
uni:streams = stream_uni
nni1 = eth iface=sw2p1
nni1::streams = stream_nni1
nni2 = eth iface=sw2p2
nni2:streams = stream_nni2
```

The interfaces usually described by two lines: the first one define the interface itself, the next one with the `:streams` suffix is a list of streams, we are trying to identify.
For each interface, we can define a meaningful custom name, will used in the rest of the config, type (in this case all of them `eth`), and their real names in Linux (`ip link`).
In this example we use the names `uni`, `nni1` and `nni2` since these properly referring to their roles: User-Network Interface (`uni`) and Network-Network Interface (`nni1` and `nni2`).
The matches of the listed streams (described in `streamname:match` line) applied to the packet sequentially, the first applicable match win, and the packet identified as part of that stream.
If none of the streams identified, the packet will be dropped.

In the `sw1.ini`'s `[interfaces]` section above we only have one stream candidate for each interface: `stream_uni`, `stream_nni1` and `stream_nni2`.
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

Lastly, there is a `[objects]` section.

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

The `sw2.ini` is exactly the same, however in the interfaces referring to `sw2p0`, `sw2p1` and `sw2p2` accordingly.

### Run the R2DTWO and generate traffic

Lets try out R2DTWO with this scenario. For that we need two more terminal window: one for generate traffic (`talker`) and two for run R2DTWO instances `sw1` and `sw2`.
In one terminal, run `r2dtwo` with the config file `scenario1/sw1.ini`, and in other `sw2.ini`:

```
# in one terminal:
sudo r2exec r2dtwo scenario1/sw1.ini

# in another terminal window:
sudo r2exec r2dtwo scenario1/sw2.ini
```

If the configuration steps executed correctly, the `r2dtwo` instances are up and running.
But we have to generate some traffic right now with `ping` so lets configure IP addresses to the talker and listener:

```
sudo r2exec ip addr add 10.0.0.1/24 dev teth0
sudo r2exec ip addr add 10.0.0.2/24 dev leth0
```

Also enable accepting traffic with local IP address on the interfaces, since its disabled by default:

```
sudo r2exec sysctl -w net.ipv4.conf.teth0.accept_local=1
sudo r2exec sysctl -w net.ipv4.conf.leth0.accept_local=1
```

Now start a ping, from talker `10.0.0.1` to listener `10.0.0.2`.
__Important!__ We have to give `ping` the source interface, without that Linux looks into the local rooting table of the destination interface, and don't route the packet, just respond it like it does for loopback addresses.

```
sudo r2exec ping -I teth0 -c 4 10.0.0.2
PING 10.0.0.2 (10.0.0.2) from 10.0.0.1 teth0: 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=0.319 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=0.170 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=0.201 ms
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=0.198 ms

--- 10.0.0.2 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3050ms
rtt min/avg/max/mdev = 0.170/0.222/0.319/0.057 ms
```

To observe the VLAN and R-tags on the __NNI__ interface pairs (`sw1p1 - sw2p1` and `sw1p2 - sw2p2`) start `tshark` in a separate terminal while pinging:

```
sudo r2exec tshark -l -O 'ieee8021cb' -i sw1p1 | grep -e 802 -e "10.0.0.1"

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

## Cleanup

Since every network configuration used in this guide sandboxed by the network namespace, its enough to delete that to clean up the interfaces and sysctl settings.
(__Run the command outside of the namespace, dont use r2exec__)
```
sudo ip netns del r2
```
