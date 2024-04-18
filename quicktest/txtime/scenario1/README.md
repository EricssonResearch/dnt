# TXTIME physical test

```
  talker                             nsx                            listener
┌────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌─────────┐
│        │    │                  │         │                  │    │         │
│        │    │          enp3s0 ─┼─────────┼─ enp4s0          │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│ teth0 ─┼────┼─ aeth0           │         │           beth0 ─┼────┼─ leth0  │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │                  │         │                  │    │         │
│        │    │          enp6s0 ─┼─────────┼─ enp7s0          │    │         │
│        │    │                  │         │                  │    │         │
└────────┘    └──────────────────┘         └──────────────────┘    └─────────┘
```

This is a simple scenario to test R2DTWO on physical devices. `enp3s0`, `enp4s0`, `enp6s0` and `enp7s0` are physical devices, `teth0`, `aeth0`, `beth0` and `leth0` are veth devices. Physical interfaces have multiple `TX` queues. On these devices, we configured `ETF` qdisc as the second `TX` queue. `ETF` offload is enabled, so the delay is going to be what we wrote into the `ini` file. The delta does not matter here.

```
nsx tc qdisc add dev enp3s0 handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0
nsx tc qdisc replace dev enp3s0 parent 100:2 etf clockid CLOCK_TAI delta 300000 skip_sock_check offload
```

The first queue is maintained for unwanted traffic such as `ARP` or `PTP`. This means that if we want to send a packet through `ETF`'s queue, we should set the packet's priority to 1. We can achieve this in the `ini` file:

```
... edit cvlan.pcp=1, delay 1500 offload, ...
```

Physical interfaces will generate RX hardware timestamps. This hardware timestamp will be used instead of the software timestamp. These timestamps will appear in the debug log.

### We need to sync clocks with `PTP` for the accurate delay:

```
nsx ptp4l -i enp3s0 -p /dev/ptp4 -i enp4s0 -p /dev/ptp4 -i enp6s0 -p /dev/ptp4 -i enp7s0 -p /dev/ptp4 -m -H
```

```
nsx phc2sys -rr -m -R 10 -c enp3s0 -s CLOCK_REALTIME -O 0 -z /var/run/ptp4l -w
```

```
nsx phc2sys -rr -m -R 10 -c enp4s0 -s CLOCK_REALTIME -O 0 -z /var/run/ptp4l -w
```

```
nsx phc2sys -rr -m -R 10 -c enp6s0 -s CLOCK_REALTIME -O 0 -z /var/run/ptp4l -w
```

```
nsx phc2sys -rr -m -R 10 -c enp7s0 -s CLOCK_REALTIME -O 0 -z /var/run/ptp4l -w
```

### To run this scenario:
```
nsx r2dtwo send.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.28 14:17:44 [MAIN] [INFO] R2DTWO - Reliable & Robust Deterministic Tool for netWOrking 6.3
2024.03.28 14:17:44 [MAIN] [INFO] Reading config 'send.ini'

2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: rx_filter requested HWTSTAMP_FILTER_ALL got HWTSTAMP_FILTER_ALL
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp3s0'
2024.03.28 14:17:44 [INTERFACE] [INFO] Eth interface nni0 on device enp3s0
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: rx_filter requested HWTSTAMP_FILTER_ALL got HWTSTAMP_FILTER_ALL
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SO_TXTIME enabled on 'enp6s0'
2024.03.28 14:17:44 [INTERFACE] [INFO] Eth interface nni1 on device enp6s0
2024.03.28 14:17:44 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'aeth0' is not available
2024.03.28 14:17:44 [INTERFACE] [INFO] Eth interface uni on device aeth0
```

```
nsx r2dtwo recv.ini -o stdout -v INTERFACE:ALL

Info: Logging to standard output.
2024.03.28 14:18:01 [MAIN] [INFO] R2DTWO - Reliable & Robust Deterministic Tool for netWOrking 6.3
2024.03.28 14:18:01 [MAIN] [INFO] Reading config 'recv.ini'

2024.03.28 14:18:01 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: rx_filter requested HWTSTAMP_FILTER_ALL got HWTSTAMP_FILTER_ALL
2024.03.28 14:18:01 [INTERFACE] [INFO] Eth interface nni0 on device enp4s0
2024.03.28 14:18:01 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: rx_filter requested HWTSTAMP_FILTER_ALL got HWTSTAMP_FILTER_ALL
2024.03.28 14:18:01 [INTERFACE] [INFO] Eth interface nni1 on device enp7s0
2024.03.28 14:18:01 [INTERFACE] [DEBUG] SIOCSHWTSTAMP: HW timestamping for 'eth' on 'beth0' is not available
2024.03.28 14:18:01 [INTERFACE] [INFO] Eth interface uni on device beth0
```

```
tx ping 10.0.0.2 -c 4

PING 10.0.0.2 (10.0.0.2) 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=200 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=200 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=200 ms
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=200 ms
```

## Cleanup:

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window. To do that use the `exit` command or press `CTRL + D`. When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.