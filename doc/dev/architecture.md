# R2DTWO architecture

This document gives an overview of the components of R2DTWO and the way they are connected to each other.

The objects that R2DWTO is composed of are all identified by their names, which are often used as hash keys.


## Reference Counting

In C there is no automatic memory management, it is the responsibility of the programmer to keep track of the dynamically allocated objects.
The usual solution is to define ownership relations, where the owner object must dispose of the owned objects when they are no longer needed.

In R2DTWO the establishment of ownership relations are assisted with a reference-counting scheme.
Each object that is a *shared resource* has a `reference_count` member that keeps track of the number of its users.
These object types have a `TYPE_ref()` member to acquire a reference, and a `TYPE_unref()` member to release the reference.
The reference-counted object types don't have a `delete()` method, they are automatically disposed of when their reference count reaches zero.


## Architecture overview

The overall architecture of R2DTWO is shown in the figure below:

```
                                            ╔═══════════════════════════════════════════════╗
                                            ║                      OAM                      ║
  ───▷ Has pointer to                       ║ ┌──────────────────┐    ┌──────────────────┐  ║
                                            ║ │     Sessions     │    │     MP hash      │  ║
  ───▶ Has pointer to and holds reference   ║ └────────┬─────────┘    └────────┬─────────┘  ║
                                            ║          │                       │            ║
                                            ║ ┌────────▽─────────┐    ┌────────▽─────────┐  ║
                                            ║ │     Request      ├────▶ MaintenancePoint │  ║
╔════════════════════════╗                  ║ └──────────────────┘    └──┬─────▲──────┬──┘  ║
║         State          ║                  ╚════════════════════════════╪═════╪══════╪═════╝
║                        ║               ┌───────────────────────────────┘     │      │
║ ┌───────────────────┐  ║   ┌───────────▼──────┐              ┏━━━━━━━━━━━━━━━┷━━┓   │
║ │   Objects hash    ├──╫───▶  PipelineObject  ◀──────┐       ┃    OAM action    ┃   │
║ └───────────────────┘  ║   └──────────────────┘      │       ┠──────────────────┨   │
║                        ║                             └───────┨ Stateful action  ┃   │
║ ┌───────────────────┐  ║   ┌──────────────────┐              ┠──────────────────┨   │
║ │  Interfaces hash  ├──╫───▶    Interface     ◀──────────────┨   Send action    ┃   │
║ └───────────────────┘  ║   └────────┬─────────┘              ┠──────────────────┨   │
╚════════════════════════╝            │                        ┃      Action      ◁───┘
                                      │                ┌───────▶     Pipeline     ┃
                             ┌────────▽─────────┐      │       ┗━━━━━━━━━━━━━━━━━━┛
                             │    ParseTree     ├──────┤
                             └──────────────────┘      │       ┌──────────────────┐
                                                       └───────▷ HeaderDescriptor │
                                                               └──────────────────┘
```

The main state storage is the *State* module in `state.c`.
It keeps two hashmaps:

* Objects holds the PipelineObjects (SeqGen, SeqRcvy etc.)
* Interfaces holds the Interface objects

These hashes own the items in them, so their reference count is 1 when they are unused.

*Interface* objects handle sending/receiving packets, their public interface is in `interface.h`.
Interfaces that can receive packets have a ParseTree (defined in `parsetree.h`) to process it.

*ParseTree* analyzes the received packet, identifies headers in it, and matches field values to see which stream it belongs to.
It has a list of registered streams, each stream is composed of two parts:

* *HeaderDescriptor* is a linked list of header types, and associated field match statements, if any
* *Pipeline* is an array of *Action* objects that are the processing steps we must do on the received packet (it also carries the name of the stream)

If a stream is received on multiple interfaces, the Pipeline is de-duplicated: each ParseTree holds a reference to a common shared Pipeline.
On the other hand, the HeaderDescriptor list is copied, because it's difficult to reference count a linked list.

