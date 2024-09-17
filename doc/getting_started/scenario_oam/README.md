# Scenario: R2DTWO Operation, Administration and Maintenance (OAM)

__Important: this scenario assumes background knowledge of the first three scenarios.
Please take a look into Scenarios TSN, TSNoDetNet and IP46oDetNet scenarios if you have not already.__

This scenario extending the IP46oDetNet scenario with OAM capabilities and functions.
To make things simpler, the IPv6 streams and addressing removed to focus more on the OAM part.

We will use the following topology, which consists:

* a talker node called **talker** which will generate IPv4 traffic
* a node called **listener** which receive the traffic coming from the **talker**
* two R2DTWO instances, running on the **nxp1** and **nxp2** nodes.
* a node called __admin__ which connected to __nxp1__ and __nxp2__ over a management network.

```
                                   ┏━━━━━━━┓
                                   ┃ admin ┃
                           ┌───────┺━━━━━━━┹───────┐
                           │                       │
                           │      172.16.0.254     │
                           │       ┌────────┐      │
                           │ ┌─────┤  mgmt  ├────┐ │
                           └─┼─────┴────────┴────┼─┘
                 ┏━━━━━━┓    │                   │    ┏━━━━━━┓
                 ┃ nxp1 ┃    │                   │    ┃ nxp2 ┃
                 ┡━━━━━━┹────┴──────┐     ┌──────┴────┺━━━━━━┩
┏━━━━━━━━┓       │        mgmt0     │     │     mgmt0        │       ┏━━━━━━━━┓
┃ talker ┃       │     172.16.0.1   │     │   172.16.0.2     │       ┃listener┃
┡━━━━━━━━┹──┐    │                  │     │                  │    ┌──┺━━━━━━━━┩
│           │    │              swp0├─────┤swp0              │    │           │
│           │    │      192.168.55.1│     │192.168.55.2      │    │           │
│           │    │                  │     │                  │    │           │
│       eth0├────┤swp2              │     │              swp2├────┤eth0       │
│10.0.100.11│    │10.0.100.1        │     │        10.0.200.1│    │10.0.200.22│
│           │    │                  │     │                  │    │           │
│           │    │              swp1├─────┤swp1              │    │           │
└───────────┘    │      192.168.66.1│     │192.168.66.2      │    └───────────┘
                 └──────────────────┘     └──────────────────┘

                      PRF ───►                     ───► PEF

                      PEF ◄───                     ◄─── PRF

                      R2DTWO                       R2DTWO
```

Compared to the IP46oDetNet scenario, there are some notable changes.

A new node is added to the network.
This is named "admin" and is the operator's machine.
It is connected to __nxp1__ and __nxp2__ via a management network 172.16.0.0/24.
This is a very common network setup: the production network has a high-speed forwarding plane, and the configuration of the nodes is done through a management network.

Also, FRER/PREOF was intended to protect against path failures.
Therefore administration of R2DTWO over the production network would be a bad idea since it is expected to fail from time to time.

__nxp1__ and __nxp2__ connected to the management network with their `mgmt0` interfaces.
They have the `172.16.0.1` and `172.16.0.2` management IP addresses respectively.


## Oparation, Administration and Maintenance (OAM) intro

Before we proceed, we will introduce the concept of Maintenance Points.
R2DTWO and this guide use the following IEEE 802.1ag terminology for OAM entities.

MP - Maintenance Point(s)
MIP - Maintenance Intermediate Point
MEP - Maintenance Endpoint

The operator must configure such MPs in R2DTWO to monitor the network.
OAM communication is only defined between MPs (two or more).
There are two types of MEPs: MEP Start and MEP Stop.
The MEP Start should be the initiator of the OAM session to a MIP or a MEP Stop.
The MEP Stop is responsible for dropping the OAM messages at the boundary of the OAM domain.

An OAM level is configured for each MP by the operator.
This level is a number from 0 to 7 (3 bits) and OAM sessions also contain the level specified by the operator.
The level determines whether or not an MIP or MEP stop responds to the OAM request: if the level of the request is less than the level configured on the MP, it is dropped.
If the level is equal, the MP responds to the request; if it is greater, the MP ignores it and proceeds to the next node.

OAM can be used for continuity checking, recording path, delay measurement, and PREOF function status monitoring.
In this guide, we will show examples for each use case.


## R2DTWO CLI interface

R2DTWO provides a command line interface (CLI) to access its OAM capabilities.
This interface can be accessed via the Telnet protocol.
For secure access, a Telnet-SSH proxy can be used that is not part of R2DTWO.

The Telnet CLI access is configured like a regular interface in the `[interfaces]` section of the config file.
However, it has a special interface type, `oam_cmd`.

