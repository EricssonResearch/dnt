
# How to transfer values between objects?

This problem mostly arises when reading or writing header fields in the packet.

We use a Producer--Consumer model. Both of them are functions.

* Producer creates the value on its stack, calls the Consumer function with it
* Consumer receives the value and processes it (e.g. writes it into the packet)

Rationale behind this architecture:

* we want both functions to be re-entrant
* we don't want to malloc a temporary storage to pass the value
* we don't want to have a fixed-length buffer to pass the value

How to create a matching pair? The config compiler should know everything we need.

* Producers can tell what type of data they will produce (offset, length)
* Consumers can tell what they need as input (also offset, length)

TODO we may also need to consider the type of the value (address, time etc.)

TODO we may need adaptors when the Consumer wants some offset or something the Producer can't create TODO do we know of such a scenario?

## Consumer

This function has three parameters:

* value to read from: comes from the Producer
    * start pointer, offset, length (this is a stream of bits in network byte order)
* state depending on the type of the consumer
    * field writer: header field to write to (header index, bit offset, bit length)
    * other object types have their own state
* packet: the current packet of the action pipeline
    * field writer: this is where the target header field is
    * maybe not all consumer objects need the packet

The state is radically different for each Consumer, the parameter has to be void\*. The config compiler will know how to construct the appropriate state.

### Field writer

We need a series of such functions for various bit lengths and offsets. We must consider both the target field and the source value.

* both length and offset are multiple of 8 (simple memcpy)
* length is <8 and offset is such that it doesn't span multiple bytes
* length is < 16 and offset is such that it only spans 2 bytes
* length is < 8 and offset is such that it spans at least 3 bytes

The input value should be prepared such that this writer doesn't need to do bit-shift on it.
TODO this applies to all Consumers
TODO if we do need shift, do it with an adaptor?

### Object

The object receives its input by having a method act as a Consumer function.

TODO document the objects that act as a Consumer: Sequence Recovery, Delay, POF etc.

### Packet metadata

Currently both our metadata are uint32 (sequence number, timestamp), this Consumer type will write the given value into the metadata slots. The Producer for these is typically a field reader, but we have appropriate generators too.

## Producer

This function has four parameters:

* producer state: depends on the type of the producer
* consumer function: this will be called on the generated value
* consumer state: opaque state of the consumer
* packet: the current packet of the action pipeline

Producer types:

* Constant value: we don't have a producer function, just a value
    * constants should be typed, the config compiler should check the type against its usage
    * when we are at the assignment we no longer have types just start, offset, length
* Generated value: Producer creates it on its stack, calls the Consumer
    * sequence number
    * timestamp
    * interface property
    * packet property: arrival time, sequence number
    * Header field: format in config: headername.fieldname

The Producer can be stateful, but it must be re-entrant: it receives its state as a parameter. As this parameter is radically different for each Producer, the parameter is void\*. The config compiler will know how to construct the appropriate state.

The producer may need input to generate the value. This input comes from the state parameter.

### Constant value

Do we need a Producer for constant values? Isn't it enough if we just have the value and call the Consumer on it? Currently the Edit action has this simplification.


### Field reader

Similar to field writer.

### Sequence Generator

Receives a Consumer that will know what to do with the generated sequence number. The two most common Consumers: field writer, metadata writer.

### Packet metadata

This producer reads the packet metadata and supplies it to a Consumer, which is typically a field writer.

## Timestamps

The system time is `struct timespec` with seconds and nanoseconds. The TSN timestamp is a 21 bit value where 20 bits encode microseconds (2^20 is a little over 1 million) and the MSB is one second. We need to convert between these types. TODO where?

## Questions

Isn't this overkill? See Maxim #37

How can this be used by the packet matching? It needs to use the packet header field extractor Producer somehow.

