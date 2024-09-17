# R2DTWO path masking

R2DTWO has a runtime path masking feature. This allows replication pipelines to be masked through the Telnet CLI interface on a running R2DTWO instance.
These tests should verify that R2DTWO's path masking feature is working properly.

There are two versions of the test topology: single node and multi node.
The single-node version is useful for testing because it does not require a virtual network setup.
All traffic is generated locally, and all replication and recovery graph elimination functions are implemented within an R2DTWO instance.

The multi-node version implements the same recovery graph, but with a more realistic, virtual network scenario.
Here, each replication and elimination function is implemented on different nodes, so that real network transmissions take place.

## Network setup

```
┏━━━┓                                             ┏━━━━┓
┃ t ┃                                             ┃ l  ┃
┗━┯━┛                                             ┗━▲━━┛
  │         M1         ┌────┐  M4                   │
  └─────┐ ┌────────────▶ R2 ●────────────┐          │
        │ │            └─●──┘            │          │
        │ │              │ M5            │          │
      ┌─▼─●┐   M2        │             ┌─▼──┐ End ┌─┴──┐
      │ R1 ●─────────────┼─────────────▶ E2 ●─────▶ O  │
      └───●┘             │             └─▲──┘     └────┘
          │              │               │
          │ M3         ┌─▼──┐  M6        │
          └────────────▶ E1 ●────────────┘
                       └────┘

   ╔══════════════════════════════════════════╗
   ║AutoMIP level = 3                         ║
   ║Naming: o_STREAM_LEVEL_{pre|post}-OBJNAME ║
   ╚══════════════════════════════════════════╝
```

The recovery graph above has two replications, two eliminations, and an ordering function.
The same network was used to test both local masking and mask signaling.
Local masking means instantaneous masking and unmasking of a replication pipeline.
This can be done through the Telnet CLI using the mask/unmask commands.
It is called local because path masking occurs only on the local node and does not affect other nodes on the network.
As a result, the elimination object's sequence recovery might detect the missing packets and report it as an error.
However, if the diagnostic or latent error feature of the sequence recovery is disabled, this will not cause any problems.

Mask signalling has been introduced to overcome the false-positive errors.
When mask signaling is enabled, a mask OAM request is generated immediately after the mask CLI command.
This message notifies the subsequent sequence recovery to treat this path as a masked path and update the latent error paths accordingly.
In the example above, the E2 has three paths and the mask M2 command instructs the E2 to change the available paths to two.