Here the operator can specify an IP address and TCP port where to listen for the incoming Telnet session.
This is typically the management IP discussed above.

For example, this is how it looks like in the `nxp1.ini`:

```
cmd0 = oam_cmd ip=172.16.0.1 port=8000
```

Now R2DTWO listen on the 172.16.0.1:8000 TCP socket for incoming Telnet CLI sessions.
The __admin__ machine can access this since its also part of the management network.
After R2DTWO started, we can access the CLI with the following command.

```
(oam) root:scenario_oam# admin telnet 172.16.0.1 8000
Trying 172.16.0.1...
Connected to 172.16.0.1.
Escape character is '^]'.
OAM ready.
```

Make sure `telnet` is installed on your machine.
If the connection is successful, R2DTWO send the `OAM ready.` message for the client.
You can use the `help` command to list the available CLI commands.

__Important__: to terminate the telnet session properly use the `exit` or `quit` commands or the `Ctrl+D` keyboard shortcut.
Do not use the `Ctrl+C` since thats for terminating a running OAM command.


## The R2DTWO OAM configuration

In this guide, we discuss only the OAM-related parts of the configuration.
Most of the configuration remains the same as in IP46oDetNet (except for the IPv6-related parts, which have been removed).
We also split the `pw` stream to `pw1` and `pw2` with MPLS labels `1000` and `2000` respectively.

### Create Maintenance Points

We already discussed the concept of Maintenance Points (MPs).
The first step in R2DTWO OAM configuration is the MP creation.

As mentioned before, three types of MP supported: MEP Start, MIP and MEP Stop.
These are defined in the action pipeline of the `[streams]` configuration.
The syntax of the MP configuration are the following (from `doc/inispec.md`):

* `mep-start name level` Maintenance End Point, can initiate OAM messages
* `mep-stop name level [object]` Maintenance End Point, terminates an OAM monitoring route, can report status information about an object
* `mip name level [object]` Maintenance Intermediate Point, answers OAM messages, implicitly a mep-start point, can report status information about an object

The `name` can be arbitrary, it has meaning for the operator.
It has to be unique within a configuration: multiple MP with the same name not supported, since its also used as an ID for the MP.
The `level` as mentioned is a value from 0 to 7, it controls which MP will answer, pass and drop the OAM messages.
The `object` is an optional parameter: this can be a name of an instance from the `[objects]` section.
If given, the MIP or MEP Stop will be associated with that object.
This can be used to query the state (e.g.: conunter values, config parameters, error counters, etc.) of that object.
An object can be associated with multiple MIP or MEP Stop.

In this guide, we will configure 1 MEP Start, 4 MIPs and 1 MEP stop.
They will configured as in the following figure:

```
compound -> pw                                             pw -> compound
                       nxp1nni1            nxp2nni1
      │           ┌───────●───────────────────●───────┐           ▲
      │           │      MIP                 MIP      │           │
      ▼───nxp1uni─●                                   ●─nxp2uni───┘
         MEP Start│      MIP                 MIP      │ MEP Stop
                  └───────●───────────────────●───────┘
                       nxp1nni1            nxp2nni1
```


__Important__: MPs cannot configured for arbitrary streams or arbitrary places in the action pipeline.
To create a MP, the following conditions must be met:

* The packet's encapsulation is MPLS PseudoWire: the streams configured as `:packet = mpls, dcw, ...`
* If the packet's encapsulation is not MPLS PW, we have to use `add` and `del` actions to remove other headers and add `mpls` and `dcw`
* After MEP Start, there is a mandatory `edit` action in the pipeline which set the `label` field of the `mpls` header

With that in mind, lets see how it looks like for `nxp1.ini` action pipelines:

```
[streams]

compound:packet = eth, cvlan, ipv4
compound:match = cvlan tpid=ipv4, ipv4 src=10.0.100.0/24
compound:actions = gen, del eth, del cvlan, before ipv4 add dcw, before dcw add mpls bos=1, mep-start nxp1uni 3, prf prf-member1 prf-member2

prf-member1 = mip nxp1nni1 3, edit mpls.label=1000 mpls.bos=1, send nni_out0
prf-member2 = mip nxp1nni2 3, edit mpls.label=2000 mpls.bos=1, send nni_out1
```

This is the UNI part, we receiving Layer2 Ethernet packets from the host.
Before the `mep-start` action, we make the header stack OAM conform.
This is done by deleting the Layer2 headers, and adding the MPLS and dCW headers.
This is the mandatory encapsulation format for OAM as mentioned before.

Then we can create the MEP Start with the following action: `mep-start nxp1uni 3`.
Recommended to stick to naming convention for MP names, R2DTWO support arbitrary names.
Make sure these names are unique in the network, since they are MP IDs as well.

