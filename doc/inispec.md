
# Specification for the config file format of R2DTWO 6.0 and above

We use a simple INI format for the config. The INI format is not formally specified, and variations in syntax exist in the parser implementations. We expect the most basic variant: single-line key=value elements, any number of whitespace around the = sign, section headers with the [name] syntax, and we assume no ordering of the keys within a section. Comments start with '#' or ';' and last until the end of the line. Multi-line strings or comments are not supported.

Our INI parser deviates from the usual INI format by treating the keys as case-sensitive. This is because originally it was designed to parse .desktop files, which have a standardized format and it is specified to be case-sensitive.

The format of the values given in the config file depend on the type of the parameter or the header field.  Regular numbers can be given in decimal or hexadecimal form. Booleans can be given as true/false, yes/no, 1/0. Time values don't have units, they are always interpreted as milliseconds. Ethernet, IPv4 and IPv6 addresses must be given in their usual form.

## Sections

There are 4 sections in the config: *interfaces*, *streams*, *objects*, *oam*. The first two are mandatory. Each section can only be present once, their order is arbitrary.

## interfaces

List of interfaces where we can send/receive packets. The keys of the items are the names of the interfaces, actions refer to interfaces by their names, and not by the hardware interface names. The values of the items are in the form of `iftype parameter=value [parameter=value]`. The *iftype* is mandatory. The valid parameters depend on the *iftype* of the interface. The currently supported interface types with their parameters are the following:

* `eth` raw ethernet interface intended to be a TSN UNI, valid parameters:
    * `iface` the name of the hardware interface
* `ip` outging UNI for IP-over-DetNet (cannot receive packets), valid parameters:
    * `iface` the name of the hardware interface
* `udp-in` receiving end of an UDP tunnel (cannot send packets), valid parameters:
    * `iface` the name of the hardware interface
    * `port` the UDP port to listen on (default: 6635)
    * `ipv` the IP version, can be 4 or 6 (default: 4)
* `udp-out` sending end of an UDP tunnel (cannot receive packets), valid parameters:
    * `iface` the name of the hardware interface
    * `srcport` the UDP source port of the sent packets (default: let Linux choose)
    * `dstport` the UDP port to send to (default: 6635)
    * `dstip` the IP address to send to (also determines the IP version, domain names are also accepted)
    * `prio` the IPv4 TOS or IPv6 Traffic Class for the sent packets (default: 0)
* `internal` a virtual interface within R2DTWO, useful for stream re-classification in decapsulating scenarios, no parameters
* `oam` receives OAM reply messages out-of-band
    * `ip` return address (required)
    * `port` return port (default: 6634)
* `oam_cmd` OAM command interface, use telnet to connect to it, use the `help` command
    * `ip` address (optional)
    * `port` listening port (default: 8000)

Each interface has an accompanying line with key `ifname:streams` that defines the streams received on that interface. The value for this key is a list of stream names separated by space. The ordering of the streams in this line determines the matching order when a received packet is processed. The interface drops all incoming packets if no streams are defined on it. One stream can be listed on multiple interfaces.

Each interface can have read-only properties that can be used as right-hand-side expressions in edit actions in the form of `ifname.property`. The currently supported properties are:

* eth: mac
* ip: srcip
* udp-in: srcip, port
* udp-out: dstip, srcport, dstport
* internal: (nothing)

The OAM interfaces never send/receive data plane traffic, and they have no readable properties.

Example for a DetNet scenario:

```
[interfaces]

ifUNIin = eth iface=enp3s0
ifUNIout = ip iface=enp3s0
ifNNIin = udp-in iface=enp4s0 ipv=6
ifNNIout = udp-out iface=enp4s0 dstip=fd03::11

ifUNIin:streams = user_in
ifNNIin:streams = tunnel_in

cmd0 = oam_cmd oam_cmd_ip=10.0.0.1 oam_cmd_port=8000
oam0 = oam oam_ip=10.0.0.1 oam_port=6634
```

## streams

This section lists the packet streams that R2DTWO can receive, and the actions that must be performed on the received packets. Each stream is defined with three lines: `match`, `packet`, `actions`. Their order in the config file is not important. The syntax for the key of the stream lines is in the form of `streamname:packet`, where `streamname` is used to find the corresponding definition lines for a stream. Missing one or two of these lines for a stream is an error.

### packet

This line specifies the expected header structure of the packet. The header list is separated by commas (,).

The currently known header types are:

* eth
* svlan
* cvlan
* rtag
* ttag
* ipv4
* ipv6
* arp
* udp
* mpls
* dcw
* tcw

The actions that manipulate the packet refer to the headers by their names. The name of the headers are their types by default. If the packet contains multiple headers of the same type, an alphanumeric suffix in the form of `headertype_identifier` can be used to distinguish them. The name of the header is the whole expression, the type is still the *headertype* part. The *identifier* strings are arbitrary. This distinguishing identifier can be omitted, if the duplicate headers are not referenced any action or match.

