
# Logging functionality

R2DTWO provides logging capabilities to monitor its operation in real time.
This gives the operator a detailed view of what is happening in different modules with adjustable verbosity.
Logging levels (verbosity) are adjustable separately for each module.

It is important to note that __increasing logging verbosity has a performance impact on packet processing__.


## Selecting output

The output where the log messages go can be selected with the `-o target` or `--output target` commandline argument.
The options have a single-letter shorthand format, marked with the **bold** letter.

* **s**tdout (default)
* std**e**rr
* sys**l**og
* log**f**ile

The name of the log file is automatically generated from the name of the config file and the PID of R2DTWO.

The log messages are colorful, if the target is stdout or stderr and it's a terminal.

If the output is syslog the results can be viewed with `journalctl -t r2dtwo` on systems using `systemd-journald`.

## Selecting verbosity

R2DTWO is modularized, and each module has a well-defined purpose, such as reading configuration, sending and receiving packets, parsing and matching packet headers, executing a pipeline of actions, etc.
Logging verbosity can be configured independently for each module.
Currently available modules:

* `CONFIG`
* `DELAY`
* `DIAGNOSTIC`
* `HEADER`
* `INTERFACE`
* `MAIN`
* `NOTIFICATION`
* `OAM`
* `PACKET`
* `PACKETTRACE`
* `PARSER`
* `PIPELINE`
* `POF`
* `RCVY`
* `THREAD`

The logging level can be set at startup with the `-v` or `--verbose` commandline argument.
The format is `MODULENAME1:LEVEL_X,MODULENAME2:LEVEL_Y,...`, to set it globally, use `ALL` for module name.
There are pre-defined default levels for each R2DTWO module, which is typically `INFO`.

Verbosity levels:

* `NONE` - logging completely turned off for this module
* `ERROR` - fatal error conditions, where R2DTWO will shut down, e.g. configuration error
* `WARNING` - unusual behavior, indicates a non-fatal malfunction, e.g. low memory, unexpected data received
* `INFO` - indicates normal operation, e.g. a service was started/stopped
* `PACKET` - detailed information about packet processing steps __Note: decreases the packet processing performance significantly__
* `DEBUG` - detailed logging for developers
* `ALL` - shortcut for enabling all logging, no messages are generated with this level

The logging level can also be configured with the `R2DTWO_VERBOSE` environment variable.
It accepts the same format as the `--verbose` commandline argument.
First this environment variable is processed, then the commandline argument can override it.

The logging level can also be changed during runtime with the `log` [telnet command](oam.md).
Without arguments it lists the log level for each module.
Changing the level for a module is done with the `log MODULE NEWLEVEL` command format.


## Usage examples

`r2dtwo --output syslog --verbosty ALL:NONE,INTERFACE:PACKET` - direct the output to syslog (likely at `/var/log/syslog`), and turn off logging for all module except `INTERFACE` where logging will be at `PACKET` verbosity level.
This is equivalent to the `r2dtwo -o l -v ALL:NONE,INTERFACE:PACKET` command.

`R2DTWO_VERBOSE=ALL:NONE r2dtwo -ol -vINTERFACE:PACKET` - does the same setting as the previous example.
Note that POSIX allows the space to be omitted between the argument name and its parameter.

A telnet example:

```
$ telnet localhost 8000
OAM ready
log INTERFACE WARNING
```

## PACKETTRACE module

The `PACKET` and `DEBUG` logging levels are useful for a detailed understanding of how packets are handled by the application (receive interface, matching streams, parsed headers, actions, etc.).
They however print multiple lines for each packet, which can be too verbose for tracing the lifetime of packets on a busy node.

For a quick overview of the packets, there is the `PACKETTRACE` module, which prints only one line for each packet.
That line is a summary for the processed packet, including receive interface, packet size, matching stream if any, drop reason, header stack, pipeline actions, and egress interface if any.

__Important__: `PACKETTRACE` only logs at the `PACKET` verbosity level, if the verbosity for this module is lower, then it doesn't do anything (and has no processing overhead).
