
# OAM functions

OAM functions include service basic ping and remote ping functionality, initiated either from telnet-like CLI or configuration file.

The OAM functionality requires the following pre-requisites:

* Configure `oam` and `oam_cmd` interfaces
* Add `mep-start`, `mip` and `mep-stop` actions to the stream actions
* optionally add OAM background commands in the config

The OAM CLI can be reached via `telnet` command to the address:port specified for the `oam_cmd` interface.

Examples and naming convention for the interface and command parameters can be found below.

## Naming convention

For a self-explanatory configuration file and proper OAM operation, there are recommended naming rules.
The naming is also important for [automatically generated OAM points](#automatic-mip-generation) discussed later.
Note that the `mip`s will be automatically generated only if `AutoMIP` is specified for the replication/elimination object.

* to identify the streams one can use `s1` or `s2` names, or `m1` and `m2` for member streams. Compound streams can be `c1`, `c2`.
* in case of replication `R1` object, each of its stream/pipeline can be named as `r1-{stream name}`
* in case of elimination `E1` object, its pipeline should named as `e1-{stream name}`

For example, a replication pipeline should look like:

```ini
s1:actions = ..., replicate r1-m1 r1-m2
r1-m1 = ..., send if1
r1-m2 = ..., send if2
```

An elimination pipeline should look like:

```ini
s1:actions = ..., E1 e1-c1
s2:actions = ..., E1 e1-c1
e1-c1 = ..., send if1
```

A more complex scenario with replication/elimination of compound streams:

```
     s1-----E1------ e1-c1
            /
           / r1-m1
     s2--R1
          \_______ r1-m3

s1:actions = ..., eliminate E1 e1-c1
s2:actions = ..., replicate R1 r1-m1 r1-m3
r1-m1 = ..., eliminate E1 e1-c1
r1-m3 = ..., send if2
e1-c1 = ..., send if3
```

## Automatic MIP generation

For each replication and elimination object, `mip` points can be automatically created with the `AutoMIP=level` parameter. In case of replication objects, a `mip` is placed before the replication. In case of elimination objects, a `mip` is placed before AND after the elimination.
The automatically generated `mip`s are always uniquely identified with the following naming scheme.

The naming for the auto generated replication `mip` objects is

* `o_<stream_action_name>_L<level>_pre_<replication_object_name>`

For the elimination objects, the generated `mip`s are:

* `o_<stream_action_name>_L<level>_pre_<elimination_object_name>`
* `o_<stream_action_name>_L<level>_post_<elimination_object_name>`

Here `stream_action_name` is the name of the action pipeline. After a jump the stream name will be the action pipeline name where the jump refers. This is needed to uniquely identify the streams/substreams. For elimination objects, the post elimination `mip` is uniquely identified by the elimination object name.
The `replication_object_name` and `elimination_object_name` refer to the replication and elimination objects.
The level of the generated `mip` actions is specified by the `AutoMIP=level` parameter of the object.

## OAM CLI commands

The main OAM commands are `ping` and `rping`. There also are several helping commands. Commands cannot be abbreviated.

The available commands are:

* `help` get help
* `exit`, `quit`, `CTRL+D` exit OAM
* `log [module newlevel]` get current log levels or set it for the given module
* `mode <mode>` set ping reply printing mode, can be 'dump' or 'json'
* `list` list monitoring start points
* `returns` list return interfaces
* `sessions [stream]` list active sessions for stream, lists all sessions if no 'stream' is specified
* `[un]mask <replication pipeline>` mask/unmask a replication pipeline
* `rlist[@if] <mep-start/mip> <mep-stop/mip/any> <level>` list monitoring start points of the remote node
* `ping[@if] <mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]` sends a ping request inside a stream, the reply will come out-of-band to @if
    * `<mep-start>` sets the position in the action pipeline where the ping starts
    * `<mep-stop/mip/any>` is the name of the destination of the ping
    * `<level>` is the OAM level of the session, can be 0..7
    * `-r` ask for route record
    * `-o` ask for object information
    * `-i` interval in ms (default: 1000)
    * `-n` number of requests to send (default: 1)
    * `-t` ttl (default: 64)
* `rping[@if] <remote stream:mep-stop/mip> <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]` sends a remote ping request: instruct a remote OAM start point to send a ping request
    * accepts the same parameters as `ping`
* `stop [stream session_id]` stop a running OAM session identified by `stream:session_id`, without parameter it stops the last session

The `ping` command sends a ping request inside a stream, and the responder will send a ping reply out-of-bound to the OAM return interface. The `rping` command on an *originator* node is sent to the *initiator* node, which in turn will send a normal ping request to the given target.

If `@if` parameter specifies the OAM return interface for *ping/rping/rlist*, where the reply will be sent by the responder. If unspecified, the default OAM return interface will be used. For `rping`, the `@if` parameter refers to the interface at the originator node.

It is also possible to specify an IP address and port instead of the name of a return interface, if e.g. one is using a central report collection server. The accepted formats are: IPv4, IPv4:port, IPv6, [IPv6], [IPv6]:port. The port defaults to 6634.

With the `mask` and `unmask` command the operator can disable or enable the transmission on replication pipelines at runtime.
Only works with replication pipelines, normal stream and jump pipelines cannot be masked.

## OAM message formats

The OAM in-band requests have a fixed header, and a payload in Json format. The out-of-band replies only have a Json payload.

### ping request

Fixed header

 * channel - we use 1 as a placeholder until IANA assigns something to us
 * level - OAM level
 * nodeid - identifies the source node, should be an MPLS label (now: last 2 octets of the IPv4 address of the default return interface)
 * session - distinguishes between ping sessions from the same node on the same stream
 * seq - sequence number within a session (when sending multiple ping requests)

Json

 * type = "ping"
 * code = "request"
 * return - ip/port where the ping reply should be sent
    * ip
    * port
 * stream - name of the stream where the start point is, important for reply processing (note: at the target node the stream can have different name!)
 * target - the end point that must answer the request, can be "any"
 * rr - route request, all intermediate measurement points add themselves to this list
 * objects - if true, the target measurement point supplies information about its associated object
 * delay - if true, requests delay measurement (the target writes its receive timestamp anyways)
 * send_s - timestamp seconds
 * send_ns - timestamp nanoseconds
 * source - source MIP statistics if "-o" option given
    * packets_passed - data packets seen by the MIP
    * packets_passed - data packets seen by the MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP

### ping reply

Json returns everything as-is, except "return". Also adds info received in the fixed header.

 * type = "ping"
 * code = "reply"
 * stream - returned unchanged
 * target - returned unchanged or with stats (below) of "-o" given
    * packets_passed - data packets seen by the MIP
    * packets_passed - data packets seen by the MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP
 * delay - returned unchanged
 * level - from fixed header
 * nodeid - from fixed header
 * session - from fixed header
 * seq - from fixed header
 * label - MPLS label on the request
 * receiver - name of the processing end point (interesting when target is "any")
 * rr - returned, processing end point added
 * objects - returned, object state filled
 * recv_s - timestamp seconds
 * recv_ns - timestamp nanoseconds
 * source - source MIP statistics if "-o" option given
    * packets_passed - data packets seen by the MIP
    * packets_passed - data packets seen by the MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP
    * octets_passed - aggregate size of OAM packet generated/seen by the MEPStart/MIP

### rping request

Fixed header is same as ping.

Json has mostly the same info as ping.

 * type = "rping"
 * code = "request"
 * return - ip/port where the ping reply should be sent
    * ip
    * port
 * stream - name of the stream where the start point is, important for reply processing (note: at the target node the stream can have different name!)
 * target - the end point that must process the request, can **not** be "any"
 * **command** - the ping command to be executed on the remote node, specifies a start point that can be different from the target of the rping, can't start with "@if" because the return interface is specified in the "return" field
 * send_s - timestamp seconds
 * send_ns - timestamp nanoseconds

Three actors: originator -> *rping* -> initiator -> *ping* -> responder -> *ping response* -> originator

Receiving such a request triggers a normal ping by interpreting "command" as it came from the command line, minus the "ping" at the beginning. The initiated ping will use "stream" and "session" from this request instead of the ones allocated on the initiator. The return interface is the one supplied in this request instead of one of the interfaces on the initiator.

### rping error

This is an error message, sent to the originator when the initiator could not send the ping.

Json has mostly the same info as ping reply.

 * type = "rping"
 * code = "error"
 * error - the error message

### rlist request

The originator of an rping can discover start points on the potential initiator nodes.

Fixed header is same as ping. Json has mostly the same info as ping.

 * type = "rlist"
 * code = "request"
 * return - ip/port where the ping reply should be sent
    * ip
    * port
 * stream - name of the stream where the start point is, important for reply processing (note: at the target node the stream can have different name!)
 * target - the end point that must answer the request, can be "any"

### rlist reply

Same deal as with ping: Json has the info from the fixed header, "return" is removed.

 * rlist - list of start points for the stream
