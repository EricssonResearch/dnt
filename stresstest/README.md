# Stress test

This TSN scenario has replication-elimination, and a POF ordering. In both directions one branch has a delay, the other has loss. This is intended to exercise the sequence recovery and ordering modules.

To run:

```
sudo ./stressnet.py
mininet> xterm talker listener nxp1 nxp2
```

In the *nxp1* and *nxp2* terminals do `../r2dtwo stress.ini` to start the tunnel.

Traffic generation:

* on *listener* `iperf -Vs` for dual-stack reception
* on *talker* `iperf -c 10.0.0.2` or `iperf -c fd01::2`

