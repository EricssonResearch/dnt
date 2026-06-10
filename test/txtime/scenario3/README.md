# TXTIME test3

**This scenario and configuration focuses on UDP packet sending using ETF qdisc to create delay.**

This scenario is the same as you can find in the `getting_started/scenario_tsn_over_detnet` folder. The difference is that we removed one route and we want to use `ETF` qdisc to send packets with delay. To achieve this, we set up every interface with 4 tx queues in the `env.sh`. The second queue is the `ETF`'s queue. If we want to send packets into this queue, we need to set the packet's priority to 1. We can do this in the `ini` file:

```
nni1_out = udp-out iface=swp0 dstip=192.168.55.2 prio=1
```

Now the `ETF` offload is not set in `env.sh`, which means the proper delay is calculated from the delay that we wrote into the ini file minus the `ETF`'s delta. For example, if the delay is `1500 us` = `1.5 s` and the delta is `1000000000 ns` = `1 sec`, this means the delay is going to be `1.5 s` - `1 s` = `0.5 s`.

```
    talker              nxp1                         nxp2              listener
┌──────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌──────────┐
│          │    │                  │         │                  │    │          │
│          │    │      192.168.55.1│         │192.168.55.2      │    │          │
│          │    │           swp0  ─┼─────────┼─  swp0           │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│          │    │                  │         │                  │    │          │
│    eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0    │
│ 10.0.0.1 │    │                  │         │                  │    │ 10.0.0.2 │
│          │    │                  │         │                  │    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘
```

### Commands to run this scenario:

```
nxp1 dnt nxp1.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.27 12:58:05 [MAIN] [INFO] DNT - Dependable Networking Toolkit 6.3
2024.03.27 12:58:05 [MAIN] [INFO] Reading config 'nxp1.ini'

2024.03.27 12:58:05 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 12:58:05 [INTERFACE] [INFO] Udp-out interface nni1_out on device swp0
2024.03.27 12:58:05 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'udp-in' on 'swp0' is not available
2024.03.27 12:58:05 [INTERFACE] [INFO] Udp-in interface nni1_in on device swp0
2024.03.27 12:58:05 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp2' is not available
2024.03.27 12:58:05 [INTERFACE] [INFO] Eth interface uni on device swp2
```

```
nxp2 dnt nxp2.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.27 12:58:20 [MAIN] [INFO] DNT - Dependable Networking Toolkit 6.3
2024.03.27 12:58:20 [MAIN] [INFO] Reading config 'nxp2.ini'

2024.03.27 12:58:20 [INTERFACE] [INFO] Udp-out interface nni1_out on device swp0
2024.03.27 12:58:20 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'udp-in' on 'swp0' is not available
2024.03.27 12:58:20 [INTERFACE] [INFO] Udp-in interface nni1_in on device swp0
2024.03.27 12:58:20 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp2' is not available
2024.03.27 12:58:20 [INTERFACE] [INFO] Eth interface uni on device swp2
```

```
talker ping 10.0.0.2 -c 10 -A

PING 10.0.0.2 (10.0.0.2) 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=501 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=500 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=500 ms
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=500 ms
...
```

### To observe ETF:

```
nxp1 watch -n 0.1 tc -s qdisc show dev swp0
```

```
qdisc mqprio 100: root tc 3 map 0 1 2 2 0 0 0 0 0 0 0 0 0 0 0 0
             queues:(0:0) (1:1) (2:3)
             mode:dcb
             shaper:dcb
 ...
 ...
 ...
qdisc etf 8010: parent 100:2 clockid TAI delta 1000000000 offload off deadline_m
ode off skip_sock_check on
 Sent 3480 bytes 24 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 152b 1p requeues 0
```
