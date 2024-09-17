# Scenario #6: Replication path masking

__Important: this scenario assumes background knowledge from scenarios TSN, TSNoDetNet, IP46oDetNet and OAM.
Please follow the instructions of those if you have not already.__

This scenario illustrate the R2DTWO path masking functionality.
We use the OAM getting started as our base scenario.

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

Everything identical to the OAM scenario regarding the network topology.
Every changes compared to the OAM scenario is in the R2DTWO configuration.


## Replication path masking intro

By default, the replication function of both FRER and PREOF (we will refer to both as PRF for simplicity) replicates every frame unconditionally.
This means that if we have configured 3 replication paths, R2DTWO PRF will send a replica packet on each path during the whole operation time.
However, this may not be the desired operation.
The administrator may want to use only one path as active path and the rest as backup paths.
In our terms, the PRF has one unmasked and two masked paths.
In the following list, we will mention some advantages of such a configuration:

* The configuration of the PRF is done only at initialization, and path changes can be done quickly at runtime.
* Network resources can be allocated and released according to the needs of the application (e.g.: if one path is good enough, we can mask the rest)
* It is useful for testing purposes to mask or unmask PRF paths (e.g.: test PEF reset, debug stream identification, etc.).

The motivation for this feature came from a 5G cellular use case.
There were two 5G radio paths that were used by the PRF all the time.
However, testing confirmed that most of the time one path was good enough, especially when the UE was stationary.
When the UE started to move, the antenna orientation and radio coverage changed.
In this situation, the increased reliability provided by the PRF is beneficial to the application.
But most of the time, we can save radio resources by masking one path.

## The R2DTWO path masking

In R2DTWO there are two types of path masking: _local path masking_ and _signaled path masking_.
Local path masking (or local masking for short) means that PRF mask the path but subsequent does not notified by the situation.
This is important, if the subsequent PEFs of the recovery graph running periodical diagnostic.
Both 802.1CB Latent error detection or R2DTWO diagnostic entity report false positive errors for locally masked paths.
These functions has no way to tell if the path is actually failing or just masked by the administrator.

On the other hand, signaled masking intended to cope with such false positive error situations.
This is done by sending OAM messages on the masked path about the fact of it is masked.
From the operator's viewpoint, both looks the same: masking can be done in telnet CLI with the `mask PRFPIPELINE` command where the `PRFPIPELINE` is a pipeline argument of an existing `Replicate` object.

### Signaled path masking

Here we discuss the signaled masking in more details, since its operation requires some attention.
As we will see later in this guide, mask signaling done by the AutoMIPs (introduced in the OAM guide).
Right here we focusing on the operation rather than the configuration.
First let's see what happens in a masking scenario on the PRF and PEF:

__PRF signaled masking steps__:

1. Operator mask the pipeline in the telnet CLI (new masking OAM session established)
2. The auto-generated post-MIP associated with the PRF on the masked path generate a OAM message type `mask`
3. This mask signal is link-local meaning its only sent to the neighbor auto-generated MIP
4. Not auto-generated MIPs of the stream ignore the message
5. The PRF generate a mask signal in every 1 second

__PEF signaled masking steps__:

1. The PEF's auto-generated pre-MIP receive the mask signal OAM message
2. It checks if that path already considered masked or not (unmasked)
3. Since its unmasked (for example sake) the pre-MIP will notify the PEF about the mask signal
4. The PEF will decrease its `frerSeqRcvyLatentErrorPaths` value, which is used by the latent error detection and the diagnostic entity
5. With that the PEF know how much unmasked path its have so it can report actual path anomalies
6. The mask signal not propagated by the PEF's pre-MIP and dropped after the processing of it

This is works as a heartbeat (or keep-alive) mechanism: the PEF's pre-MIP store the timestamp when it see the last mask signal.
If the last mask heartbeat __older than 3 seconds__ the pre-MIP consider the path unmasked.
It also notify the PEF so it can increase the `frerSeqRcvyLatentErrorPaths`.

__Important:__ if the latent error test period or the diagnostic period less than 1 second, that can generate false positive errors as well.
Make sure its configured larger than 1 second, recommended to configure it larger than 3 seconds.
Also there is a check in PEF to ensure `frerSeqRcvyLatentErrorPaths` value cannot go below 0 or over the value configured in the config.
The operator notified about both case, these considered faulty scenarios.
For example below 0 can happen if PEF configured with smaller `frerSeqRcvyLatentErrorPaths` than the actual ingress member pipelines.

### Unmask

For re-enable the replication transmission on a masked path, we have the `unmask` command.
The operation is similar to the mask command, see below:

