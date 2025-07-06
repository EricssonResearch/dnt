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
  ───▷ Has pointer to                       ║                      OAM                      ║
                                            ║ ┌──────────────────┐    ┌──────────────────┐  ║
  ───▶ Has pointer to and holds reference   ║ │     Sessions     │    │     MP hash      │  ║
                                            ║ └────────┬─────────┘    └────────┬─────────┘  ║
                                            ║          │                       │            ║
                                            ║ ┌────────▽─────────┐    ┌────────▽─────────┐  ║
╔════════════════════════╗                  ║ │     Request      ├────▶ MaintenancePoint │  ║
║         State          ║                  ║ └──────────────────┘    └──▲────────────┬──┘  ║
║                        ║                  ╚════════════════════════════╪════════════╪═════╝
║ ┌───────────────────┐  ║   ┌──────────────────┐              ┏━━━━━━━━━┷━━━━━━━━┓   │
║ │   Objects hash    ├──╫───▶  PipelineObject  ◀──────┐       ┃    OAM action    ┃   │
║ └───────────────────┘  ║   └──────────────────┘      │       ┠──────────────────┨   │
║                        ║                             └───────┨ Stateful action  ┃   │
║ ┌───────────────────┐  ║   ┌──────────────────┐              ┠──────────────────┨   │
║ │  Interfaces hash  ├──╫───▶    Interface     ◀──────────────┨   Send action    ┃   │
║ └───────────────────┘  ║   └────────┬─────────┘              ┠──────────────────┨   │
╚════════════════════════╝            │                        ┃      Action      ┃   │
                                      │                ┌───────▶     Pipeline     ◁───┘
                             ┌────────▽─────────┐      │       ┗━━━━━━━━━━━━━━━━━━┛
                             │    ParseTree     ├──────┤
                             └──────────────────┘      │       ┌──────────────────┐
                                                       └───────▷ HeaderDescriptor │
                                                               └──────────────────┘
```

The main state storage is the *State* module in `state.c`.
It keeps two hashmaps:

* Objects holds the PipelineObjects (SeqGen, SeqRcvy etc.)
* Interfaces holds the interfaces

These hashes own the items in them, so their reference count is 1 when they are unused.

*Interface* objects handle sending/receiving packets, their public interface is in `interface.h`.
Interfaces that can receive packets have a ParseTree (defined in `parsetree.h`) to process it.

*ParseTree* analyzes the received packet, identifies headers in it, and matches field values to see which stream it belongs to.
It has a list of registered streams, each stream is composed of two parts:

* HeaderDescriptor is a linked list of header types, and associated field match statements, if any
* Pipeline is an array of *Action* objects that are the processing steps we must do on the received packet (it also carries the name of the stream)

If a stream is received on multiple interfaces, the Pipeline is de-duplicated: each ParseTree holds a reference to a common shared Pipeline.
On the other hand, the HeaderDescriptor list is copied, because it's difficult to reference count a linked list.

The name ParseTree was chosen because originally we envisioned it as a decision tree, like [Panda parser](https://github.com/panda-net/panda/blob/main/documentation/parser.md).
In the end it became a simple list of streams.

The *Pipeline* contains the actions that must be performed on the packet.
The actions (defined in `action.h`) are designed to be minimal and *stateless*: their result only depends on the current packet, so there is no inter-packet dependency and the same Pipeline can be used by multiple threads to process packets at the same time.

Some actions need to keep state between packets, such actions use a *PipelineObject* to keep their state.
These actions hold a reference to their state object, multiple actions can refer to the same object.
For example, Elimination needs a Sequence Recovery object, and when multiple Elimination actions refer to the same object, that becomes a single elimination point.

The *Interface* has two types of references:

* Interface owner: held by the hashmaps that own the Interface
* Interface sender: held by Send actions that use the Interface

This distinction is needed for a proper create, open, close and shutdown life-cycle.
The Interface only disappears, when its both reference types are zero.

The *OAM* module performs Operation, Administration and Maintenance functions.
It can send/receive probe messages to monitor the network using *MaintenancePoint* objects that are owned by the OAM actions:

* OAM Injector can put a monitoring message into the pipeline that will go in-band with the data packets
* OAM Receiver can detect that a packet is an in-band OAM message, and hands it over to the OAM module

Note that these two actions are not found in the configuration.
A `mep-start` action is translated to an Injector, a `mep-stop` action is translated to a Receiver, and a `mip` action is translated to a Receiver and an Injector.

The *Sessions* registry keeps track of these messaging sessions, each active session is described with a *Request* structure.

The *OAM* module also has a hashmap that keeps track of the MaintenancePoint objects, but this is only used for enumeration, the owner of the MaintenancePoint objects is the OAM action.


## Packet processing

**TODO** receive, parse, match to identify stream, get Pipeline, run PipelineIterator (automatically disposes the packet and itself)


## OAM module

**TODO** components: command, maintenance, message, request, session, core

**TODO** threads: in-band receive, out-of-band receive, command connection


## Configuration

**TODO** config file -> transaction -> commit

**TODO** ConfAction -> Action compilation

**Important note** the transaction commit mechanism is unfinished, it can properly start up and shut down R2DTWO, but incremental configuration during runtime is not yet possible


## Tutorial on adding a new action

**TODO** start by adding an item in the `enum ConfActionType`, then check the compile errors

