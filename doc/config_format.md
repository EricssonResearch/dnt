
# Specification for the config file format of R2DTWO 6.0 and above

We use a simple INI format for the config. The INI format is not formally specified, and variations in syntax exist in the parser implementations. We expect the most basic variant: single-line `key=value` elements, any number of whitespace around the = sign, section headers with the `[name]` syntax, and we assume no ordering of the keys within a section. Comments start with '#' or ';' and last until the end of the line. Comments can appear at the end of data lines, but not after section headers. Multi-line strings or comments are not supported.

Our INI parser deviates from the usual INI format by treating the keys as case-sensitive. This is because originally it was designed to parse .desktop files, which is a [standardized format](https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html) and it is specified to be case-sensitive.

The format of the values given in the config file depend on the type of the parameter or the header field.  Regular numbers can be given in decimal, octal or hexadecimal form. Booleans can be given as true/false, yes/no, 1/0. Time values don't have units, they are always interpreted as milliseconds. Ethernet, IPv4 and IPv6 addresses must be given in their standard format.

## Sections

There are 4 sections in the config: *interfaces*, *streams*, *objects*, *oam*. The first two are mandatory. Each section can only be present once, their order is arbitrary.

The examples for each section together form a complete configuration for a DetNet scenario. For more examples see the [getting started](getting_started/README.md) directory.

## interfaces

List of interfaces where we can send/receive packets. The keys of the items are the names of the interfaces, actions refer to interfaces by their names, and not by the hardware interface names. The values of the items are in the form of `iftype parameter=value [parameter=value]`. The *iftype* is mandatory. The valid parameters depend on the *iftype* of the interface. The currently supported interface types with their parameters are the following (parameters without a default value are mandatory):

* `eth` Ethernet-level send/receive interface intended to be a TSN UNI or NNI, valid parameters:
    * `iface` the name of the hardware interface
* `ip` IP-level send/receive interface intended to be a DetNet UNI, valid parameters:
    * `iface` the name of the hardware interface
* `udp-in` receiving end of an UDP tunnel (intended for PseudoWire), cannot send packets, valid parameters:
    * `iface` the name of the hardware interface
    * `port` the UDP port to listen on (default: 6635)
    * `ipv` the IP version, can be 4 or 6 (default: 4)
    * `senders` used by the Dynamic IP feature (optional); a list of udp-out interfaces that send to this udp-in, the listed nodes will be notified about ip address changes on the receiving interface; must specify the `ip:port` of the OAM return interface (default `port` is 6634) and the name of the udp-out interface on those nodes (format: `ipv4,udpoutname,ipv4:port,udpoutname,ipv6,udpoutname,[ipv6]:port,udpoutname`)