The name "ParseTree" was chosen because originally we envisioned it as a decision tree, like [Panda parser](https://github.com/panda-net/panda/blob/main/documentation/parser.md).
In the end it became a simple list of streams.

The *Pipeline* contains the actions that must be performed on the packet.
The actions (defined in `action.h`) are designed to be minimal and *stateless*: their result only depends on the current packet, so there is no inter-packet dependency and the same Pipeline can be used by multiple threads to process packets at the same time.

Some actions need to keep state between packets, such actions use a *PipelineObject* to keep their state.
These actions hold a reference to their state object, multiple actions can refer to the same object.
For example, Elimination needs a Sequence Recovery object, and when multiple Elimination actions refer to the same object, that becomes a single elimination point.

The *OAM* module performs Operation, Administration and Maintenance functions.
It can send/receive probe messages to monitor the network using *MaintenancePoint* objects that are owned by the OAM actions:

* OAM Injector can put a monitoring message into the pipeline that will go in-band with the data packets
* OAM Receiver can detect that a packet is an in-band OAM message, and hands it over to the OAM module

Note that these two actions are not found in the configuration.
A `mep-start` action is translated to an Injector, a `mep-stop` action is translated to a Receiver, and a `mip` action is translated to a Receiver and an Injector.
Multiple OAM actions can reference the same *MaintenancePoint* state object, in this case they are considered to be the same OAM maintenance point.

The *Sessions* registry keeps track of the OAM messaging sessions, each active session is described with a *Request* structure.

The *OAM* module also has a hashmap that keeps track of the MaintenancePoint objects, but this is only used for enumeration, the owner of the MaintenancePoint objects is the OAM action.

The MaintenancePoint objects can be associated with a PipelineObject to monitor and report its state, this association manifests in a reference held by the MaintenancePoint.
When the AutoMIP option is enabled on a PipelineObject, the generated MIPs will be associated with that object.


## Packet processing

The main thread of R2DTWO is just an idle loop waiting for the interrupt or terminate signal.
The *Interface* objects that can receive packets each run their own receive threads, and the packet processing happens in those threads.
This means that all packet processing code must be thread-safe.

There is a common send-receive infrastructure found in `if_utils.h` that is used by almost all Interface types.
It takes care of Packet allocation, receiving on the socket, timestamping, and error handling.

There is a limit on how many packets can exist in R2DTWO at the same time to prevent memory exhaustion.
If the limit is reached, the incoming packets are read into a dummy buffer that is never processed, because we have to drain the receive buffer of the socket.
Currently only Delay and POF modules can buffer packets, so those are the ones to watch out for when R2DTWO starts issuing warnings about packet overflow.

Once a Packet has been received there are two distinct phases of processing:

1. *Classify*: find a matching *stream* for the packet
2. *Process*: execute the action pipeline corresponding to the *stream*

It is important that there is no second classification phase.
If another stream classification is needed, e.g. after a tunnel decapsulation, the packet can be sent to an *internal* interface where the receive function does a classification based on the registered streams.

The Classify phase is done by the ParseTree object, each Interface has one.
It takes the entries configured by the `ifname:streams` line, and probes each one until it finds a match.
The result of the match is that the `headers` array in Packet is filled with the protocol stack of the stream, and a *PipelineIterator* is returned, which holds the Packet, and a reference to the Pipeline.

The *PipelineIterator* manages the packet processing on the action pipeline.
The actions are in an array, and the iterator calls their `execute()` function one-by-one.
That function can return one of the following instructions:

* `Continue`: go to the next action
* `Done`: finish processing and dispose of the Packet, e.g. the Eliminate action has eliminated this duplicate
* `Hold`: stop processing for now, the Packet is held in a queue (POF) or handed over to a slow-path (OAM)

When a Hold happens the receive thread of the Interface moves on to get the next packet from the socket.
If the action decides to continue processing the packet, it will use its own thread to resume the iterator.

When the PipelineIterator finishes processing (either by reaching the end of the pipeline or receiving a Done result) it releases the Pipeline reference, and disposes of the Packet and itself automatically.


## Interface life-cycle

The *Interface* objects have two types of references that are needed to manage their life-cycle:

* Owner: held by objects that own the Interface, during normal operation it's just the hash of the State manager (`iface_ref` and `iface_unref`)
* Sender: held by Send actions that use the Interface to send packets (`iface_add_sender` and `iface_del_sender`)

The life-cycle of Interface objects has multiple phases.

* Init: the `new_XXX_interface` constructors just allocate and initialize the object, but don't create the socket, because we want to populate the ParseTree with the appropriate streams first
* Open: the `open` method of the object creates and opens the socket, so the packet reception can begin, this is the normal working state of the interfaces
* Shutdown: when the owner reference count reaches zero, but there are still Send actions that use it, the object gets into this state, where it no longer receives packets, but still allows sending

The Interface object only destructs itself, when both of its references reach zero.

The Shutdown state is final, there is no way of re-opening the interface.

The `udp-out` interface can be in a semi-open state, when its target IP address is not yet known.
In this state the socket not yet created, and the interface blocks sending.
The interface can return to this state, if the receiver `udp-in` signals that it has no address to receive on.


## OAM module

The Operation Administration and Maintenance (OAM) is a slow-path module running in the background.
It has two background threads that receive messages:

* In-band receiver: the OAM Receiver actions hand over the matching packets for inspection
* Out-of-band receiver: the `oam` and `oam_eth` interfaces receive out-of-band replies to the in-band requests (ping, rlist etc.)



**TODO** components: command connection, maintenance points, message handlers, request structure, session database, core utilities

**TODO** threads: in-band receive, out-of-band receive, command connection

**TODO** interfaces: command, return (no user traffic, but otherwise the same life-cycle)

## Configuration

**TODO** config file -> transaction -> commit

**TODO** ConfAction list -> Action array compilation

**TODO** startup and shutdown procedure

**Important note** the transaction commit mechanism is unfinished, it can properly start up and shut down R2DTWO, but incremental configuration during runtime is not yet possible (missing: remove streams, rollback on error)


## Tutorial on adding a new action

**TODO** start by adding an item in the `enum ConfActionType` in *conf_actions.c*, then check the compile errors (no default in switch-case), then do the same with `enum ActionType` in *action.h*

**TODO** adding a new PipelineObject ?