__PRF signaled unmask__:

1. The operator unmask the pipeline in the telnet CLI
2. The mask heartbeat generated by the associated PRF's post-MIP stopped immediately
3. An OAM `unmask` signal generated by the same post-MIP

__PEF signaled unmask__:

1. If the replication path is working, the PEF's pre-MIP receive the unmask signal (act up on it immediately)
2. If not, the pre-MIP simply notice the mask heartbeat timeout (as some sort of a delayed unmask)
3. In both case, the pre-MIP notify the PEF about the unmask signal
4. The PEF increase the `frerSeqRcvyLatentErrorPaths`


### Mask and unmask signal re-generation

For sake of simplicity, the above scenarios explained for a simple scenario where we have one PRF and one PEF in the network.
In more complex network, we have arbitrary number of PRFs and PEFs with many disjoint redundant connections.
Signaled mask/unmask works well in those scenarios as well, with the following simple rules.
These are the mask/unmask signal re-generation rules, meaning automatic establishment of new mask OAM sessions.

__PRF rules__:

1. Forward the mask/unmask signal

__PEF rules__:

1. If all of the PEF's paths masked, the PEF will re-generate the mask signal with its post-MIP
2. If the PEF in state mask regenerate state and its pre-MIP receive a new unmask signal from one of its paths, the PEF's post-MIP stop it's mask OAM session and generate a new unmask signal immediately

With these rules, the path masking will work on arbitrary complex recovery graph with many PRFs and PEFs.
These rules are enforced by the R2DTWO signaled masking operation, no additional configuration required.


## Configuration and path masking

First lets observe the configuration files `nxp1.ini` and `nxp2.ini`.
As one can see, there are no manually configured nor automatically generated MIPs.
That means signaled path masking not available, only local masking possible.

To try out local masking, start the test environment.
We need four terminal windows: 1-1 for the R2DTWO instances on __nxp1__ and __nxp2__, 1 for traffic generator and 1 for the telnet CLI.

First, start the R2DTWO instances with packet level logging enabled:

```
# nxp1
nxp1 r2dtwo nxp1.ini -vPACKETTRACE:ALL

# nxp2
nxp2 r2dtwo nxp2.ini -vPACKETTRACE:ALL
```

Then from a terminal telnet into `nxp1`' CLI from the admin machine:

```
(oam) root:scenario_mask# admin telnet 172.16.0.1 8000
Trying 172.16.0.1...
Connected to 172.16.0.1.
Escape character is '^]'.
OAM ready.
```

From another terminal start to generate some traffic from __talker__ to __listener__.
This can be done e.g. `ping` but with that we would see the backward traffic as well.
To avoid that there is a very small python script `traffic.py`.
This generate a small UDP message to port 5555 of the IP given in argument in every 0.5 second.

```
(mask) root:scenario_mask# talker ./traffic.py 10.0.200.22
```

After running the traffic generator, we can observe the output of R2DTWO on __nxp2__:

```
...
2024.09.17 10:38:02 [PACKETTRACE] [PACKET] [id=10 oid=10] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (5 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:02 [PACKETTRACE] [PACKET] [id=11 oid=11] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (5 drop) 
2024.09.17 10:38:03 [PACKETTRACE] [PACKET] [id=12 oid=12] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (6 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:03 [PACKETTRACE] [PACKET] [id=13 oid=13] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (6 drop) 
2024.09.17 10:38:03 [PACKETTRACE] [PACKET] [id=14 oid=14] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (7 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:03 [PACKETTRACE] [PACKET] [id=15 oid=15] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (7 drop) 
2024.09.17 10:38:04 [PACKETTRACE] [PACKET] [id=16 oid=16] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (8 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:04 [PACKETTRACE] [PACKET] [id=17 oid=17] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (8 drop) 
2024.09.17 10:38:04 [PACKETTRACE] [PACKET] [id=18 oid=18] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (9 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:04 [PACKETTRACE] [PACKET] [id=19 oid=19] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (9 drop) 
2024.09.17 10:38:05 [PACKETTRACE] [PACKET] [id=20 oid=20] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (10 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:05 [PACKETTRACE] [PACKET] [id=21 oid=21] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (10 drop) 
2024.09.17 10:38:05 [PACKETTRACE] [PACKET] [id=22 oid=22] nni_in0 41 pw1 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (11 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:38:05 [PACKETTRACE] [PACKET] [id=23 oid=23] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (11 drop) 
...
```

As visible in the output, R2DTWO receive ingress message on both NNI interfaces `nni_in0` and `nni_in1`.
Likewise they match on stream `pw1` and `pw2`.
Also visible in the packet log the PEF drop half of the packets, since these are the replica packets.

