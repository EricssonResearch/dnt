

# R2DTWO Log function

R2DTWO provides logging capabilities to monitor the operation in real time.
This gives the operator a detailed view of what is happening in different modules with adjustable granularity.
Logging levels (verbosity or granularity) are adjustable per module, both of which are discussed in this document.

It is important to note that __increasing logging verbosity has a performance impact on packet processing__.
Logging can be completely disabled by selecting minimal verbosity, which does not affect packet processing performance.

## Usage

R2DTWO has two command line argument, related to the log function:

* `-o` or `--output`: this select the output channel for the application.
Currently supported output channels are `stdout`, `syslog`, `logfile`, `stderr`.
This parameter is optional, by default `stdout` is used.
For convenience, there is a short format for each value:

```
stdout: s
syslog: l
logfile: f
stderr: e
```

* `-v` or `--verbose`: verbosity level, can be defined per-module.
The format is `MODULENAME1:LEVEL_X,MODULENAME2:LEVEL_Y,...`
To set it globally, use `ALL` for module name.
By default, there are pre-defined levels for each R2DTWO module.
These defaults are tuned to provide information about error conditions and cannot induce overhead for normal successful packet processing.

Verbosity levels:

* `NONE` - logging turned off for this module. To turn off logging globally, use `ALL:NONE`
* `ERROR` - error conditions e.g.: interface shutdown, disconnected sockets, malformed packets
* `WARNING` - unusual behavior, probably indicates a non-fatal malfunction e.g.: low memory, unexpected data received on egress interface
* `INFO` - messages for verification of normal operation, e.g.: configuration details, timer resets
* `PACKET` - information about the received/sent packets. __Note: may decrease the packet processing performance__
* `DEBUG` - detailed logging for developers


Usage example:

`r2dtwo --output syslog --verbosty ALL:NONE,INTERFACE:PACKET` - direct the output to syslog (likely at `/var/log/syslog`), turn off logging for all module except `INTERFACE` where logging with `PACKET` verbosity level.
This equivalent with the `r2dtwo -o l -v ALL:NONE,INTERFACE:PACKET` command.

The verbosity level can be configured with the `R2DTWO_VERBOSE` environment variable.
This makes it easier to run the application, since lengthy verbosity arguments can be omitted.

## Modules

R2DTWO is modularized, and each module has a well-defined purpose, such as reading and parsing configuration, sending and receiving packets, matching and parsing packet headers, executing a pipeline of actions, etc.
As mentioned earlier, logging verbosity can be configured independently in each module.
Currently available modules:

* `CONFIG`
* `DELAY`
* `DIAGNOSTIC`
* `HEADER`
* `INTERFACE`
* `MAIN`
* `OAM`
* `PACKET`
* `PACKETTRACE`
* `PARSER`
* `PIPELINE`
* `POF`
* `RCVY`
* `THREAD`

## PACKETTRACE module

The `PACKET` and `DEBUG` logging levels print multiple lines for each packet.
This is useful for a detailed understanding of how packets are handled by the application (receive interface, math stream, parsed headers, action pipeline, etc.).

However, for a quick overview of the packets, there is the `PACKETTRACE` module.
This prints only one line for each packet.
That line is a summary log message for the processed packet, including receive interface, packet size, matching stream if any, drop reason, header stack, pipeline actions, and egress interface if any.

__Important__: `PACKETTRACE` only works at `PACKET` or higher verbosity level, below that it is disabled (and has no processing overhead).