* `udp-out` sending end of an UDP tunnel (intended for PseudoWire), cannot receive packets, valid parameters:
    * `iface` the name of the hardware interface
    * `srcport` the UDP source port of the sent packets (default: let Linux choose)
    * `dstport` the UDP port to send to (default: 6635)
    * `dstip` the IP address to send to, `ipv4` and `ipv6` mean the address will be specified later (can't send until a valid destination is set, see `senders` parameter on udp-in)
    * `prio` the IPv4 TOS or IPv6 Traffic Class for the sent packets (default: 0)
* `internal` a virtual interface within R2DTWO that can send and receive packets, useful for stream re-classification in decapsulating scenarios, no parameters
* `oam` receives OAM reply messages out-of-band, can have any number of these, the default one is the first one in alphabetic order
    * `ip` return address to listen on (required)
    * `port` return port (default: 6634)
* `oam_eth` receives Ethernet OAM reply messages out-of-band. Can have any number of these, the default one is the first one in alphabetic order. It will listen on all of them, but will use the default for sending.
    * `iface` the name of the hardware interface. It should be a VLAN interface, used for management VLAN
* `oam_cmd` OAM command interface, can have only one, use `telnet` to connect to it, use the `help` command
    * `ip` address (optional, accept incoming telnet connections on any address if not given)
    * `port` listening port (default: 8000)

Each interface can have an accompanying line with key `ifname:streams` that defines the streams received on that interface. The value for this key is a list of stream names separated by space. The ordering of the streams in this line determines the matching order when a received packet is processed. The interface drops all incoming packets if no streams are defined on it. One stream can be listed on multiple interfaces.

Each interface can have read-only properties that can be used as right-hand-side expressions in `edit` actions in the form of `ifname.property`. The currently supported properties are:

* eth: mac
* ip: srcip
* udp-in: srcip, port
* udp-out: dstip, srcport, dstport
* internal: (nothing)

The OAM interfaces never send/receive data plane traffic, and they have no readable properties.

The packets received on an `eth` interface always have a VLAN tag. If the incoming packet didn't have a 802.1Q or 802.1AD tag (`cvlan` and `svlan` in our terminology) after the Ethernet header, then the interface automatically adds a null `cvlan` header. This is meant to simplify the [packet matching](#match) rules, if it is not needed, it can be removed with a `del` [action](#actions).

Example for a DetNet scenario with Dynamic IP configuration:

```ini
[interfaces]

ifUNIin = eth iface=enp3s0
ifUNIout = ip iface=enp3s0
ifNNIin = udp-in iface=enp4s0 ipv=6 senders=fd01::1,ifNNIout2,192.168.10.11:6666,ifNNIout1
ifNNIout = udp-out iface=enp4s0 dstip=fd03::11

ifUNIin:streams = user_in
ifNNIin:streams = tunnel_in

cmd0 = oam_cmd
oam0 = oam ip=10.0.0.1
```

### Dynamic IP configuration

Dynamic IP address allocation is possible for UDP PseudoWire tunnels: when the receiving end of the UDP tunnel has dynamically allocated address (e.g. via DHCP, ICMPv6), R2DTWO can notify the sending endpoints about its address.

This implementation reuses the OAM framework for receiving and processing the address change notifications. On the UDP tunnel sender endpoint node, where the `udp-out` interface is, there has to be an `oam` interface to receive notifications, and it must be reachable from the node that has the `udp-in` interface.

On the UDP tunnel terminating endpoint the `senders` parameter of the `udp-in` interface lists the sender endpoints that must be notified. The value of this parameter is a comma-separated list of address and interface name pairs. The address must be the IP address (and port) of the `oam` interface on the sender endpoint. The interface name is the name of the `udp-out` interface on the sender endpoint that has to send traffic to this `udp-in` interface. There is no limit on the number of senders to be notified.

The `udp-out` interfaces can be configured to an initial target address, or just `ipv4` or `ipv6`, which means the target address will be known from notifications sent by the target `udp-in`. Note that the IP version of the tunnel cannot be changed dynamically.

## streams

This section lists the packet streams that R2DTWO can receive, and the actions that must be performed on the received packets. Each stream is defined with three lines: `packet`, `match`, `actions`. Their order in the config file is not important. The syntax for the key of the stream lines is in the form of `streamname:packet`, where `streamname` is used to find the corresponding definition lines for a stream. Missing one or two of these lines for a stream is an error.

### packet

This line specifies the expected header structure of the packet. The header list is separated by commas (,).

See [protocols.md](protocols.md) for the known protocols and their supported fields. This list is dynamically generated by a [script](protocolfields.pl) from the code in [protocol.c](../protocol.c) when `make doc` is issued.

The actions that manipulate the packet refer to the headers by their names. The name of the headers are their types by default. If the packet contains multiple headers of the same type, an alphanumeric suffix in the form of `headertype_identifier` can be used to distinguish them. The name of the header is the whole expression, the type is still the *headertype* part. The *identifier* strings are arbitrary. This distinguishing identifier can be omitted, if the headers of the same type are not referenced any action or match.

All headers must be specified in the `packet` line that are to be used in the `match` or `actions` line. The packet is only processed until the last header defined in the `packet` line, and the rest of the packet is considered `payload` that cannot be manipulated by the actions. If the last header before the `payload` specifies the type of the next header, then the type of the payload will be handled correctly when adding a new header just before the payload (the nextheader field of the new header will be set correctly), but no action can manipulate the `payload` directly.

Note that incoming packets are not guaranteed to contain the headers listed in the *packet* line. Field matching can be used to resolve the ambiguities (e.g. match on the *nextheader* field for the desired protocol type) before performing the stream matching.

### match

Separate from the packet structure definition, there is a line that specifies which header field values identify the stream.

Value matching for header fields is done with the following syntax: `headername fieldname=fieldvalue [fieldname=fieldvalue]`. Negative matching is also supported as `fieldname!=fieldvalue`. It is possible to match multiple fields of the same header, separated by space. The header names used in this line refer to the names assigned in the `packet` line. Match specifications for different headers are separated by commas (,). There can be no whitespace around the `=` and `!=` operators.

The matches for a stream are processed in the order they are given. The matches don't have to be in the order of the headers in the *packet* line.

If there is no matching stream for an incoming packet, it is dropped.

The `fieldvalue` in *match* follows the same rules as the constants in the assignments of the `edit` action, with one addition: prefix matching is supported on MAC, IPv4, IPv6 addresses with the format `address/prefix`.

### actions

This line specifies the processing actions that must be run on the received packet that matches the corresponding *packet* and *match* lines. The actions in the list are separated by commas (,), and they are executed in the given order.

The available actions are the following:

* `{before|after} header add newheader fieldname=fieldvalue` adds a new header of type `newheader` with the given field values at the given position relative to `header`
    * if `newheader` has a nextheader field, it will be filled with the correct type code automatically
    * if `newheader` has a sequence number field, a `writeseq` action is automatically inserted after the `add` action
    * if `newheader` has a timestamp field, a `writetstamp` action is automatically inserted after the `add` action
* `del header` removes the given header from the packet
* `delay delay [offload]` puts the packet in a delay buffer, where it will be kept until the specified *delay* milliseconds have passed since the timestamp value of the packet; the *delay* should be between 0 and 2000 ms and it can be a float value as well; there is an optional `offload` parameter; when `offload` is set, it will use an external delay mechanism provided by the OS, for example ETF qdisc; however if `offload` is set and ETF is not configured, no packets will be delayed; we assume that ETF qdisc is configured on the interface; the delay is influenced by the ETF's delta; the ETF qdisc will schedule its next wake-up time for the next packet's txtime minus the delta value; precision of the actual delay depends on the software configuration and the ETF hardware `offload` support; in hardware `offload` case the system clock and the network interface's clock must be synchronized
* `drop` unconditionally drops the packet; no further actions can be in the pipeline
* `edit header.fieldname=newvalue` changes the given field of the given header; multiple fields can be edited at once (separated by space), can edit multiple headers, can edit headers created by `add`, no space allowed around the `=` operator; the right-hand-side of the edit expression can be
    * constant:  format must be appropriate for the field type
        * numbers can be given in decimal, octal, hexadecimal (they are always unsigned)
        * 1 bit values can be 0/1, true/false, yes/no
        * IP/MAC addresses use their standard formatting (e.g. `ipv6.dst=fd00::1`, `ipv4.dst=1.2.3.4`, `eth.dmac=a:b:c:d:e:f`)
        * nextheader accepts numeric value or protocol name (the `add` action sets this field type automatically)
        * all the other types (ttl, tsnseq etc.) are considered to be numbers
    * another header field (can be in the same or in a different header)
    * an interface property (e.g. `ipv6.src=uniOut.srcip`)
* `eliminate seq_rec pipeline` conditional drop, uses the given sequence recovery object and the packet's sequence number metadata (see `readseq` action); after elimination the processing jumps to the given pipeline; no further actions can be in the pipeline
* `jump pipeline` continues the processing on the named pipeline, which has to be defined in the *streams* section, it does not return to the current pipeline; useful for breaking up long pipelines or reuse operations for multiple streams; no further actions can be in the pipeline; see the [naming convention](#naming-convention) guidelines
* `mep-start name level` Monitoring EndPoint, can initiate OAM messages
* `mep-stop name level [object]` Monitoring EndPoint, terminates an OAM monitoring route, can report status information about an object
* `mip name level [object]` Monitoring Intermediate Point, answers OAM messages, implicitly a mep-start point, can report status information about an object
* `pof pofobject` puts the packet in a reorder buffer based on its sequence number metadata, continues the actions on this pipeline when the ordering is okay
* `readseq header` reads the sequence number from the given header into the packet metadata field (to be used by the `eliminate` and `pof` actions)
* `readtstamp header` reads the timestamp from the given header into the packet metadata field (to be used by the `delay` action); initially the timestamp of the packet is the time it was received on the interface
* `replicate [object] pipeline1 [pipeline2]` makes copies of the packet and continues processing them on the given pipelines, which have to be defined in the *streams* section; this can create any number of branches; the first argument can optionally be the name of a Replicate object that stores statistics about the replication; no further actions can be in the pipeline
* `send iface` sends out the packet on the given interface from the *interfaces* list
* `seqgen generator` uses the given sequence generator object to set the sequence number metadata of the packet
* `ttlcheck` drops the packet if the metadata TTL is 0; see `ttlreduce` action
* `ttlreduce header` decreases the TTL in the given header, remembers the resulting value in a packet metadata field; see `ttlcheck` action
* `writeseq header` writes the sequence number from the packet metadata to the given header, the metadata has to contain a valid sequence number (from `seqgen` or `readseq`)
* `writetstamp header` writes the timestamp from the packet metadata to the given header

In these actions `header` refers to any header in the *packet* list by name, using the identifier suffix if there is one. The `newheader` in `add` can also have an identifier suffix. Later actions can refer to newly added headers by their names.

When the parameter of an action is a header field, it is given in the form `headername.fieldname`, except for the `add` action, which can only edit the fields of the newly added header. Some actions only need the header name, and they automatically select the correct field by its type (e.g. `readseq`).

It is possible to define action pipelines in the *streams* section that are not tied to streams received on interfaces, rather, they are referenced by `replicate`, `eliminate` and `jump` actions. The names of these pipelines can be arbitrary, but must be unique.

Some actions process the name of the pipeline they are in. When jumping to another pipeline (`replicate`, `eliminate` and `jump` actions) the name of the pipeline changes to the target of the jump. The `mip` actions that reside in pipelines with the same name (typically after jumping to the same pipeline from multiple places) are considered to be the same monitoring point.

For actions that use stateful objects the name of the action can be omitted, because the type of the object determines the type of the action. These actions are the following: `eliminate` (sequence recovery object), `pof` (pof object), `replicate` (replicate object), `seqgen` (sequence generator object). Similarly, the `jump` action can be used by only specifying the name of the action pipeline to jump to.

The Packet structure stores sequence number and timestamp properties (metadata fields). The `readseq` and `readtstamp` actions fill these properties from the given packet headers, and the `writeseq` and `writetstamp` actions write the metadata into the given header. The `seqgen` action generates the sequence number into the metadata field, and the `eliminate` action uses the sequence number in the metadata field.

The default timestamp metadata is the time the packet was received, the sequence number is initially undefined. The config is invalid, if `eliminate` or `writeseq` is used on undefined sequence number.

When adding a header that has *TSNSEQ* or *TSNTSTAMP* field type, an action that fills that field (`writeseq`/`writetstamp`) is automatically created.

If the first header in the `:packet` line has a *TTL* field, a `ttlreduce` action is automatically created for it at the beginning of the action pipeline. A `ttlcheck` action is also automatically created before the first `send` action, unless the header has been deleted. These actions can also be created manually.

The OAM Monitoring points (`mep-start`, `mep-stop`, `mip`) are only allowed in the action pipeline, where the header structure of the packet starts with `mpls` and `dcw`. The mpls label must be written *after* the start point that injects monitoring packets into the stream.

When the action pipeline is finished, the memory used for the packet is automatically reclaimed, there is no need to explicitly drop it with the `drop` action. The `drop` action can be used to explicitly filter out certain types of packets.

Example for a DetNet scenario:

```ini
[streams]

user_in:packet = eth, cvlan, ipv6
user_in:match = cvlan vid=13, ipv6 src=fd03::3 dst=fd42::/64
user_in:actions = del eth, del cvlan, seq_gen2, before ipv6 add dcw, before dcw add mpls bos=1 ttl=64, mep-start tunnelStart 3, edit mpls.label=13, send ifNNIout

tunnel_in:packet = mpls, dcw_tunnel, ipv6_user
tunnel_in:match = mpls label=42
tunnel_in:actions = readseq dcw_tunnel, seq_rcvy2, mep-stop tunnelEnd 3 seq_rcvy2, del mpls, del dcw_tunnel, edit ipv6_user.src=fd03::1, send ifUNIout
```

## objects

This section instantiates the stateful objects that implement TSN/DetNet functionalities. They can be referenced by name in the action pipelines of the streams. Each object type has a corresponding action that uses it. One object can be referenced by multiple actions, in that case they share their state.

The object instantiation is in this format: `name = type parameter=value [parameter=value]`. The valid parameter names and their valid values depend on the type of the object. The currently known object types are:

* `SeqGen` sequence number generator (for `seqgen` action)
    * `InitSeqFlag` use the Init flag for seamless mode (default: off)
    * `InitSeqStart` the starting sequence number (default: 0x8000)
    * `ResetFlag` use the Reset flag for seamless mode (default: off)
* `SeqRec` sequence number recovery (for `eliminate` action)
    * `frerSeqRcvyAlgorithm` can be Vector (default), SeamlessVector, Match
    * `frerSeqRcvyHistoryLength` size of the history window (default: 2)
    * `frerSeqRcvyLatentErrorPaths` elimination path count (default: 2)
    * `frerSeqRcvyResetMSec` timeout when no packet has been received (default: 2000)
    * `InitSeqFlag` whether to use the Init flag for seamless mode (default: off)
    * `ResetFlag` use the Reset flag for seamless mode (default: off)
    * `frerSeqRcvyLatentErrorPeriod` run latent error check and root cause detection (default: 0)
    * `frerSeqRcvyLatentResetPeriod` reset latent error and root cause related counters (default: 0)
    * `frerSeqRcvyLatentErrorDifference` do not treat packet drops below this threshold as loss in one period (default: 0)
    * `frerSeqRcvyOutageThreshold` above that threshold consecutive packet drops reported as burst loss/path outage (default: 0)
    * `AutoMIP=level` enables automatic `mip` generation for the eliminate action with the level given
* `Pof` packet ordering function (for `pof` action)
    * `BufferSize` max number of packets in the reorder buffer (default: 2)
    * `MaxDelay` timeout when waiting for missing packet (default: 20)
    * `TakeAnyTime` initial time for sequencing (default: 2000)
* `Replicate` counters for `replicate` action, takes no parameters
    * `AutoMIP=level` enables automatic `mip` generation for the replicate action with the level given

All of these objects work on the metadata of the packet instead of header fields.

Example for a DetNet scenario:

```ini
[objects]

seq_gen2 = SeqGen
seq_rcvy2 = SeqRcvy frerSeqRcvyAlgorithm=Vector frerSeqRcvyHistoryLength=1993
```

## oam

This optional section instantiates OAM commands.
The format of the commands is `name = command with parameters`, where the command is in the same format as it would be given on the CLI.
Refer to the [oam documentation](oam.md) for reference on the commands.

The currently supported OAM commands in this section are:

* `ping` sends ping requests in the background indefinitely, the `-n` argument is not allowed

Example for a DetNet scenario:

```ini
[oam]
global_connectivity_check = ping mepn1s1 any 4 -r -o
```
