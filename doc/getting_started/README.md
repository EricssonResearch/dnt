# Getting started with R2DTWO: Reliable & Robust Deterministic Tool for netWOrking

This guide only show how to compile and run R2DTWO in a very simple network environment.
For simplicity, in this guide we use standard linux tools to create our sandbox network.
Advanced users can use real network topology for that, however the configs given should be modified accordingly.

## Requirements

R2DTWO currently only supports GNU/Linux environments.
A fairly up-to-date GNU/Linux distribution like Ubuntu, Debian, Fedora, RHEL, Arch, etc. should works.
Install the dependencies for the compilation and to run the project.

On Ubuntu/Debian based distros:
```
sudo apt install build-essential iproute2
```
On Fedora/RHEL:
```
sudo dnf groupinstall @development-tools @development-libraries
```

We use `veth` virtual ethernet interfaces in this guide, make sure its supported by your kernel

```
grep VETH /boot/config-$(uname -r)
CONFIG_VETH=m
```
If `CONFIG_VETH` is `=m` or `=y` that means your kernel has `veth` support.
If `veth` not supported, consider switch to a recent major GNU/Linux distro.

## Compilation and install of R2DTWO

The compilation of the project can be done with a `make` command

```
cd r2dtwo
make

# verify if r2dtwo executable successfully created
file r2dtwo 
r2dtwo: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 3.2.0, with debug_info, not stripped
```

Optional: to run `r2dtwo` from any working directory, consider installing it to system wide.
To do that, execute the following

```
sudo cp r2dtwo /usr/bin/

# verify if r2dtwo installed on the system
cd $HOME
r2dtwo
R2DTWO - Reliable & Robust Deterministic Tool for netWOrking implementation
Version 6.0
usage: r2dtwo configfile
```

# Test R2DTWO in a simple emulated network environment

## Prepare the test namespace and execute commands in that namespace

In the following, we create a Linux network namespace for our tests.
That's intended to separate every network configuration and traffic from our host's normal networking.
This helps not to interfere with the host networking and also helps to clean up everything after the testing.
For simplicity, we call our network namespace as `r2` in this guide. Create it with the command below:

```
sudo ip netns add r2
```

Normally every shell command runs in the host namespace.
In order to execute a command in that new `r2` namespace, we have to do it explicitly:

```
sudo ip netns exec r2 ip show link
1: lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

Right now we only have one loopback interface named `lo` in the `r2` network, created by the kernel automatically.
For convenience, we create an alias command to run other commands inside the `r2` namespace:

```
alias r2exec="sudo ip netns exec r2"
r2exec ip link
1: lo: <LOOPBACK> mtu 65536 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

## Create the network topology

R2DTWO can protect the traffic by using redundant network paths simultaneously.
Every packet duplicated (or more generally replicated, since it can supports more than two paths) to the network paths, called _replication_.
The receiving node only accept the first copy of the packet and drop the rest, that's called _elimination_.

We will create the following topology, which consist:
* a talker node called **talker** which will generate traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO switches, called **sw1** and **sw2** switching the traffic between the __talker__ and __listener__.
Normally, without virtual ethernet interfaces this would require 4 physical machines and 8 NICs.
```
  talker               sw1                         sw2              listener
┌────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌─────────┐
│        │    │                  │         │                  │    │         │
│        │    │          sw1p1  ─┼─────────┼─  sw2p1          │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│ teth0 ─┼────┼─ sw1p0           │         │           sw2p0 ─┼────┼─ leth0  │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          sw1p2  ─┼─────────┼─  sw2p2          │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```
As you can see, there are redundant paths between **sw1** and **sw2**.
These paths will utilized by R2DTWO for redundancy.

Note that, the nodes __talker__, __listener__, __sw1__ and __sw2__ configured implicitly by the interface configs and R2DTWO configs.

Lets create the topology with the following commands.
```
r2exec ip link add teth0 type veth peer name sw1p0
r2exec ip link add leth0 type veth peer name sw2p0
r2exec ip link add sw1p1 type veth peer name sw2p1
r2exec ip link add sw1p2 type veth peer name sw2p2
```
After their creation, the `veth` interfaces (and the `lo` loopback) all disabled.
To turn them all on, we can execute the following commands:

```
r2exec ip link set teth0 up
r2exec ip link set leth0 up
r2exec ip link set sw1p0 up
r2exec ip link set sw1p1 up
r2exec ip link set sw1p2 up
r2exec ip link set sw2p0 up
r2exec ip link set sw2p1 up
r2exec ip link set sw2p2 up
r2exec ip link set lo up
```
