# R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

R2DTWO is a generic userspace deterministic networking toolbox.

Highlighted features:

* Portable with minimal external dependencies
* Layer 2 TSN virtual switch with IEEE 802.1CB (FRER) support
* Layer 3 DetNet virtual router with PREOF support
* DetNet OAM support and built-in command-line interface
* Generic header matching & editing for many protocols (Ethernet, S-VLAN, C-VLAN, MPLS, IPv4, IPv6, etc.)
* Extensions to the FRER/PREOF standards for better cloud-native operation support (seamless FRER/PREOF, delay function)
* Supporting Ethernet, IPv4, IPv6 and MPLS pseudo-wire interfaces for sending/receiving
* Extensible architecture, well defined interfaces for new protocols and functions

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

A fairly up-to-date Linux kernel is also required.
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

To get started with R2DTWO there is a detailed [Getting started](doc/getting_started/README.md) guide.
This shows three common practical use-cases (Layer 2 TSN, Layer 3 TSN over Detnet and IP over Detnet) supported by R2DTWO.
To try them no special hardware equipment is required, however the scenarios are crafted such that the configs can be run on NXP switches without modifications.

R2DTWO comes with detailed documentation included in the release package.
The documentation shows various scenarios and configuration examples.

__Important:__ the recommended way to run R2DTWO is to use root privileges.

### Running the application

The only mandatory command line argument is the configuration file.
The verbosity of the logging and the target of the output can be configured with optional command line arguments.
There are per-module default verbosity levels at the moment, and the `-v` option defines global verbosity.
The default logging output is syslog (`-ol`, stdout is `-os` and logfile is `-of`).

```
Usage: r2dtwo [OPTION...] CONFIGFILE

  -o, --output=logfile       Output: log[f]ile, sys[l]og, [s]tdout
  -v, --verbose=DEFAULT      Available loglevels: NONE, ERROR, WARNING, INFO,
                             PACKET, DEBUG, ALL
  -?, --help                 Give this help list
      --usage                Give a short usage message
```

## Tests

R2DTWO comes with a few different tests to verify its correct operation.

### Unit tests

These are in the `unit_test` directory. They need `cmake` to build, and it's recommended to build them in a separate directory. For example:

```
cd unit_test
mkdir build
cd build
cmake ..
make
./test_seq_rcvy_vector
```

The unit tests use the same infrastructure as the unit tests that come with the INI parser library used by R2DTWO (see inifile/test). The exit code of the tests is 0 if all the test cases passed.

### Integration tests

These are in the `test` directory. They test R2DTWO as a whole by running it with certain configurations. They need python3 to run, and most of them also need python3-scapy. __TODO more documentation on this__

### Debugging tests

These were used to debug R2DTWO during development. They are not systematic, not comprehensive, and not well-documented, but may be useful for showing some of the capabilities of R2DTWO. They need python3 and mininet to run.

* *quicktest* is a mess that was used to test various R2DTWO functionalities during development. It contains configs for TSN and DetNet scenarios. Start the network with quicknet.py, and consult the comment lines in that script to see how to start R2DTWO and run the traffic.
* *stresstest* is a scenario for testing the POF and Delay functions of R2DTWO: it replicates the traffic on two paths, one path has a substantial packet loss, the other one is delayed. Running this scenario is similar to running *quicktest*.

It is recommended to start xterm terminals from mininet on the virtual nodes, and run the commands in those terminals.

## Support

With questions regarding to R2DTWO bugs/usage, please contact [Ferenc Fejes \<ferenc.fejes@ericsson.com\>](mailto:ferenc.fejes@ericsson.com)

Team:

* Ericsson: Balázs Varga, Ferenc Fejes, János Farkas
* BME: István Moldován, Miklós Máté
