# Scenario: Dynamic IP configuration

Topology:

* mobile has two 5G interfaces connecting to different networks
* both 5G networks have a gateway node
* server has one interface toward each mobile network

The traffic between the server and the mobile is replicated on both 5G networks.

The address of ethX on the server and wwanX on the mobile must be routable and directly reachable from the other endpoint.
If the provider puts the mobile behind a stateful firewall our UDP PseudoWire tunnels will not work.

This topology is also a demonstration on how to set up R2DWO on an end-station.

```
 server                          gateway1                                mobile
┌────────────────────────────┐  ┌──────────────┐  ┌────────────────────────────┐
│           ╭───────────────╮│  │              │  │╭───────────────╮           │
│           │           eth0││  │swp0          │  ││               │           │
│           │            ───┼┼──┼───           │  ││               │           │
│           │        fd11::2││  │fd11::1   swp1│  ││ wwan0         │           │
│           │               ││  │           ───┼──┼┼───            │           │
│           │               ││  │       fd12::1│  ││               │           │
│           │               ││  └──────────────┘  ││               │           │
│  vrf0     │               ││                    ││               │     vrf0  │
│   ────────┼───            ││                    ││            ───┼────────   │
│ fd66::1   │               ││                    ││               │   fd55::1 │
│           │               ││   gateway2         ││               │           │
│           │               ││  ┌──────────────┐  ││               │           │
│           │               ││  │              │  ││               │           │
│           │           eth1││  │swp0          │  ││               │           │
│           │             ──┼┼──┼────          │  ││               │           │
│           │        fd21::2││  │fd21::1   swp1│  ││wwan1          │           │
│           │ R2DTWO        ││  │           ───┼──┼┼───     R2DTWO │           │
│           ╰───────────────╯│  │       fd22::1│  │╰───────────────╯           │
└────────────────────────────┘  └──────────────┘  └────────────────────────────┘
```

TODO explain the routing

TODO radvd configs, try if it works

TODO explain how to use this stuff

