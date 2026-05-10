# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

## Requirements

R2DTWO currently only supports GNU/Linux environments.
A fairly up-to-date GNU/Linux distribution like Ubuntu, Debian, Fedora, RHEL, Arch, etc. should work.
__Important:__ Linux kernel version 4.20 or later is required!
To build R2DTWO only a C compiler and Make are required, there are no other dependencies.

Installing the build dependencies on Ubuntu/Debian based distros:
```
sudo apt install build-essential
```
Installing the build dependencies on Fedora/RHEL:
```
sudo dnf groupinstall @development-tools @development-libraries
```

We use `veth` virtual ethernet interfaces in this guide, make sure it's supported by your kernel

```
grep VETH /boot/config-$(uname -r)
CONFIG_VETH=m
```

If `CONFIG_VETH` is `=m` or `=y` that means your kernel has `veth` support.
If `veth` is not supported, consider switching to a recent major GNU/Linux distro.

For connectivity check the _ping_ application from the `iputils-ping` package required.
To trace traffic on the interfaces, `wireshark` and `tshark` packages are required.

## Compilation and install of R2DTWO

The compilation of the project can be done with a `make` command

```
cd r2dtwo
make

# verify if r2dtwo executable successfully created
file r2dtwo
r2dtwo: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, with debug_info, not stripped
```

Optional: to run `r2dtwo` from any working directory, consider installing it to system-wide.
To do that, execute the following

```
sudo make install

# verify if r2dtwo is installed on the system

cd $HOME
r2dtwo

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

## Test R2DTWO in various network scenarios

For simplicity, in this guide we use standard Linux tools to test R2DTWO in a sandbox network.
Some scenarios with more complex network we use `mininet` network emulator.

These sandboxes are built to mimic the environment with NXP boards.
Advanced users can use real network topology, however, make sure the NXP boards are properly initialized.

__Please note:__ the R2DTWO configurations used in the scenarios identical to the NXP boards, so they can be used on those as well.
But every scenario described here focuses on running R2DTWO on a single machine for simplicity.

These are the scenarios, you can find detailed descriptions and relevant configs (network setup scripts and R2DTWO configs) in their subfolders:

1. [scenario_tsn](scenario_tsn/README.md) - this is the recommended starting point, using R2DTWO as a Layer2 Ethernet swtich with 802.1CB extension
2. [scenario_tsn_over_detnet](scenario_tsn_over_detnet/README.md) - TSN over DetNet scenario, where Layer2 Ethernet traffic encapsulated into DetNet MPLS pseudowires and handled with PREF extension
3. [scenario_ip_over_detnet](scenario_ip_over_detnet/README.md) - IPv46 over DetNet scenario, where regular Layer3 IP traffic encapsulated into DetNet MPLS pseudowires and handled with PREF extension
4. [scenario_ladder](scenario_ladder/README.md) - Ladder topology with DetNet implementing the IEEE 802.1CB's ladder redundancy example
5. [scenario_dynip](scenario_dynip/README.md) - Dynamic IP configuration for a mobile endpoint
6. [scenario_oam](scenario_oam/README.md) - IPv4 over DetNet scenario with additional Operation Administration and Maintenance (OAM) extension
7. [scenario_mask](scenario_mask/README.md) - Scenario presenting the path masking functionality of the replication object
