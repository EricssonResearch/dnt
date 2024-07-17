# Dynamic IP test

This scenario has 5 nodes in a chain: *na* and *ne* are the endpoints, there is an IP-in-PseudoWire tunnel between *nb* and *nd*. The IPv4 traffic goes in an IPv4 tunnel, the IPv6 traffic goes in an IPv6 tunnel.

Node *nc* provides IP address for *nd* with a DHCP server or ICMPv6 Router Advertisement.

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

Some more serious traffic:

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

This will use the prefix of the existing IPv6 address on *ethCD*. If the address on `ethCD` is changed to one with a differenct prefix, *radvd* will start advertising the new prefix. Note that it takes a few seconds for the new prefix to be advertised, during which there is an outage in the tunnel.

## DHCP

**TODO find a DHCP server that works**

