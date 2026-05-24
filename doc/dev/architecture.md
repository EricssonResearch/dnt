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

When entering the Shutdown phase the Interface drops its incoming streams (they are reference-counted).
Therefore, the Shutdown state is final, there is no way of re-opening the Interface.

The Interface object only destructs itself, when both of its references reach zero.

The `udp-out` interface can be in a semi-open state, when its target IP address is not yet known.
In this state the socket not yet created, and the interface blocks sending.
The interface can return to this state, if the receiver `udp-in` signals that it has no address to receive on.


## OAM module

The Operation Administration and Maintenance (OAM) is a slow-path module running in the background, its public API is in `oam.h`.
It has two background threads that receive messages:

* In-band receiver: the OAM Receiver actions hand over the matching packets for inspection
* Out-of-band receiver: the `oam` and `oam_eth` interfaces receive out-of-band replies to the in-band requests (ping, rlist etc.)

The in-band OAM messages are requests that must be answered if the interceptor OAM monitoring point is the target of the request, the target is `any`, or the TTL of the packet has expired (not for TSN).
The reply is sent to the address specified in the request.

If the OAM monitoring point forwards the in-band message the action processing on the packet is continued in the receiver thread.
This means that the ordering between the data packets and the OAM packets is not preserved.

The replies are sent out-of-band with sockets not belonging to any Interface.
The received replies are matched against the initiated requests, and the results are reported to the command connection that has initiated the request.
The database of the active OAM request sessions is implemented in `oam_session.c`.

The `oam_cmd` interface type allows only one instance, but it can handle multiple simultaneous command connections.
Each command connection runs in a separate background thread, the OAM request messages are injected into the action pipelines from these threads.

The OAM functionality of R2DTWO is based on standardized technologies (e.g. RFC 9546, IEEE 802.1ag), but most of it (monitoring point architecture, message sequences, message formats) is non-standard, and it's only compatible with itself.
In this implementation the messages are encoded as JSON (ECMA-404).


## Configuration

The configuration processing infrastructure is completely separate from the packet processing of R2DTWO.
It has several components, each corresponding to a section in the configuration file.

The most complex processing is for the `actions` line of the streams in `conf_actions.c`.
It uses the `process_stages` and `process_tokens` helpers from the `conf_utils` module to build a linked list of `ConfAction` structures.
One stage corresponds to one action (they are separated by commas), and the tokens are its name and parameters (separated by spaces).

The two main functions in `conf_actions.c` are

* `process_token` receives the parts of the action one-by-one, and fills the corresponding data in the `ConfAction` structure
* `process_action` is called by `process_stages` after all the tokens have been processed to perform a final processing and verification on the action

Some actions do most of their processing in `process_token`, while others do it in `process_action`, there is no general pattern to this.
Note that error checking should be performed as soon as possible.

The configuration file is processed into a `Transaction` structure that contains all the interfaces, objects and streams to be created.
Committing the transaction sets up R2DTWO for actual packet processing.
The conversion from `ConfAction` list to `Action` array by `assemble_actions` happens in this phase (this function is basically the constructor for the `Pipeline` structure).

The shutdown sequence of R2DTWO is simple: commit a transaction that removes all the interfaces.
This starts an avalanche:

1. The Interfaces release their streams (the action pipeline is reference-counted)
2. The stateful actions in the streams release their PipelineObject reference, so they can go away
3. The Send actions in the streams release their Interface reference, so they can go away
4. The OAM actions in the streams release their Monitoring Point reference, so they can go away

After all those references have been released all dynamically allocated components are gone.

Note that this shutdown process doesn't lose packets that have already been received and are being processed, because the
PipelineIterator objects hold a reference to their action pipeline.

**Note**: the transaction commit mechanism is currently unfinished.
It can properly start up and shut down R2DTWO, but incremental configuration during runtime is not yet possible.
The main missing features are: remove individual streams, rollback on error, user interface (telnet commands and/or NETCONF).


