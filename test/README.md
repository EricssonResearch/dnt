# DNT testing

These tests intended for automatic integration testing (CI) use.
Also they include many useful configuration and network examples.

To run the test manually:

```
sudo python -m testname.testname
```

For example there are `stress_ping.py` and `stress_iperf.py` in the `stress` folder.
To execute them, use the commands:

```
sudo python -m stress.stress_ping
sudo python -m stress.stress_iperf
```

## Requirements

To run the selftests manually, some packages are required.
Install these requirements with:

```
sudo apt install cmake mininet python3-pyroute2 python3-regex iproute2 strace psmisc iputils-ping valgrind clang iperf
```

## Integration tests

There are integration selftests intended to be part of a CI/CD pipeline.
They supposed to find regressions in DNTs behavior.
Additionally, these tests are (partially) documented and they have many configuration file examples.
The examples can be used as building blocks other configurations as well.


## Debugging tests

In addition to selftests, there are live debugging mininet environments for quick development.
These were used to debug DNT during development.
They are not systematic, not comprehensive, and not well-documented, but may be useful for showing some of the capabilities of DNT.
It is recommended to start `xterm` terminals from mininet on the virtual nodes, and run the commands in those terminals.

`quick` - small dual-stack test environment for basic TSN and DetNet functionality
`stress/stress_iperf.py` - environment for high-throughput (iperf) tests exercising the POF and delay functionalities
`dynip` - environment to experiment with dynamic IP configuration (IPv4 and v6)

