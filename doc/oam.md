
# OAM functions

OAM functions include service basic ping and remote ping functionality, initiated either from telnet-like CLI or configuration file.
The OAM functionality requires the following pre-requisites:

* Configure 'oam' and 'oam_cmd' interfaces
* Add 'mep-start', 'mip' and 'mep-stop' actions to the stream actions
* optionally configure OAM background commands to be executed

Example for ini file parameters are in the inispec.md

The OAM CLI can be reached via 'telnet' command to the address:port specified for the oam_cmd interface.

## OAM CLI commands

The main commands are 'ping' and 'rping'. There also are several helping commands. Commands cannot be abbreviated.
The available commands are:

* help - get help
* exit, quit, CTRL+D - exit OAM
* log [module newlevel] - get current log levels or set it for the given module.
* list - list monitoring start points
* rlist[@if] <stream:mep-start/mip> <mep-stop/mip/any> <level> - list monitoring start points of the remote node.
* mode <mode> - terminal mode. Mode can be 'dump' or 'json'.
* sessions [stream] - list active sessions for stream. If no 'stream' specified, lists all sessions
* stop [stream session_id] - stop a running OAM session, identified by 'stream:session_id'. Without parameters it stops the last session
* returns - list return interfaces
* ping[@if] <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]
* rping[@if] <remote stream:mep-stop/mip> <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]

For rping, the @if parameter refers the interface at the initiator node. It is not possible to specify other OAM interface at the remote node, the default OAM interface will be used.

## OAM message formats

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

### ping reply

Json returns everything as-is, except "return". Also adds info received in the fixed header.

 * type = "ping"
 * code = "reply"
 * stream - returned unchanged
 * target - returned unchanged
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

Receiving such a request triggers a normal ping by interpreting "command" as it came from the command line, minus the "ping" at the beginning. The initiated ping will use "stream" and "session" from this request instead of the ones allocated on the receiver. The return interface is the one supplied in this request instead of one of the interfaces on the receiver.

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