# How to add new components

## Adding a new action

Start by adding an item in the `enum ConfActionType` in `conf_actions.c`, then check the compile errors.
That enum is always used in switch-cases, where there is deliberately no default branch.
The only necessary place not caught by this technique is the action type decoding in the `CA_UNDEF` branch at the beginning of `process_token`.
The parameters of the action can be collected in the `ConfAction` structure.

After that is done, the actual processing code for the action can be added similarly.
Add the new item in `enum ActionType` in `action.h`, and add the `create_action_XXX` function that instantiates the action in the Pipeline array.
The creation and execution functions of the actions use an established infrastructure, just follow the existing patterns.

The `action_private` structure holds the type-dependent data of the action.
It should be constant during the lifetime of the action, if dynamic state is needed, it should be in a PipelineObject.

The *del* callback is optional, `action_private` is automatically deleted by the common `delete_action`, the callback is only needed if `action_private` holds something that must be deleted manually.

Example commits for adding new actions:

* a8600a42 added Checksum and Verify
* 170d2099 added Setlength
* 6b4fd376 added TTLCheck and TTLReduce
* 55e7d9fc added FilterOAM


## Adding a new PipelineObject

If the new action needs permanent state, it needs a PipelineObject to store it.

The PipelineObject types are registered in `enum PipelineObjectType` in `object.h`.
In the same file `struct PipelineObject` is the base class of the objects.
The *process_packet* is the most important callback to implement, but the *get_state* and *print_info* are also required.

The configuration for the PipelineObject must also be created in `conf_object.c`.
There the most important part are the correct parsing of the object parameters, and providing sensible defaults to them.

All existing stateful actions can be instantiated in the config by the name of the corresponding PipelineObject, because the type of the object determines the action type.
Newly added actions and objects must also have this property.
In `conf_actions.c` the `CA_UNDEF` branch at the beginning of `process_token` is responsible for this.

Example commit for adding new object:

* 74b3fe68 added the Replicate object (slightly outdated commit, later 99c7ca73 changed the PipelineObject API)

## Adding a new Interface

The public API of the interfaces is in `interface.h`, here a new item must be added to`enum IfaceType`.

The only required public function of an interface is its constructor.
Other than that the *open*, *close_*, and *send* virtual methods are also mandatory.
The *close_* method is marked with an underscore, because it's private: only `iface_unref` calls it when the reference count has reached zero.

The constructor only initializes the struct to `INIT` state, the socket should be created in the *open* method.

The configuration for the Interface must also be created in `conf_interface.c`.
There the most important part are the correct parsing of the interface parameters, and providing sensible defaults to them.

Example commit for adding new interface: none, all interfaces were added a long time ago, and the code has been changed a lot since then.
The simplest interface is probably *eth*, it might be useful as reference.

## Adding a new OAM method

First the command to send the request must be added to `command_loop` in `oam_command.c`, along with its documentation in `help_str`.
The command should be processed by a `parse_XXX_command` function in `oam_request.c`, which always returns a `struct OamRequest`, on error it should set an error string in `req->error`.
Sending the request is done with `initiate_request`, which calls `send_request`, these should not need much modification.

The in-band requests are received by `process_inband_message` in `oam_message.c`, here the new message type must be handled, and a response can be generated.
The response should be a JSON containing everything from the request, including the parameters read from the fixed header, plus the information the response wants to return.
The response can be sent by calling `send_message_outofband`, it decides the way of sending the response based on the return address.

Processing the out-of-band reply happens in `process_reply` in `oam_message.c`, here a report can be written to the command connection that has initiated the request.
The command connection is found using the session identifiers echoed back in the reply.

Example commit for adding new OAM method: none, the latest added method is *notif_trigger*, but it spreads over several commits, and the whole OAM code has been reworked since then.
The simplest OAM method is *rlist*, it can be useful as reference.

