# Value transfer to/from packet header fields

We've developed a universal value passing architecture to have a universal `edit` action that can manipulate any protocol header in a generic way.
In this architecture the actual packet header field editing is happens by copying bit sequences, where the appropriate offsets and bit counts are set up from protocol header descriptors during configuration time.

This value transfer architecture was designed to be more generic than a simple header read/write.
In the original design of R2DTWO one could transfer values to/from packet header fields, packet property (metadata) slots, stateful objects, interface properties etc.
Currently R2DTWO doesn't fully utilize the capabilities that could be possible, just reads and writes packet headers.


## Producer-Consumer model

The universal value passing is based on a Producer-Consumer model, both of them are functions.

* *Producer* function creates the value on its stack, and calls the Consumer function with it
* *Consumer* function receives the value and processes it (e.g. writes it into the packet at a given offset)

The rationale behind this calling convention:

* we want both functions to be re-entrant
* we don't want to malloc a temporary storage to pass the value
* we don't want to have a fixed-length buffer to pass the value

The value to be transferred is a sequence of bits starting at a specified bit offset from the beginning of a buffer.

To be compatible with IEEE and IETF protocols, the buffer contents must be in big endian byte order, MSB-first.

Both the Producer and the Consumer have a state parameter that configures the function for working on the correct number of bits with the correct offset.
This state is constructed by the config compiler once (when processing the `edit` action), and used during the lifetime of the action pipeline.

In the current implementation the Producer and Consumer function pair and their parameter settings must be compatible by referring to

* same number of bits
* same bit offset within a byte

The original plan also included adaptor functions that can offset the data created by a Producer to make it compatible with a Consumer, but we never needed this in practice, so this was never implemented.

How to get a compatible pair of Producer and Consumer functions?
For the `edit` action the config compiler checks the types and sizes of the operands for their compatibility.

Where to find the code related to this value transfer architecture:

* `value.h` contains the `Value` structure, and the prototypes for the Producer and Consumer functions
* `header.h` has functions that return Producer and Consumer functions for header fields based on bitoffset/bitcount
* `interface.h` has the base class for interfaces, the `get_property_reader()` virtual method returns a Producer
* `protocol.h` provides information on the header fields of each protocol known to R2DTWO
* `conf_actions.c` is responsible for compiling the actions in the config into action pipelines

The `edit` action parameters are assignments in the form of `lhs=rhs`.
They are processed into Producer-Consumer pairings by `process_assignment_lhs()` and `process_assignment_rhs()` in `conf_actions.c`.
The right-hand-side of the assignment becomes a Producer, the left-hand-side becomes a Consumer.


## Value structure

The Producer function hands over data to the Consumer with a Value structure that contains

* bitoffset
* bitcount
* value is a buffer that holds the data

In the current implementation the Producers make sure that bitoffset is less than 8.

There are two structures that are related to this:

* ProtocolField describes one field in a protocol header, with name and type in addition to the bitoffset/bitcount
* HeaderField describes a field in a header of the packet we are working on, with a header index in addition to the bitoffset/bitcount

The config compiler uses the information in ProtocolField to construct Value and HeaderField structures that are used when processing packets.
Note that the type of the fields are only considered during compilation, when processing packets the header fields are untyped bit strings.


## Consumer function

This function has three parameters:

* `state` depending on the type of the Consumer
    * e.g. for the header field writer: header field to write to (header index, bitoffset, bitcount)
    * other types have their own state
* `value` to read from: comes from the Producer
    * start pointer, bitoffset, bitcount
* `packet` the current packet of the action pipeline
    * for the header field writer: this is where the target header field is
    * not all consumer types need the packet

The state is different for each Consumer type. The config compiler has to know how to construct the appropriate state.

In the current implementation there is only one Consumer function, the header field writer.

### Header field writer

In `header.h` the function `header_get_field_writer()` returns a Consumer function that can write the given *target* header field from the given *source* value, or NULL when they are incompatible.
It returns one of the following (private) bit copy implementations:

* copy whole bytes
* copy bits in a single byte
* generic copy

The state for the returned function should be the same *target*.
The `HeaderField` structure for *target* can be created from a *ProtocolField* and a header index with `new_headerfield()`.
To get a *ProtocolField* see e.g. `protocol_get_field_by_name()` in `protocol.h`.


## Producer function

This function has four parameters:

* `producer state` depends on the type of the Producer
* `consumer` this function will be called on the generated value
* `consumer state` opaque state of the Consumer
* `packet` the current packet of the action pipeline

The producer state is different for each Producer type.
The config compiler has to know how to construct the appropriate state.

The following Producer functions are currently available in R2DTWO.

### Constant value

This doesn't have a producer function, the constant value is stored directly in the buffer of a Value object, and it is fed directly to the Consumer.

When processing constants the config compiler checks that the given value fits into the target, and that it has the correct type for the target (e.g. a header field with type IPv6 expects a constant in the usual IPv6 address format).
The type checking happens during config compilation time, the packet processing is just moving bits around.

### Header field reader

In `header.h` the function `header_get_field_reader()` returns a Producer function that can read the given *source* header field into the given *target* value, or NULL when they are incompatible.
The state for the returned function should be the same *source*.
For more info on `HeaderField` see the field writer section.

### Interface property

Interfaces can have read-only properties (e.g. MAC address, IP address) that assignments in `edit` actions can use as data source.
The optional `get_property_reader()` virtual method returns the appropriate value Producer for the properties based on their names, and checks compatibility with the given target where the produced value is intended to be stored.
Not all interface types support querying properties.


## Value comparison

In addition to the `edit` action the generic value infrastructure is also used by the `Parsetree` object to match header fields against constant values to determine which stream the packet belongs to.
The header field comparison is done with a Comparator function that has three parameters:

* `state` depending on the type of the comparator
    * in the current `Parsetree` implementation this is always a `HeaderField`
* `value` is always a constant value, currently no Producers are supported
* `packet` the current packet that needs to be identified by the `Parsetree`

In `header.h` the function `header_get_field_comprator()` returns a Comparator function that compare the given *target* header field with the given *match* value, or NULL when they are incompatible.

There were discussions about supporting generic value-value comparisons and Producer functions, but the current implementation is good enough for the `Parsetree`.
It might be useful to support comparison with interface properties, like `ipv6.dst=recvif.srcip`, but currently there are no plans to implement it.

