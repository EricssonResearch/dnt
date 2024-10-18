# R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

R2DTWO is a generic userspace deterministic networking toolbox.

Highlighted features:

* Portable with minimal external dependencies
* Layer 2 TSN virtual switch with IEEE 802.1CB (FRER) support
* Layer 3 DetNet virtual router with PREOF support
* DetNetSRv6 experimental support
* DetNet OAM support and built-in telnet command-line interface
* Generic header matching & editing for many protocols (Ethernet, S-VLAN, C-VLAN, MPLS, IPv4, IPv6, etc.)
* Extensions to the FRER/PREOF standards for better cloud-native operation support (seamless FRER/PREOF, delay function)
* Supporting Ethernet, IPv4, IPv6, SRv6 and MPLS pseudo-wire interfaces for sending/receiving
* Hardware offload support for delay function (hardware support required)
* Dynamic address configuration for tunnel endpoints on DHCP and NDP configured interfaces
* Extensible architecture, well defined interfaces for new protocols and functions
* Many more...

## Installation

__Install the required dependencies.__

On Ubuntu/Debian based distros:

```
sudo apt install build-essential
```

On Fedora/RHEL and derivates:

```
sudo dnf groupinstall @development-tools @development-libraries
```
The minimal required Linux version is 4.20.
Kernel versions shipped with major GNU/Linux distributions in the past ~5 years should be fine.

__Compile and (optionally) install R2DTWO__

Download the source code archive or clone the Git repository.
After that the compilation can be done with _make_ command.

```
cd r2dtwo
make
```

Optional: install R2DTWO system wide.

```
sudo make install
```

## Usage

To get started with R2DTWO there is a detailed [Getting started](getting_started/README.md) guide.
This presents common practical use-cases (TSN, DetNet, OAM and more) supported by R2DTWO.
To try them no special hardware equipment is required, however the scenarios are crafted such that the configs can be run on NXP switches without modifications.

R2DTWO comes with detailed documentation included in the release package.
The documentation shows various scenarios and configuration examples.

__Important:__ the recommended way to run R2DTWO is to use root privileges.
Unpriviliged operation also supported, but for Layer2 operation (RAW Ethernet/IP sockets) `CAP_NET_RAW` capability must be granted.

### Running the application

The only mandatory command line argument is the configuration file.
The verbosity of the logging and the target of the output can be configured with optional command line arguments.
There are per-module default verbosity levels at the moment, and the `-v` option defines global verbosity.
The default logging output is syslog (`-ol`, stdout is `-os` and logfile is `-of`).

```
Usage: r2dtwo [OPTION...] CONFIGFILE

  -o, --output=logfile       Output: log[f]ile, sys[l]og, [s]tdout (default),
                             std[e]rr
  -v, --verbose=MODULE:LEVEL Available levels: NONE, ERROR, WARNING, INFO,
                             PACKET, DEBUG, ALL
  -?, --help                 Give this help list
      --usage                Give a short usage message

Mandatory or optional arguments to long options are also mandatory or optional
for any corresponding short options.
r2dtwo: Config file required!
Try `r2dtwo --help' or `r2dtwo --usage' for more information.
```

## Tests

R2DTWO comes with various self-tests to verify its correct operation.
These tests designed to be run automatically by part of a CI pipeline.
There are debugging tests as well, for rapid development.
For more information check their [documentation](./test/README.md)

## Documentation

As mentioned before, the entry point of R2DTWO usage are the [getting started](./getting_started/README.md) guides.
All available configuration options are listed in the [config specification](./doc/config_format.md).
Other topics such as the supported protocols, SRv6, OAM, logging, etc. documented in the [doc](./doc/) folder.


## Support

With questions regarding to R2DTWO bugs/usage, please contact [Ferenc Fejes \<ferenc.fejes@ericsson.com\>](mailto:ferenc.fejes@ericsson.com)

Team:

* Ericsson: Balázs Varga, Ferenc Fejes, Ferenc Orosi, János Farkas
* BME: István Moldován, Miklós Máté

