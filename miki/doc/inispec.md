
# Specification for the new config file format

TODO update this document for the current agreed-upon specification

Initially use a simple INI format. When we feel the need for array values or nested sections we can switch to [TOML](https://toml.io/en/). Note that TOML is not fully compatible with INI, the key differences are: identifiers are case-sensitive, string values must be placed in "double quotes".

The INI format is not formally specified, and variations in syntax exist in the parsers. We expect the most basic variant: single-line key=value elements, any number of whitespace around the = sign, section headers with the [name] syntax, and we assume no ordering of the keys within a section.

TODO find a suitable INI parser

## Sections

There are 4 sections: *interfaces*, *streams*, *objects*, *macros*. The first two are mandatory. Each section can only be present once, their order is arbitrary.

## interfaces

List of interfaces where we can send/receive packets. The keys of the items are the names of the interfaces, streams use these names to specify the incoming and outgoing interfaces. The values are in the form of `parameter=value` separated by space. The common parameters are:

* `iface` The name of the hardware interface associated with this interface, or `virtual`
* `type` The type of the interface, known types: `eth`, `ipv4`, `ipv6`, `udp-in`, `udp-out`

TODO parameters specific for the interface `type`: udp port and peer ip, eth priority etc.

A `virtual` interface is a special hardware interface that can be used in decapsulating scenarios: the last action sends the decapsulated packet to a virtual interface, where it will be parsed&processed again.
TODO should `virtual` be the type or the name of the interface?

## streams

This section lists the packet streams, each stream is defined with three lines: `iface`, `packet`, `actions`. Their order is not important. The syntax for the key of the stream lines is in the form of `streamname:actions`, where `streamname` is used to match the three definition lines.

### iface

This line lists the interfaces the stream can come from, separated by space. Optional, when not given, packets of the stream can arrive on any interface.

### packet

This line specifies the expected header structure of the packet, and the values of the header fields that are used to identify the stream. The header list is separated by semicolons (;).

Known header types are: eth, svlan, cvlan, rtag, ttag, ipv4, ipv6, arp, mpls, dcw_seq, dcw_ts TODO what else?

The actions that manipulate the packet refer to the headers by their type names. If the packet contains multiple headers of the same type, an alphanumeric suffix in the form of `headertype_identifier` can be used to distinguish them, the name of the header becomes the whole expression. The identifiers are arbitrary. This distinguishing identifier can be omitted, if the duplicate headers are not referenced any action.

Anything after the last specified header is considered `payload`. If the last header before the `payload` specifies the type of the next header, then the type of the payload will be handled correctly (e.g. when adding a new header just before it), but no action can manipulate the `payload` directly.

Value matching for header fields is done with the following syntax: `headername fieldname=fieldvalue`. It is possible to match multiple fields of the same header, separated by space.

If there is no matching stream for an incoming packet, the action is `drop`.

### actions

This line specifies the processing actions that must be run on the received packet that matches the corresponding *packet* line. The actions in the list can be elementary actions and/or macros in any combination, separated by semicolons (;).

The elementary actions are the following:

* `add {before|after} header newheader fieldname=fieldvalue` adds a new header of type `newheader` with the given field values at the given position
* `del header` removes the given header from the packet
* `edit header fieldname=newvalue` changes the given field of the given header, multiple fields can be edited at once, separated by space, can edit headers created by `add`
* `send iface` sends out the packet on the given interface from the *interfaces* list
* `drop` unconditionally drops the packet
* `replicate pipeline1 pipeline2` makes copies of the packet and continues processing them on the given pipelines, this can create any number of branches
* `eliminate seq_rec sequence_field` conditional drop, uses the given sequence recovery object and the field that contains the sequence number of the packet
* `pofobject sequence_field` puts the packet in a reorder buffer, continues the actions when the ordering is okay
* `delay timestamp_field delay` puts the packet in a delay buffer, continues the actions when the time is right
* `call pipeline` process the packet with the specified action pipeline and continue with this pipeline, useful for breaking up long pipelines or reuse operations for multiple streams; additional parameters are string substitutions for the specified pipeline
* `readseq fieldname`
* `readtstamp fieldname`

In these actions `header` refers to any header in the *packet* list by name, using the number suffix if necessary. The `newheader` in `add` can also have a number suffix.

It is possible to define action pipelines that are not tied to streams, rather, they are referenced by `replicate` and `call` actions. The names of these pipelines can be arbitrary.

When the parameter of an action is a header field, it is given in this form: `header.field`

When the parameter of an action is time, it needs to have one of these suffixes: 'us', 'ms', 's'

When the action pipeline is finished, the memory used for the packet is automatically reclaimed, there is no need to explicitely drop it.

TODO the source of values in `add`, `edit` can be properties of an interface, like IP or MAC address

TODO do we allow adding a header with a name that already exists (e.g. `add svlan` when we already have an svlan)? If the existing or the new one has a custom name then this problem doesn't arise. -> No, don't allow this.

## objects

This section instantiates the stateful objects that can be referenced by name in the action pipelines of the streams. The instantiation is in this format: `name = type parameter=value`. The valid parameter names and their valid values depend on the type of the object. The known object types are:

* `gen` sequnce number generator
* `rec` sequence number recovery
* `pof` packet ordering function

TODO document their parameters

TODO how to use these objects: `gen` and `rec` are parameters to certain actions, `pof` is an action TODO should `rec` be an action too?

TODO they have completely different interfaces

TODO timestamp generator?

## Fatal errors

The following errors result in the config being rejected:

* missing `interfaces` or `streams` section
* unknown section
* interfaces
    * reference a non-existing hardware interface
    * invalid interface type
    * invalid parameter for the interface type
* streams
    * iface, packet, actions line with a corresponding line missing (iface is optional, the others are not)
    * iface referring to non-existing interface
    * packet referring to invalid header type
    * packet match referring to invalid header field
    * actions containing unknown action (valid ones: elementary, object, macro)
    * actions having invalid parameter
* objects
    * invalid object type
    * invalid parameter for the object type
* macros
    * recursive macro definition
    * macro is not a valid action list

When the config is invalid *r2dthree* will not start.