After the MEP Start and the replication action, we define two MIPs, one for each NNI path.
They done in the replication pipelines:

```
prf-member1 = mip nxp1nni1 3, edit mpls.label=1000 mpls.bos=1, send nni_out0
prf-member2 = mip nxp1nni2 3, edit mpls.label=2000 mpls.bos=1, send nni_out1
```

As one can see the names are different and we have the `edit mpls.label` after the `mip` definitions, which is mandatory.

Similarly, there are MPs defined in `nxp2.ini` as well.
The last parameter of the action definition is the OAM level.
In this example, we use value `3` everywhere, this can be anything from 0 to 7.
With that, every MP react to every OAM request with level 3.


## Maintenance and network discovery with the OAM

For moving forward, we need three terminals: two for the `r2dtwo` instances and one for the `telnet` application.

Start the R2DTWO instances:

```
# terminal 1:
(oam) root:scenario_oam# nxp1 r2dtwo nxp1.ini

# terminal 2:
(oam) root:scenario_oam# nxp2 r2dtwo nxp2.ini
```

From the __admin__ machine, login to the __nxp1__'s R2DTWO instance with `telnet`:

```
(oam) root:scenario_oam# admin telnet 172.16.0.1 8000
Trying 172.16.0.1...
Connected to 172.16.0.1.
Escape character is '^]'.
OAM ready.
```

To check if NNI path #1 and path #2 available, ping their MIPs on __nxp2__ from __nxp1__'s MEP Start.
This is done by the `ping` command, which in its simples form looks like `ping SOURCE TARGET LEVEL`:

```
ping nxp1uni nxp2uni 3
OAM request ping session 1 seq 0, nxp1uni -> nxp2uni level 3 count 1 interval 1000, rr: no os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:1 seq 0 lvl 3 R - ping on stream compound target nxp2uni; reply from nxp2uni
```

As one can see, we sent an OAM ping request from `nxp1uni` source to `nxp2uni` target.
We also received a reply which is the last line of the output (`... reply from nxp2uni`).
From that we know that __nxp2__ UNI is reachable through the network.
Implicitly we also know that the elimination on __nxp2__ working properly since we dont have duplicate replies.


### Record Route

This is an R2DTWO extension for OAM in FRER/PREOF environment.
We know that the UNI towards the __listener__ is reachable, but we not know the exact path, where we reaching it.

Normally on the IP networks we can use `traceroute` or `tracepath` to see the hops until our destination address.
In R2DTWO we have the `ping` command's `-r` argument, to record the route until the MIP.
This argument encoded in the OAM request, and every MIP see the request append it's MP IP to the request.
When the request reach the target MIP, that will send a reply to us with the list of the MP IDs recorded.

Let's see what happens:

```
ping nxp1uni nxp2uni 3 -r
OAM request ping session 2 seq 0, nxp1uni -> nxp2uni level 3 count 1 interval 1000, rr: yes os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:2 seq 0 lvl 3 R - ping on stream compound target nxp2uni; reply from nxp2uni
	Record Route: [ nxp1uni nxp1nni1 nxp2nni1 nxp2uni ]
```

In the last line we see the recorded MP IDs between the source and target MPs.
__Important__: right now the recorded route is path #1.
This is the actual faster path which might changes if the delay changes on the paths.
To quickly repeat the last command, use the `UP` (up arrow key) then the `Enter`.
With that you might see the OAM request might take the other path as well.


### Network discovery

It is useful the check all the MIPs on the network.
There is a special target for the `ping` command called `any`.
If a MIP see the `any` as the target, it will respond to that OAM request.

Let's see what happens if we `ping` target `any` in this network:

```
ping nxp1uni any 3
OAM request ping session 13 seq 0, nxp1uni -> any level 3 count 1 interval 1000, rr: no os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:13 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni1
  oam_r compound:13 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni2
  oam_r compound:13 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni1
  oam_r compound:13 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni2
  oam_r compound:13 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2uni
```

As one can notice, all MIPs responded to our ping request.
Important to mention, the order of the output not deterministic:
it represents the order of the reception of the replies, not the delay until the MIP.
However the delay measurement can be turned on with the `-d` ping argument:

```
ping nxp1uni any 3 -d
OAM request ping session 14 seq 0, nxp1uni -> any level 3 count 1 interval 1000, rr: no os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:14 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni1 delay 0.0
  oam_r compound:14 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni2 delay 0.0
  oam_r compound:14 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni1 delay 0.132095
  oam_r compound:14 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni2 delay 0.177154
  oam_r compound:14 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2uni delay 0.132095
```

