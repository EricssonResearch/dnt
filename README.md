# R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

R2DTWO is a generic userspace deterministic networking toolbox.

Highlighted features:

* Portable with minimal external dependency
* Layer 2 TSN virtual switch with IEEE 802.1CB (FRER) support
* Layer 3 DetNet virtual router with PREOF support
* Generic header matching & editing for many protocols (Ethernet, S-VLAN, C-VLAN, MPLS, IPv4, IPv6, etc.)
* Extensions to the FRER/PREOF standards to support better cloud-native operation (seamless FRER/PREOF, delay function)
* Supporting Ethernet, IPv4, IPv6 and MPLS pseudo-wire interfaces
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

Also a fairly up-to-date Linux kernel required.
Kernel versions shipped with major GNU/Linux distributions in the past ~5 years should be fine.

__Compile and (optionally) install R2DTWO__

Download the source code archive or clone the Git repository.
After that the compilation can be done with _make_.

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
This shows three common use-cases (Layer 2 TSN, Layer 3 TSN over Detnet and IP over Detnet) supported by R2DTWO.
To try them out no hardware equipment required, however we support NXP switches as well.

R2DTWO supplied with detailed documentation included in the release package.
The documentation show various scenarios and configuration examples.

__Important:__ the recommended way to run R2DTWO is to use root privileges.

### Support

Question regarding to R2DTWO contact with [Ferenc Fejes \<ferenc.fejes@ericsson.com\>](mailto:ferenc.fejes@ericsson.com)

Team:

* Ericsson: Balázs Varga, Ferenc Fejes, János Farkas
* BME: István Moldován, Miklós Máté
