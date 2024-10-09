# SRv6 test

__Important: this assumes background knowledge of the basic from the doc/srv6. Please take a look into the SRv6 documentation if you have not already.__ [SRv6](../../doc/srv6.md)

The SRv6 testing can be started by the `srv6.py` python script, which sets up a mininet scenario and runs the tests.
The script has a debug mode, where a given scenario can be selected, and it will give a mininet CLI for further testing.
There are 3 test cases differentiated:
* IPv6 UNI - the incoming traffic is IPv6
* IPv4 UNI - the incoming traffic is IPv4
* TSN UNI - the incoming traffic is TSN
For each test case there is an R2DTWO configuration pair in this directory, named <nodename>_<test>.cfg.

## Running the srv6.py script

The script needs to be run with root privilege, as mininet requires it. Running the self-tests can be done with the following command:

$ sudo python3 srv6.py

The script sets up the network, and runs the test cases sequentially. If everything went well, we get a similar output:
```
...
R2DTWO SRv6 test
Test SRv6 with IPv6 UNI   Background traffic...  DetNet traffic...  packet sizes...  stats... ✔
Test SRv6 with IPv4 UNI   Background traffic...  DetNet traffic...  packet sizes...  stats... ✔
Test SRv6 with TSN  UNI   Background traffic...  TSN    traffic...  packet sizes...  stats... ✔
```

The script tests the connectivity over the SRv6 tunnels for backgroung and TSN/DetNet traffic types, checks the correct packet sizes and also the Linux SRv6 tunnel statistics.

For TSN test scenaio, there may be "size mismatch" type extra messages, as other packets are also generated. If the check mark in the end is displayed, these mismatches can be ignored.

After running the tests, the generated logfiles are removed.

For debugging a scenario, the script must be started with two more parameters: 'debug' and the scenario name. For example the TSN scenaric can be started with the following command:

$ sudo python3 srv6.py debug tsn

Possible scenarios are `ipv6`, `ipv4` and `tsn`. The default is `ipv6`, so if `debug` is specified only, ipv6 scenario is supposed. After running the debug scenario, a mininet prompt is presented and the traffic can be generated manually. Note that in debug mode the generated R2DTWO logfiles are not removed, they must be removed manually.

## Network topology

The network topology is the same for all 3 scenarios. For IPv6/IPv4 scenario the T1 and L5 have IPv6/IPv4 addresses, and for the TSN scenario a VLAN is used. VLAN 10 is used to send TSN traffic.
To differentiate the TSN/DetNet traffic from background, IP DSCP 6 (TOS 0xC0) is used. For TSN traffic, the DSCP 6 traffic is automatically mapped to Ethernet PCP 6 when sent out.
```
                                                   ┌─────────────┐                                                   
                                                   │fd13:fade::0 │                                                   
                                                   └─────────────┘                                                   
                                                   ┏━━━━━━━━━━━━━┓                                                   
                                                   ┃             ┃                                                   
                                    ┌──────────────┨     R3      ┠─────────────┐                                     
                                    │  fd02:a1fa::3┃             ┃fd04:a1fa::3 │                                     
                                    │              ┗━━━━━━━━━━━━━┛             │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                                    │                                          │                                     
                        fd02:a1fa::2│                                          │fd04:a1fa::4                         
┏━━━━━━━━━━━━━┓              ┏━━━━━━┷━━━━━━┓                            ┏━━━━━━┷━━━━━━┓               ┏━━━━━━━━━━━━━┓
┃             ┃              ┃             ┃                            ┃             ┃               ┃             ┃
┃     T1      ┠──────────────┨     R2      ┠────────────────────────────┨     R4      ┠───────────────┨     L5      ┃
┃             ┃  fd01:a1fa::2┃             ┃fd03:a1fa::2    fd03:a1fa::4┃             ┃fd05:a1fa::4   ┃             ┃
┗━━━━━━━━━━━━━┛    10.0.1.2  ┗━━━━━━━━━━━━━┛                            ┗━━━━━━━━━━━━━┛  10.0.5.2     ┗━━━━━━━━━━━━━┛
 fd01:a1fa::1                ┌─────────────┐                            ┌─────────────┐                 fd05:a1fa::5
 10.0.1.1                    │fd12:fade::0 │                            │fd14:fade::0 │                     10.0.5.1
 Vlan 10: 10.10.0.1          └─────────────┘                            └─────────────┘           Vlan 10: 10.10.0.2

```
To test a scenario with ping, the following commands can be used:

For IPv6:
mininet> t1 ping6 fd05:a1fa::5  and t1 ping6 fd05:a1fa::5 -Q 0xc0

For IPv4:
mininet> t1 ping 10.0.5.1   and  t1 ping  -Q 0xc0 10.0.5.1

For TSN:
mininet> t1 ping 10.10.0.2  and  t1 ping  -Q 0xc0 10.10.0.2