Without stopping the traffic generator, lets mask one of the replication pipeline on __nxp1__.
As visible in the config file, we have the following PRF definition: `prf prf-member1 prf-member2`.
Let's mask `prf-member1` replication pipeline with the following command in telnet CLI:

```
mask prf-member1
```

Right after executing the command, we see two messages:

```
Pipeline 'prf-member1' masked
mep start not found for 'mask' command
```

These might be confusing, but this is the expected behavior.
The first message tells the path masking was successful (since the pipeline exist and not masked already).
The second message tells we dont have MEP Start for mask command.
Indeed, we dont have any MPs configured, therefor this is a local masking.

After the command executed, we can immediately verify from the packet logging PRF dont send replica packets on `prf-member1` anymore.
On __nxp2__ we also verify that we only see ingress packets from one NNI:

```
2024.09.17 10:46:38 [PACKETTRACE] [PACKET] [id=55 oid=55] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (1037 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:46:39 [PACKETTRACE] [PACKET] [id=56 oid=56] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (1038 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:46:39 [PACKETTRACE] [PACKET] [id=57 oid=57] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (1039 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:46:40 [PACKETTRACE] [PACKET] [id=58 oid=58] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (1040 pass) Del FilterOAM Del Send uni_out 
2024.09.17 10:46:40 [PACKETTRACE] [PACKET] [id=59 oid=59] nni_in1 41 pw2 |mpls|dcw|ipv4|payload| TTLReduce ReadSeq Eliminate (1041 pass) Del FilterOAM Del Send uni_out
```

After a while, we see periodical WARNING messages on __nxp2__ output:

```
...
2024.09.17 10:46:38 [DIAGNOSTIC] [WARNING] pef: Latent error signal
2024.09.17 10:46:38 [DIAGNOSTIC] [WARNING] pef: DISFUNCTIONING_PATHS: 1 path(s)
...
2024.09.17 10:46:38 [DIAGNOSTIC] [WARNING] pef: DISFUNCTIONING_PATHS: 1 path(s)
...
```
These messages generated by the PEF object's anomaly detection functions.
The `Latent error signals` generated by standard 802.1CB Latent error detection function.
The `DISFUNCTIONING_PATHS` warnings generated by the R2DTWO anomaly detection extension.
To make them disappear, let's unmask the path with the following command:

```
unmask prf-member1
```

After that PRF will use both NNI paths to packet transmission.

But as mentioned before, these warnings are false-positive reports: we intentionally masked the path.
As mentioned, this false-positive warning can be solved with signaled masking.
To enable signaled masking, we have to enable auto-generated MPs for PRF and PEF objects:

```
# in nxp1.ini and nxp2.ini

pef = SeqRcvy AutoMIP=5 frerSeqRcvyHistoryLength=16 ...
prf = Replicate AutoMIP=5
```

With adding `AutoMIP` argument to the objects, we enabled automatic MP generation.
Let's restart both R2DTWO instances to see them.
After the restart, verify if the auto-generated MIPs are there from the telnet CLI with the `list` command:

```
list
Available MEP Start points:
o_compound_L5_post-pef level 5 in pipe pw1 at pos 5
o_compound_L5_pre-prf level 5 in pipe compound at pos 8
o_prf-member1_L5_post-prf level 5 in pipe prf-member1 at pos 1
o_prf-member2_L5_post-prf level 5 in pipe prf-member2 at pos 1
o_pw1_L5_pre-pef level 5 in pipe pw1 at pos 3
o_pw2_L5_pre-pef level 5 in pipe pw2 at pos 3
```

This is the output on __nxp1__ and it shows we have the auto-generated MIPs.
The important here are the post-PRF MIPs since these do the OAM mask signaling.
Now mask the `prf-member1` again as before (make sure the traffic generator is still running).
As visible in the output of __nxp2__ we dont see the `DISFUNCTIONING_PATHS` warnings anymore.
This is exactly because the signaled masking update the `frerSeqRcvyLatentErrorPaths` value of the PEF as paths masked/unmasked.


## Optional: Cleanup (if running locally, not on physical NXP boards)

Since every network configuration used in this guide sandboxed, we only have to exit from the sourced environment in every terminal window.
To do that use the `exit` command or press `Ctrl+D`.
When we exit from the last environment, that will clean up the network namespaces and every other network configuration related to the test environment.

__Important: do not run multiple test scenarios at the same time! Make sure you are exited from the test environment in every terminal before sourcing a new environment in for a new test scenario!__
