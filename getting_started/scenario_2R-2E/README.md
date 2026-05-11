# Scenario 9: R2DTWO IPv4 over DetNet PseudoWire with 2 Replicate and 2 Eliminate points

This scenario is the same from networking aspects as the Scenario IP over DetNet, but the network topology is extended, as the stream replication and elimination is done at two r2dtwo nodes in the network, and we also have fully configured OAM and notification setup.

```
                                                                                        ┌─────┬──────────────────┐             
                                                                                        │admin│                  │             
                                                                                        ├─────┘    mgmt          │             
                                                                                        │      172.16.0.254      │             
                                                                                        └────────────┬───────────┘             
                                                                                                     │                         
                                 ┌────────────────────────────┬───────────────────────┬──────────────┴─────────────────┐       
                                 │       ┌─┬──────────────────┴───┐                   │                                │       
                                 │       │B│                 mgmt │                   │                                │       
┌───────┬───────┐    ┌─┬─────────┴──┐    ├─┘           172.16.0.2 │      ┌─┬──────────┴──┐    ┌─────────┬────┐         │       
│talker │       │    │A│        mgmt│    │                        │      │D│        mgmt │    │listener │    │         │       
├───────┘       │    ├─┘  172.16.0.1│ ┌──┤ba                    bd├──┐   ├─┘  172.16.0.4 │    ├─────────┘    │         │       
│               │    │              │ │  │10.0.12.2      10.0.24.2│  │   │               │    │              │         │       
│               │    │              │ │  │                        │  │   │               │    │              │         │       
│               │    │            ab├─┘  │          bc            │  └───┤db             │    │              │         │       
│               │    │     10.0.12.1│    │       10.0.23.2        │      │10.0.24.4      │    │              │         │       
│               │    │              │    └───────────┬────────────┘      │               │    │              │         │       
│         eth0  ├────┤eth0          │      ───► PRF  │                   │          eth0 ├────┤eth0          │         │       
│  192.168.1.1  │    │192.168.1.2   │      PEF ◄───  │                   │   192.168.2.1 │    │192.168.2.2   │         │       
│               │    │              │                │                   │               │    │              │         │       
│               │    │              │                │                   │               │    │              │         │       
│               │    │              │    ┌─┬─────────┴────────────┐      │               │    │              │         │       
│               │    │            ac├─┐  │C│         cb           │  ┌───┤dc             │    │              │         │       
│               │    │     10.0.13.1│ │  ├─┘      10.0.23.3       │  │   │10.0.35.4      │    │              │         │       
│               │    │              │ └──┤ca                    cd├──┘   │               │    │              │         │       
│               │    │              │    │10.0.13.3      10.0.35.3│      │               │    │              │         │       
└───────────────┘    └──────────────┘    │                        │      └───────────────┘    └──────────────┘         │       
                        ───► PRF         │                   mgmt │           ───► PEF                                 │       
                        PEF ◄───         │             172.16.0.3 │           PRF ◄───                                 │       
                                         └──────────────────────┬─┘                                                    │       
                                            ───► PEF            │                                                      │       
                                            PRF ◄───            └──────────────────────────────────────────────────────┘       

```

The talker stream is replicated at nodes A and B and elimination is done at nodes C and D, while the listener stream is replicated at nodes D and C and elimination is done at nodes A and B.

## The R2DTWO configurations

Four different configuration files for the r2dtwo nodes: `A.ini`, `B.ini`, `C.ini`, and `D.ini`.

## Run the R2DTWO and generate traffic

Let's try out R2DTWO with this scenario.

For that, we need at least six terminal windows: one for generating traffic (`talker`), one for receiving traffic (`listener`), and four terminals for running R2DTWO instances.

After opening the terminals, switch to `root` user and do the network config in each with the `source env.sh` command:

```
sudo -s
source env.sh
```

If everything is OK, the prompt will change to `( 2R-2E ) root:scenario_2R-2E# ` which tells us, we are in the test network environment.
Now we should have all the networking (nodes, interfaces, IP addresses, and routing) configured and helper commands to execute commands on the nodes.

Now we can start the R2DTWO instances in different namespaces: `A`, `B`, `C`, and `D`:

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


If everything is OK, the `r2dtwo` instances are up and running.

To generate some traffic with `ping` on the `talker` node run:

```
talker ping 192.168.2.2
```

To supress ICMP Destination Net Unreachable messages the Linux tc redirections is congifuredd in the environment. For more details see the Scenario IP over DetNet.

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
