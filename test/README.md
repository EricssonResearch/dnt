# R2DTWO testing

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

