# Dynamic IP test

This scenario has 5 nodes in a chain: *na* and *ne* are the endpoints, their default gateways are *nb* and *nd*, respectively. The traffic between *na* and *nd* is intercepted by R2DTWO on *nb* and *nd*, and forwarded in an IP-in-PseudoWire tunnel. The IPv4 traffic goes in an IPv4 tunnel, the IPv6 traffic goes in an IPv6 tunnel.

Node *nd* has no IP address on its *ethDC* interface. Node *nc* provides IP address for *nd* with a DHCP server or ICMPv6 Router Advertisement.

```
┌────────────────┐  ┌────────────────┐
│       na       │  │       nb       │
│                │  │                │
│┌──────────────┐│  │┌──────────────┐│
││              ││  ││              ││
││    ethAB     ││  ││    ethBA     ││
││   10.0.1.1   ├┼──┼┤   10.0.1.2   ││
││   fd01::1    ││  ││   fd01::2    ││  ┌────────────────┐
││              ││  ││              ││  │       nc       │
│└──────────────┘│  │└──────────────┘│  │                │
│                │  │┌──────────────┐│  │┌──────────────┐│
│                │  ││              ││  ││              ││
│                │  ││    ethBC     ││  ││    ethCB     ││
│                │  ││   10.0.2.2   ├┼──┼┤   10.0.2.3   ││
│                │  ││   fd02::2    ││  ││   fd02::3    ││  ┌────────────────┐  ┌────────────────┐
│                │  ││              ││  ││              ││  │       nd       │  │       ne       │
│                │  │└──────────────┘│  │└──────────────┘│  │                │  │                │
│                │  │                │  │┌──────────────┐│  │┌──────────────┐│  │                │
└────────────────┘  └────────────────┘  ││              ││  ││              ││  │                │
                                        ││    ethCD     ││  ││    ethDC     ││  │                │
                                        ││   10.0.3.3   ├┼──┼┤no initial ip ││  │                │
                                        ││   fd03::3    ││  ││              ││  │                │
                                        ││              ││  ││              ││  │                │
                                        │└──────────────┘│  │└──────────────┘│  │                │
                                        │                │  │┌──────────────┐│  │┌──────────────┐│
                                        └────────────────┘  ││              ││  ││              ││
                                                            ││    ethDE     ││  ││    ethED     ││
                                                            ││   10.0.4.4   ├┼──┼┤   10.0.4.5   ││
                                                            ││   fd04::4    ││  ││   fd04::5    ││
                                                            ││              ││  ││              ││
                                                            │└──────────────┘│  │└──────────────┘│
                                                            │                │  │                │
                                                            └────────────────┘  └────────────────┘
```

Run R2DTWO like this:

```
nb# ../r2dtwo nb.ini

nd# ../r2dtwo nd.ini
```

If everything is running correctly, then *na* and *ne* can communicate with each other:

```
na# ping 10.0.4.5

na# ping fd04::5
```

More serious traffic:

```
ne# iperf -sV

na# iperf -c 10.0.4.5

na# iperf -c fd04::5
```

## Router Advertisement

Run *radvd* on *nc* to assign IPv6 address to `ethDC` on *nd*:

```
nc# radvd -C ./radvdC.conf -m stderr -n
```

This will use the prefix of the existing IPv6 address on `ethCD`. If the address on `ethCD` is changed to one with a differenct prefix, *radvd* will start advertising the new prefix. Note that it takes a few seconds for the new prefix to be advertised, during which there is an outage in the tunnel.

## DHCP

There are several DHCP servers available for Linux. Once one of the servers is running on *nc*, on *nd* we can do `dhclient -d ethDC` to get IPv4 address or `dhclient -6 -d ethDC` to get IPv6 address.

Note that only DHCPv4 can supply default gateway, on IPv6 we need *radvd* to set it. The `radvdC.conf` can be used in conjunction with DHCP, the address set by DHCP will take precedence over the address configured from the router advertisement. The `radvdCnoaddr.conf` doesn't configure address, just the default gateway.

### Kea

[Kea](https://kea.readthedocs.io/en/latest/index.html) is the official DHCP server of the Internet Systems Consortium, replacing their old implementation that is now deprecated.

```
apt install kea
```

There are two issues with the default Kea install.

* the server runs by default (with a config that doesn't supply address on any interface), and this prevents us from running another instance in Mininet
    * solution: `systemctl stop kea-dhcp4-server.service` and `systemctl stop kea-dhcp6-server.service` and optionally `systemctl stop kea-dhcp-ddns-server.service` (it's also a good idea to disable these services)
* the package comes with Apparmor profiles that prevent loading the config file from anywhere other than `/etc/kea/`, and for some reason it also can't create the lock files in `/run/kea/`
    * solution: `apt install apparmor-utils` and `sudo aa-complain /usr/sbin/kea-dhcp4` to turn the Apparmor errors into warnings (see `dmesg`)

Start Kea like this:

```
nc# kea-dhcp4 -c kea4.conf

nc# kea-dhcp6 -c kea6.conf
```

### CoreDHCP

[CoreDHCP](https://github.com/coredhcp/coredhcp) is a modular DHCP server written in Go. They don't mention it in their readme, but it is actually a frontend for a [dhcp library](https://github.com/insomniacslk/dhcp) that is used in other projects too.

```
apt install coredhcp-server
```

Start CoreDHCP like this:

```
coredhcp -c coredhcp.yaml
```

This configuration is supposed to lease IPv4 and IPv6 addresses toward node *nd*, but for some reason only the IPv4 lease works, so it's not a complete alternative for Kea.