As one can see, inside __nxp1__ we reach both MIPs almost immediately.
After that, as visible in the output we reached __nxp2__ first on path #1 (`nxp2nni1`), the delay is `0.132` sec.
Then we reached `nxp2nni2` MIP on the other path, but path #2 is slower (`0.177` sec right now).
Therefore the elimination only let the OAM request pass on the faster path, then drop it on the slower path.
As a result, we reached the `nxp2uni` in `0.132` sec as well.

To discover every possible path in the network, the `any` target can be used together with the `-r` option:

```
ping nxp1uni any 3 -r
OAM request ping session 15 seq 0, nxp1uni -> any level 3 count 1 interval 1000, rr: yes os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:15 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni1
	Record Route: [ nxp1uni nxp1nni1 ]
  oam_r compound:15 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp1nni2
	Record Route: [ nxp1uni nxp1nni2 ]
  oam_r compound:15 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni1
	Record Route: [ nxp1uni nxp1nni1 nxp2nni1 ]
  oam_r compound:15 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2nni2
	Record Route: [ nxp1uni nxp1nni2 nxp2nni2 ]
  oam_r compound:15 seq 0 lvl 3 R - ping on stream compound target any; reply from nxp2uni
	Record Route: [ nxp1uni nxp1nni1 nxp2nni1 nxp2uni ]
```

Again, the output of this command depends on the current path delay and might changes with it.


## Automatic MIP configuration

For a complicated network with lots of eliminations, replications and redundant paths manual configuration of MPs is impractical.
R2DTWO support automatic MIP configuration.
This function create MIPs before and after replication, elimination and ordering functions.

To enable this, edit the object definitions in `nxp1.ini` and `nxp2.ini` and add the `AutoMIP` parameter.
The limitation of the option is we only define one level for each MIPs per-object.
The level can be set as a parameter e.g.: `AutoMIP=5` where each MIP will be configured with level 5 for that object.
Edit the configs as the example below:

```
...
pef = SeqRcvy frerSeqRcvyHistoryLength=16 AutoMIP=5
prf = Replicate AutoMIP=5
...
```

Now logout from telnet with the `exit` command and restart the R2DTWO instances with the new config.
Then connect to the telnet CLI again and list the MPs with the `list` command:

```
(oam) root:scenario_oam# admin telnet 172.16.0.1 8000
Trying 172.16.0.1...
Connected to 172.16.0.1.
Escape character is '^]'.
OAM ready.
list
Available MEP Start points:
nxp1nni1 level 3 in pipe prf-member1 at pos 2
nxp1nni2 level 3 in pipe prf-member2 at pos 2
nxp1uni level 3 in pipe compound at pos 7
o_compound_L5_post-pef level 5 in pipe pw1 at pos 5
o_compound_L5_pre-prf level 5 in pipe compound at pos 8
o_prf-member1_L5_post-prf level 5 in pipe prf-member1 at pos 1
o_prf-member2_L5_post-prf level 5 in pipe prf-member2 at pos 1
o_pw1_L5_pre-pef level 5 in pipe pw1 at pos 3
o_pw2_L5_pre-pef level 5 in pipe pw2 at pos 3
```

There are two takeaways in this output:
1. Automatic MIPs are listed at MEP Start points in the output.
We also see the manually configured MIPs as MEP Starts as well.
This is normal because R2DTWO implicitly generate a MEP Start for each MIP.
2. Automatic MIP names (MP IDs) generated following this scheme: `o_STREAM_Llevel_{post|pre}-OBJNAME`.
The `o_` prefix in the MP ID quickly tells this is an automatically generated MIP.
The `pre` or `post` before the object name distinguish between the placement of the MIP.
For example, before an elimination, we can have as much MIPs as many member streams, but only one after.
The replication is the opposite: as much `post` MIPs generated as much member streams used for the replication.

To discover all the auto-generated MIPs, we can use the `any` target in the ping again.
However we have to use level 5 since we defined `AutoMIP=5` for each object:

```
ping o_compound_L5_pre-prf any 5 
OAM request ping session 1 seq 0, o_compound_L5_pre-prf -> any level 5 count 1 interval 1000, rr: no os: no	[reply to ip: 172.16.0.1, port: 6634]
  oam_r compound:1 seq 0 lvl 5 R - ping on stream compound target any; reply from o_prf-member1_L5_post-prf
  oam_r compound:1 seq 0 lvl 5 R - ping on stream compound target any; reply from o_prf-member2_L5_post-prf
  oam_r compound:1 seq 0 lvl 5 R - ping on stream compound target any; reply from o_pw1_L5_pre-pef
  oam_r compound:1 seq 0 lvl 5 R - ping on stream compound target any; reply from o_pw2_L5_pre-pef
  oam_r compound:1 seq 0 lvl 5 R - ping on stream compound target any; reply from o_compound_L5_post-pef
```

## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
