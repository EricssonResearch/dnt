# TXTIME test2

**This scenario and configuration focuses on Ethernet frame sending using ETF qdisc to create delay.**

This scenario is the same as you can find in the `getting_started/scenario_tsn` folder. The difference is that we want to use `ETF` qdisc to send packets with delay.

```
  talker              nxp1                         nxp2              listener
┌────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌─────────┐
│        │    │                  │         │                  │    │         │
│        │    │          swp0   ─┼─────────┼─  swp0           │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│  eth0 ─┼────┼─ swp2            │         │            swp2 ─┼────┼─ eth0   │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          swp1   ─┼─────────┼─  swp1           │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```

To achieve this, we set up every interface with 4 tx queues in the `env.sh`.

```
ip link add swp2 numtxqueues 4 netns nxp1 type veth peer eth0 numtxqueues 4 netns talker
...
```

The second queue is the `ETF`'s queue.

```
nxp1 tc qdisc add dev swp0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
nxp1 tc qdisc replace dev swp0 parent 100:2 etf clockid CLOCK_TAI delta 1000000000 skip_sock_check
...
```

If we want to send packets into this queue, we need to set the packet's priority to 1 in the `ini` file, like this:

```
..., edit cvlan.pcp=1, delay 1500 offload, ...
```

Now the `ETF` offload is not set in `env.sh`, which means the proper delay is calculated from the delay that we wrote into the `ini` file minus the `ETF`'s delta. For example, if the delay is `1500 us` = `1.5 s` and the delta is `1000000000 ns` = `1 sec`, this means the delay is going to be `1.5 s` - `1 s` = `0.5 s`.

### Commands to run this scenario:

```
nxp1 r2dtwo r2dtwo.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.27 13:09:12 [MAIN] [INFO] R2DTWO - Reliable & Robust Deterministic Tool for netWOrking 6.3
2024.03.27 13:09:12 [MAIN] [INFO] Reading config 'r2dtwo.ini'

2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp0' is not available
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:12 [INTERFACE] [INFO] Eth interface nni1 on device swp0
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp1' is not available
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:12 [INTERFACE] [INFO] Eth interface nni2 on device swp1
2024.03.27 13:09:12 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp2' is not available
2024.03.27 13:09:12 [INTERFACE] [INFO] Eth interface uni on device swp2
...
```

```
nxp2 r2dtwo r2dtwo.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.27 13:09:52 [MAIN] [INFO] R2DTWO - Reliable & Robust Deterministic Tool for netWOrking 6.3
2024.03.27 13:09:52 [MAIN] [INFO] Reading config 'r2dtwo.ini'

2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp0' is not available
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp0'
2024.03.27 13:09:52 [INTERFACE] [INFO] Eth interface nni1 on device swp0
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp1' is not available
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'swp1'
2024.03.27 13:09:52 [INTERFACE] [INFO] Eth interface nni2 on device swp1
2024.03.27 13:09:52 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'swp2' is not available
2024.03.27 13:09:52 [INTERFACE] [INFO] Eth interface uni on device swp2
...
```

```
talker ping 10.0.0.2 -c 10 -A

PING 10.0.0.2 (10.0.0.2) 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=501 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=501 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=501 ms
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
 Sent 6910 bytes 72 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 0b 0p requeues 0
qdisc pfifo_fast 0: parent 100:4 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 0b 0p requeues 0
qdisc pfifo_fast 0: parent 100:3 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 0b 0p requeues 0
qdisc pfifo_fast 0: parent 100:1 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
 Sent 1146 bytes 15 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 0b 0p requeues 0
qdisc etf 8003: parent 100:2 clockid TAI delta 1000000000 offload off deadline_m
ode off skip_sock_check on
 Sent 5764 bytes 57 pkt (dropped 0, overlimits 0 requeues 0)
 backlog 0b 0p requeues 0
```
