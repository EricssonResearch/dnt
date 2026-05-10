# Scenario 4: R2DTWO IPv4 over DetNet (Ladder redundancy)

This scenario shows an example of "ladder redundancy" taken from D.3 of 802.1CB. This network will continue to function in the face of multiple failures because the Stream is repeatedly split and remerged.

```
                                                        B                             D                                                       
                                       31   ┌──────────────────────┐  31  ┌──────────────────────┐  31                                        
                                            │     b1        b3     │      │     d1        d3     │                                            
      talker               A            ┌───▶ 10.0.12.2  10.0.24.2 ├──────▶ 10.0.24.4  10.0.46.4 ├──┐             F              listener     
┌───────────────┐    ┌───────────────┐  │   │                      │      │                      │  │   ┌───────────────┐    ┌───────────────┐
│               │    │               │  │   │          b2          │      │          d2          │  │   │               │    │               │
│               │    │         a1    ├──┘   │      10.0.23.2       │      │      10.0.45.4       │  └───▶     f1        │    │               │
│               │    │     10.0.12.1 │      └───────┬──────▲───────┘      └────────┬─────▲───────┘      │ 10.0.46.6     │    │               │
│               │    │               │              │      │                       │     │              │               │    │               │
│               │    │               │              │      │                       │     │              │               │    │               │
│      eth0     ├────▶    eth0       │              │      │                       │     │              │        eth0   ├────▶      eth0     │
│  192.168.1.1  │    │192.168.1.2    │           31 │      │ 26                 31 │     │ 26           │    192.168.2.1│    │  192.168.2.2  │
00:00:00:01:01:01    │               │              │      │                       │     │              │               │    00:00:00:02:02:02
│               │    │               │              │      │                       │     │              │               │    │               │
│               │    │         a2    │              │      │                       │     │              │     f2        │    │               │
│               │    │     10.0.13.1 ├──┐   ┌───────▼──────┴───────┐      ┌────────▼─────┴───────┐  ┌───▶ 10.0.56.6     │    │               │
│               │    │               │  │   │          c2          │      │          e2          │  │   │               │    │               │
│               │    │               │  │   │      10.0.23.3       │      │      10.0.45.5       │  │   │               │    │               │
└───────────────┘    └───────────────┘  │   │                      │      │                      │  │   └───────────────┘    └───────────────┘
                                        │   │     c1        c3     │      │     e1        e3     ├──┘                                         
                                        └───▶ 10.0.13.3  10.0.35.3 ├──────▶ 10.0.35.5  10.0.56.5 │                                            
                                       26   └──────────────────────┘  26  └──────────────────────┘  26                                        
                                                        C                             E                                                       
```

As one can see, the Talker end system sequences Stream 31, then splits it into two Streams 26 and 31, sending one on each of its two ports. The network "ladder" has two "rails", an upper (relay systems B and D) and a lower (relay systems C and E). The "rungs" are the connections B–C and D–E. Each relay system sends the Stream received on the rail from the left both to the right, along the rail, and up or down, over the rung. It forwards the packets received from the rung only to the right, along the rail. At each output port to the rail, the end systems have a Sequence recovery function to eliminate duplicates.

## The R2DTWO configurations

We have six different configuration files for all DetNet nodes: `A.ini`, `B.ini`, `C.ini`, `D.ini`, `E.ini`, and `F.ini`.
This is required because even if the topology is symmetrical, we have different IP addresses.

## Run the R2DTWO and generate traffic

Let's try out R2DTWO with this scenario.

For that, we need at least eight terminal windows: one for generating traffic (`talker`), one for receiving traffic (`listener`), and six terminals for running R2DTWO instances.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo -s
source env.sh
```

If everything is OK, the prompt will change to `(ladder redundancy) root:scenario_ladder# ` which tells us, we are in the test network environment.
Now we should have all the networking (nodes, interfaces, IP addresses, and routing) configured and helper commands to execute commands on the nodes.

Now we can start the R2DTWO instances in different namespaces: `A`, `B`, `C`, `D`, `E`, and `F`:

```
A r2dtwo A.ini
```

```
B r2dtwo B.ini
```

```
C r2dtwo C.ini
```

```
D r2dtwo D.ini
```

```
E r2dtwo E.ini
```

```
F r2dtwo F.ini
```

If everything is OK, the `r2dtwo` instances are up and running.
But we have to generate some traffic right now with `netcat` so run it on the `talker` node:

```
talker netcat -u 192.168.2.2 20345 
hello
world
```

To check if the network is working as expected we can listen on `eth0` with `netcat`:

```
listener netcat -ul 192.168.2.2 20345
hello
world
```

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
