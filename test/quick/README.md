# Quicktest

This is a sandbox for testing the actions and packet header manipulations during development. The R2DTWO configurations do some sane stuff to let traffic through, but they also send some insane variations of the packets for testing.

The network can be started as

```
sudo ./quicknet.py
mininet> xterm na nb nc nd
```

## quicktest.ini

This has to be run on *nb*, its normal function is to do a VLAN translation between *na* and *nc*, check it out on *na* with `ping 192.168.1.3` or `ping fd92::3`.

## quickdetB.ini and quickdetC.ini

This sets up a TSN-in-PseudoWire tunnel between *nb* and *nc*, so *na* and *nd* can talk to each other, check it out on *na* with `ping 192.168.1.4` or `ping fd92::4`.

## quickip.ini

This is similar to *quicktest.ini* but with a layer 3 connection between *na* and *nc*. It needs some additional routing setup.