All headers must be specified in the `packet` line that are to be used in the `match` or `actions` line. The packet is only processed until the last header defined in the `packet` line, and the rest of the packet is considered `payload` that cannot be manipulated by the actions. If the last header before the `payload` specifies the type of the next header, then the type of the payload will be handled correctly (e.g. when adding a new header just before the payload, and setting the nextheader field of the header), but no action can manipulate the `payload` directly.

### match

Separate from the packet structure definition there is a line that specifies which header field values identify the stream.

Value matching for header fields is done with the following syntax: `headername fieldname=fieldvalue [fieldname=fieldvalue]`. It is possible to match multiple fields of the same header, separated by space. The header names used in this line refer to the names assigned in the `packet` line. Match specifications for different headers are separated by commas (,).

The matches for a stream are processed in the order they are given.
TODO or in the order of the protocol stack?

If there is no matching stream for an incoming packet, the action is `drop`.

### actions

This line specifies the processing actions that must be run on the received packet that matches the corresponding *packet* and *match* lines. The actions in the list are separated by commas (,), and they are executed in the given order.

The available actions are the following:

* `{before|after} header add newheader fieldname=fieldvalue` adds a new header of type `newheader` with the given field values at the given position
    * if the header has a nextheader field, it will be filled with the correct type code automatically
    * if the header has a sequence number field, a `writeseq` action is automatically inserted after the `add` action
    * if the header has a timestamp field, a `writetstamp` action is automatically inserted after the `add` action
* `del header` removes the given header from the packet
* `delay delay` puts the packet in a delay buffer, where it will be kept until the specified *delay* milliseconds have passed since the timestamp value of the packet
* `drop` unconditionally drops the packet; this action is the last one in a pipeline
* `edit header.fieldname=newvalue` changes the given field of the given header; multiple fields can be edited at once, separated by space, can edit multiple headers, can edit headers created by `add`; the right-hand-side of the edit expression can be
    * constant
    * another header field
    * an interface property
    * these expressions are validated against the size and type of the header field on the left-hand-side
* `eliminate seq_rec` conditional drop, uses the given sequence recovery object and the packet's sequence number metadata
* `jump pipeline` continues the processing on the named pipeline, which has to be defined in the *streams* section, it does not return to the current pipeline; useful for breaking up long pipelines or reuse operations for multiple streams; this action is the last one in a pipeline
* `mep-start name level [object]` Monitoring EndPoint, can initiate OAM messages, can report status information about an object
* `mep-stop name level [object]` Monitoring EndPoint, terminates an OAM monitoring route, can report status information about an object
* `mip name level [object]` Monitoring Intermediate Point, answers OAM messages, implicitly a mep-start point, can report status information about an object
* `pof pofobject` puts the packet in a reorder buffer based on its sequence number metadata, continues the actions on this pipeline when the ordering is okay
* `readseq header` reads the sequence number from the given header into the packet metadata field (to be used by the `eliminate` and `pof` actions)
* `readtstamp header` reads the timestamp from the given header into the packet metadata field (to be used by the `delay` action)
* `replicate pipeline1 [pipeline2]` makes copies of the packet and continues processing them on the given pipelines, which have to be defined in the *streams* section, this can create any number of branches; the first argument can optionally be the name of a Replicate object that stores statistics about the replication; this action is the last one in a pipeline
* `send iface` sends out the packet on the given interface from the *interfaces* list
* `seqgen generator` uses the given sequence generator object to set the sequence number metadata of the packet
* `ttlcheck` drops the packet if the metadata TTL is 0
* `ttlreduce header` decreases the TTL in the given header, remembers the value in a packet metadata field
* `writeseq header` writes the sequence number from the packet metadata to the given header, the metadata has to contain a valid sequence number (from `seqgen` or `readseq`)
* `writetstamp header` writes the timestamp from the packet metadata to the given header

In these actions `header` refers to any header in the *packet* list by name, using the identifier suffix if there is one. The `newheader` in `add` can also have an identifier suffix. Later actions can refer to newly added headers.

It is possible to define action pipelines in the *streams* section that are not tied to streams, rather, they are referenced by `replicate` and `jump` actions. The names of these pipelines can be arbitrary, but must be unique.

When the parameter of an action is a header field, it is given in this form: `headername.fieldname`, some actions only need the header name, and they automatically select the correct field by its type.

For actions that use stateful objects the name of the action can be omitted, because the type of the object determines the action. These actions are the following: eliminate (sequence recovery object), pof (pof object), seqgen (sequence generator object). Similarly, the `jump` action can be used by only specifying the name of the action pipeline to jump to.

