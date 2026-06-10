#  IP over DetNet scenario for dependable UE testing: DNT IPv4 over DetNet PseudoWire

In the following, we will use DNT as a DetNet router.
The Layer3 traffic of `talker` and `listener` nodes will be encapsulated in MPLS DetNet pseudowires, then sent through a PREF (Packet Replication and Elimination Functions).

__Important: if this test running on the NXP boards, use the setup scripts for configuring the network properly!__: `nxp1_setup.sh` and `nxp2_setup.sh`

We will use the following topology, which consists:

* a talker node called **talker** which will generate IPv4 traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two DNT instances, running on the **nxp1** and **nxp2** nodes.

```
                      PRF ───►                     ───► PEF

                      PEF ◄───                     ◄─── PRF


    talker              nxp1                         nxp2              listener
┌──────────┐    ┌──────────────────┐         ┌──────────────────┐    ┌──────────┐
│          │    │      192.168.55.1│         │192.168.55.2      │    │          │
│          │    │           swp1  ─┼─────────┼─  swp1           │    │          │
│          │    │         fc0a::1  │         │ fc0a::2          │    │          │
│          │    │                  │         │                  │    │          │
│2001::11  │    │ 2001::1          │         │       2002::2    │    │2002::22  │
│    eth0 ─┼────┼─ swp0      swp2 ─┼─────────┼─ swp2      swp0 ─┼────┼─ eth0    │
10.0.100.11│    │ 10.0.100.1       │         │       10.0.200.1 │    10.0.200.22│
│          │    │                  │         │                  │    │          │
│          │    │         fc0b::1  │         │ fc0b::2          │    │          │
│          │    │           swp3  ─┼─────────┼─  swp3           │    │          │
│          │    │eno0  192.168.77.1│         │192.168.77.2  eno0│    │          │
└──────────┘    └──────────────────┘         └──────────────────┘    └──────────┘
                  |         DNT          DNT           |
                  |192.168.111.2                192.168.111.2|
                  |               ┌──────────┐               |
                  |               │   OAM    │               |
                  └───────────────│  bridge  │───────────────┘
                                  │   br0    │
                                  └──────────┘
                                  192.168.111.3

```
As you can see, there are more redundant paths between **nxp1** and **nxp2**.
These paths will be utilized by DNT for redundancy.
There is a separate OAM network, connecting nxp1 and nxp2 to the OAM receiver via the eno0 interfaces.
On the OAM node, briding is used, thus nxp1 and nxp2 can reach each other via their OAM interfaces, on 192.168.111.x network.
The notifications are sent to 192.168.111.3, where the notification receiver runs.


## The DNT configurations

Now we have separate DNT config files for the two DetNet nodes: `nxp1.ini` and `nxp2.ini`.


### Explanation of the configuration

Since we need OAM, OAM interfaces are defined as:

```
cmd0 = oam_cmd ip=127.0.0.1 port=8000
oam0 = oam ip=192.168.55.1 port=6634

```

AutoMIP is not used, since we do not need mask signaling. Since we do local masking only with frequent masking/unmasking, signaling would just slow the process.
Thus, MPs are added manually. The controlled direction is nxp1 ───► nxp2, the configuration is not fully symmetrical.

We need pre-PRF MIPs, to count incoming packets at each member stream. This is why we have separate pipeline for each interface. Also note that for *pre-pef2_4* MIPs no object is specified. This means that these MIPs will not report PEF object information.

```
pw41:packet = mpls, dcw, ipv4
pw41:match = mpls label=400
pw41:actions = readseq dcw, mip pw41_L4_pre-pef2_4 4, pef2_4 compound
pw42:packet = mpls, dcw, ipv4
pw42:match = mpls label=400
pw42:actions = readseq dcw, mip pw42_L4_pre-pef2_4 4, pef2_4 compound
pw43:packet = mpls, dcw, ipv4
pw43:match = mpls label=400
pw43:actions = readseq dcw, mip pw43_L4_pre-pef2_4 4, pef2_4 compound

compound = mep-stop compound_L4_post-pef2_4 4 pef2_4, del mpls, del dcw, send uni_out
```

When object information is requested with `ping -o` command, the MPs will append their own statistics, and if specified also the related object information as well.
This is why at *post-pef2_4* MEP the pef2_4 object is configured. This way the pre_PEF MIPs will only report their own stats, while the post_PEF MP will report the elimination object info too.


## Operation

The scenario is started by switching to `root` user and do the network config with the `source env.sh` command:

```
sudo -s

source env.sh
```

If everything OK, the scenario starts by:
- setting up the namespaces and interfaces
- starting dnts in background
- starting a background ping with 200ms interval to generate data traffic
- starting the *telnet_control* application on nxp1 - in an xterm, which connects to the local telnet interface.
- starts a loop that periodically changes the link delay/drop rate according to the parameter table defined in the script.


## Exiting

To exit the loop, press Ctrl-C. Then the network simulation loop exits, restores normal network conditions and stops the background ping as well.
Then the prompt should be changed to `(ip over detnet) root:scenario_ip_over_detnet# ` which tells right now we are in the test network environment.

A further `exit` command is needed to stop and clean up the environment.
