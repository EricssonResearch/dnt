
# OAM functions

OAM functions include service basic ping and remote ping functionality, initiated either from telnet-like CLI or configuration file.
The OAM functionality requires the following pre-requisites:

* Configure 'oam' and 'oam_cmd' interfaces
* Add 'mep-start', 'mip' and 'mep-stop' actions to the stream actions
* optionally configure OAM background commands to be executed

Example for ini file parameters are in the inispec.md

The OAM CLI can be reached via 'telnet' command to the address:port specified for the oam_cmd interface.

## OAM CLI commands

The main commands are 'ping' and 'rping'. There also are several helping commands.
The available commands are:
* help - get help
* exit - exit OAM
* list - list monitoring start points
* mode <mode> - terminal mode. Mode can be 'dump' or 'json'.
* sessions [stream] - list active sessions for stream. If no 'stream' specified, lists all sessions
* stop [stream session_id] - stop a running OAM session, identified by 'stream:session_id'. Without parameters it stops the last session
* returns - list return interfaces
* ping[@if] <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]
* rping[@if] <remote stream:mep-stop/mip> <stream:mep-start> <mep-stop/mip/any> <level> [-r] [-o] [-i <interval>] [-n <count>] [-t <ttl>]

For rping, the @if parameter refers the interface at the initiator node. It is not possible to specify other OAM interface at the remote node, the default OAM interface will be used.
