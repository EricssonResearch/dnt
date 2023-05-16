# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

## Requirements

R2DTWO currently only supports GNU/Linux environments.
A fairly up-to-date GNU/Linux distribution like Ubuntu, Debian, Fedora, RHEL, Arch, etc. should works.
Install the dependencies for the compilation and run the project.

On Ubuntu/Debian based distros:
```
sudo apt install build-essential iproute2 wireshark
```
On Fedora/RHEL:
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

__In order to run the selftests (optional, but recommended), Python 3.10 or more recent version required.__
Also, the _ping_ application required for the tests, from the `iputils-ping` package.

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

R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation
Version 6.0
usage: r2dtwo configfile
```

# Test R2DTWO in a simple network environments

For simplicity, in this guide we use standard Linux tools to test R2DTWO in a sandbox network.
Advanced users can use real network topology for that however, make sure the NXP boards are properly initialized.

__Please note:__ the R2DTWO configurations used in the scenarios identical to the NXP boards, so they can be used on those as well.
But every scenario described here focuses on running R2DTWO on a single machine for simplicity.

There are three scenarios, you can find the details (`README.md`s) and relevant configs (network setup scripts and R2DTWO config `.ini`s) in their subfolders:

* `scenario1` - this is the recommended starting point, using R2DTWO as a Layer2 Ethernet swtich with 802.1CB extension [scenario1/README.md](scenario1/README.md)
* `scenario2` - TSN over DetNet scenario, where Layer2 Ethernet traffic encapsulated into DetNet MPLS pseudowires and handled with PREF extension [scenario2/README.md](scenario2/README.md)
* `scenario3` - IP over DetNet scenario, where regular Layer3 IP traffic encapsulated into DetNet MPLS pseudowires and handled with PREF extension [scenario3/README.md](scenario3/README.md)

