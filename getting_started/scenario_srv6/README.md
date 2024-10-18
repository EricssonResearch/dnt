# SRv6 test

__Important: this assumes background knowledge of the basic from the doc/srv6. Please take a look into the SRv6 documentation if you have not already.__ [SRv6](../../doc/srv6.md)

The SRv6 scenario can be started by the `srv6.py` python script, which sets up a mininet scenario.
There are 3 test cases differentiated:
* IPv6 UNI - the incoming traffic is IPv6
* IPv4 UNI - the incoming traffic is IPv4
* TSN UNI - the incoming traffic is TSN


## Running the srv6.py script

To start a given scenario, the following command can be used:


```
$ sudo python3 srv6.py <scenario name>
```

The script sets up the network, and runs the test cases sequentially. If everything went well, we get a similar output:

```
$ sudo python3 srv6.py
*** Adding hosts
*** Creating links
*** Configuring hosts
t1 r2 r3 r4 l5
*** Adding IPv6 addresses
*** Setting up SRv6 tunnels
*** Starting network
*** Starting controller

*** Starting 0 switches

*** Waiting for switches to connect

R2DTWO SRv6 debug
*** Starting R2DTWOs, scenario ipv6
*** Starting CLI:
mininet>

```

Possible scenarios are `ipv6`, `ipv4` and `tsn`. The default is `ipv6`, so by default, ipv6 scenario is supposed. After running the debug scenario, a mininet prompt is presented and the traffic can be generated manually. Note that the generated R2DTWO logfiles are not removed, they must be removed manually.

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
