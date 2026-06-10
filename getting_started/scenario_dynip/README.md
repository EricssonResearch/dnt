# Scenario 5: DNT Dynamic IP configuration

__Important: this scenario assumes background knowledge of the basics from the DetNet scenarios__ [TSN over DetNet](../scenario_tsn_over_detnet/README.md) and [IP over DetNet](../scenario_ip_over_detnet/README.md)

This scenario creates UDP PseudoWire tunnels between a **mobile** node and a **server** node.
The **mobile** has two 5G interfaces connecting to different wireless networks, the **server** also has one interface toward each of these mobile networks.
The **gateway** nodes are the anchor points of the mobile in each mobile network.
The traffic between the **server** and the **mobile** is replicated on both 5G network paths.

The **mobile** gets its IP addresses (on the *wwanX* interfaces) from the network dynamically, which are initially unknown to the **server** node.
The address of *ethX* on the **server** and *wwanX* on the **mobile** must be routable and directly reachable from the other endpoint.
If the mobile network provider puts the mobile behind a stateful firewall, our UDP PseudoWire tunnels will not work.
This is also the reason for this scenario being IPv6-only, as on IPv4 the mobile node would most likely be behind a NAT.

This topology also shows an example on how to set up DNT on an end-station.

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
│ fd55::1   │               ││                    ││               │   fd66::1 │
│           │               ││   gateway2         ││               │           │
│           │               ││  ┌──────────────┐  ││               │           │
│           │               ││  │              │  ││               │           │
│           │           eth1││  │swp0          │  ││               │           │
│           │             ──┼┼──┼────          │  ││               │           │
│           │        fd21::2││  │fd21::1   swp1│  ││wwan1          │           │
│           │ DNT        ││  │           ───┼──┼┼───     DNT │           │
│           ╰───────────────╯│  │       fd22::1│  │╰───────────────╯           │
└────────────────────────────┘  └──────────────┘  └────────────────────────────┘
```

The routing is set up such that the end-stations can reach each other through the gateway nodes.
When **mobile** receives its addresses, it also sets a default route to each gateway.
Normal traffic that is not explicitly bound to one of the *wwanX* interfaces will choose the first default route, so the redundant paths are not utilized.
The traffic DNT sends is bound to the interfaces, so it makes use of the distinct paths.

There is also an explicit route to reach the IP range (*fd55::/64* and *fd66::/64*) of the other end-station through the VRF interface.
These IP ranges can be private even in a public deployment, only addresses on the physical interfaces must be globally routable.

The MTU on the links of the gateway nodes is raised to 1600 so that it can accommodate the tunnelling overhead.
It is important to also set MTU for the VRF interfaces, because by default it's 64k.

## DNT configurations

The DNT configurations for **mobile** and **server** are similar to the [IP over DetNet](../scenario_ip_over_detnet/README.md) scenario.

Since the **mobile** gets its IP addresses dynamically from its anchor points, we cannot configure destination addresses on the *udp-out* interfaces on the **server** node.
Instead, we only specify the IP version in the config, and wait for notifications about the real addresses.

In the config for the **mobile** node the *udp-in* interfaces get a *senders* parameter.
This parameter tells who to notify about the IP address of the Linux interface this interface is bound to.
The other end will receive the notifications with *oam* receiver interfaces, so here we have to specify the IP of those.
In this scenario there is no dedicated OAM network, so the notification IP addresses are the ones on *eth0* and *eth1*.

The reason for sending notification to both *oam* interfaces from both *udp-in* on **mobile** is a routing issue.
As mentioned previously, traffic not bound to an interface will choose the first default route.
Here, we have a default route for each *wwan* interface, and the winner will be the one that connected to the network first.
Therefore, both *udp-in* interfaces would end up sending all their notifications on the same *wwan* interface, but one of them would not be routed to the **sever** node, because the destination is unreachable on that path.

## Running the scenario

To run this scenario we need at least 5 terminals prepared like this:

```
sudo -s

source env.sh
```

This changes the prompt to `(dynamic ip) root:scenario_dynip#` if done from the correct directory.

In two of the terminals we need to run DNT:

```
# in terminal 1
server dnt server.ini

# in terminal 2
mobile dnt mobile.ini
```

We can't run traffic yet, because the **mobile** node has no IP addresses yet.
In two of the terminals we need to run *radvd* to supply IP address to the **mobile** node via ICMPv6 Router Advertisement messages:

```
# in terminal 3
gateway1 radvd -C ./radvd.conf -m stderr -n -p radvd_gateway1.pid

# in terminal 4
gateway2 radvd -C ./radvd.conf -m stderr -n -p radvd_gateway2.pid
```

Now we can use the remaining terminals to send traffic between **mobile** and **server**.
If the previous steps were done correctly, the commands below should be successful:

```
# in terminal 5
server ping fd66::1
PING fd66::1 (fd66::1) 56 data bytes
64 bytes from fd66::1: icmp_seq=1 ttl=63 time=0.469 ms
64 bytes from fd66::1: icmp_seq=2 ttl=63 time=0.541 ms

mobile ping fd55::1
PING fd55::1 (fd55::1) 56 data bytes
64 bytes from fd55::1: icmp_seq=1 ttl=63 time=0.345 ms
64 bytes from fd55::1: icmp_seq=2 ttl=63 time=0.662 ms
```

Observing this traffic with *Wireshark* or some other tool requires further terminals.

Sending real traffic is a bit tricky, because VRF interfaces are meant for routing traffic, and not terminating it.
User sockets can send traffic into a VRF interface, but to receive packets the socket must be bound to the VRF interface.
An easy way of binding the socket of an application to the VRF interface is to use the `ip exec` command.
With this we can run *iperf* in our test network:

```
# in terminal 5
server ip vrf exec vrf0 iperf -sV

# in terminal 6
mobile ip vrf exec vrf0 iperf -c fd55::1
```

As usual, at the end we can exit the sourced environments with `exit` or `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