When adding a header that has *TSNSEQ* or *TSNTSTAMP* field, an action that fills that field (`writeseq`/`writetstamp`) is automatically created. The default timestamp is the time the packet was received.

If the first header in the `:packet` line has a TTL field, a `ttlreduce` action is automatically created for it at the beginning of the action pipeline. A `ttlcheck` action is also automatically created before the first `send` action, unless the header has been deleted.

The OAM Monitoring points (`mep-start`, `mep-stop`, `mip`) are only allowed in the action pipeline, where the header structure of the packet starts with `mpls` and `dcw`. The mpls label must be written *after* the start point.

When the action pipeline is finished, the memory used for the packet is automatically reclaimed, there is no need to explicitly drop it with the `drop` action. The `drop` action can be used to explicitly filter out certain types of packets.

Example for a DetNet scenario:

```
[streams]

user_in:packet = eth, cvlan, ipv6
user_in:match = ipv6.dst=fd03::42
user_in:actions = del eth, del cvlan, seq_gen2, before ipv6 add dcw, before dcw add mpls bos=1 ttl=64, mep-start tunnelStart 3, edit mpls.label=13, send ifNNIout

tunnel_in:packet = mpls, dcw, ipv6
tunnel_in:match = mpls.label=42
tunnel_in:actions = readseq dcw, seq_rcvy2, mep-stop tunnelEnd 3 seq_rcvy2, del mpls, del dcw, send ifUNIout
```

## objects

This section instantiates the stateful objects that implement TSN/DetNet functionalities. They can be referenced by name in the action pipelines of the streams. Each object type has a corresponding action that uses it. One object can be referenced by multiple actions, in that case they share their state.

The object instantiation is in this format: `name = type parameter=value [parameter=value]`. The valid parameter names and their valid values depend on the type of the object. The currently known object types are:

* `SeqGen` sequence number generator (for `seqgen` action)
    * InitSeqFlag use the Init flag for seamless mode (default: off)
    * InitSeqStart the starting sequence number (default: 0x8000)
    * ResetFlag use the Reset flag for seamless mode (default: off)
* `SeqRec` sequence number recovery (for `eliminate` action)
    * frerSeqRcvyAlgorithm can be Vector (default), SeamlessVector, Match
    * frerSeqRcvyHistoryLength size of the history window (default: 2)
    * frerSeqRcvyLatentErrorPaths elimination path count (default: 2)
    * frerSeqRcvyResetMSec silence detection timout (default: 2000)
    * InitSeqFlag whether to use the Init flag for seamless mode (default: off)
    * ResetFlag use the Reset flag for seamless mode (default: off)
    * frerSeqRcvyLatentErrorPeriod run latent error check and root cause detection (default: 0)
    * frerSeqRcvyLatentResetPeriod reset latent error and root cause related counters (default: 0)
    * frerSeqRcvyLatentErrorDifference do not treat packet drops below this threshold as loss in one period (default: 0)
    * frerSeqRcvyOutageThreshold above that threshold consecutive packet drops reported as burst loss/path outage (default: 0)
* `Pof` packet ordering function (for `pof` action)
    * BufferSize max number of packets in the reorder buffer (default: 2)
    * MaxDelay timeout when waiting for missing packet (default: 20)
    * TakeAnyTime initial time for sequencing (default: 2000)
* `Replicate` counters for `replicate` action, takes no parameters

All of these objects work on the metadata of the packet instead of the header fields.

Example for a DetNet scenario:

```
[objects]

seq_gen2 = SeqGen
seq_rcvy2 = SeqRcvy frerSeqRcvyAlgorithm=Vector frerSeqRcvyHistoryLength=1993
```


## oam

Here are the OAM related configurations. This section is not mandatory, but if exists it can not be empty.
The oam instantiation is in this format: `name = command with parameters`. The valid parameter names and their valid values depend on the type of the oam command, and they are the same as when issued from the command line interface. The currently known oam commands are:

```
ping[@if] <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]
```
* `ping` ping command
    * <stream:mep-start> the stream and the mep-start within the stream, from where the ping starts
    * <mep-stop/mip/any> the destination of this command (it can be a mep-stop, a mip or a any)
    * <level> is the OAM level
    * -r record route (default: off)
    * -o dump objects (default: off)
    * -i interval in ms (default: 1s)
    * -t ttl (default: 64)
    * -n count, for config file this option is invalid (a configured ping always runs)
```
rping[@if] <remote mep-stop/mip> <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]
```
* `rping` remote ping command  - coming soon
    * remote mep-stop/mip is the remote mep/mip which will execute the ping command.
    * the other parameters are the same as for `ping`.

Example for a DetNet scenario:
```
[oam]

oam1 = ping s1:mepn1s1 any 4 -r -o
oam2 = rping in12 s1:mepn1s1 in12 4
```
