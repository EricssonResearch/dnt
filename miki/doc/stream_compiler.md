
# Compiler for the Stream Properties

This document is about the processing of the `:packet` and `:actions` lines of the *streams* section in the config. For the full documentation of the config see *inispec.md*.

## Actions in the C pipeline

These are the actions the C code can perform on the packet.

* ADD adds a new header at the given index with the given type and length; the buffer for the header is allocated on the scratch space
* DEL removes the header at the given position; the scratch space is not freed
* DELAY places the packet in a delay buffer (packet has a timestamp TS1, that's when it entered the network segment, recv generated timestamp TS2, it must stay in the delay buffer for max(0, DELAY-(TS2-TS1)))
* DROP the packet is dropped
* EDIT has a series of editing commands (header index, offset, bitlength, new value), executes them in order
* ELIM eliminates duplicate packets based on their sequence number (receives info on how to read the sequence number from the packet and a sequence recovery object)
* POF reorders the packets to fix sequence number ordering, uses its own packet buffer
* REPL makes copies of the packet and executes the given pipelines on the copies (the last pipeline gets the original instance)
* SEND sends the packet on the given interface
* TODO something for `readseq` and `readtstamp`

This is not 1:1 correspondence to the config commands.

## Compiling the config

### Finding the packet and action lines

Step one is to find a matching `packet` and `actions` line. Foreach on the keys, scan for `packet`, find matching `actions`. Then do the reverse. This way we can detect if either one is missing. First process `packet` into a list of headers, then supply that to the actions parser.

### Split the line into tokens

This step is common for both `packet` and `actions`.

First split the line by ';' to separate the stages.

Then split the stage by whitespace into tokens. There can be any amount of whitespace between the tokens.

## Parsing a packet header description

The first token is in the form of `headertype[_identifier]`. If `headertype` is unknown throw exception. If `identifier` is missing, the name of the header is `headertype`.

The subsequent tokens are field assignments (`fieldname=value`). If `headertype` has no such field, throw exception. The value must be a constant (see the constant types at the `edit` action). These tokens are optional, if they exist they will be used to identify the stream the packet belongs to.

The result of this parsing is a linked list of header descriptors that can be fed into the parse tree builder, and the action parser also uses it.

## Parsing an action

The first token is the name of the action. We have a fixed set of action names and a few object types that can be used as action; the name of the instances of such objects are available in a list (we've already parsed the [objects] section). If the token doesn't match any of the above, throw exception.

The result of the parse is a linked list of action descriptor objects.

The actions have action-specific arguments.

### add

Syntax: `add {before|after} headername newheadername fieldassignment [more fieldassignments]`

The syntax for `newheadername` is the same as `headername`.

Implementation note: we create the newly added headers in the original header list with a `new` flag to track the header indexes. After all the actions have been processed these extra headers are removed from the list.

The `fieldassignment` is similar to the `edit` action, but here the field *must* refer to the newly created header, so it must not contain the *headername* part. This also applies to the header matching in the *:packet* line, and in that line the value must be a constant.

Implementation note: this action is split into an "add header" and an "edit header" action.

### call

Syntax: `call pipelinename [substitutions]`

This finds a key in the *streams* section with the name `pipelinename`, interprets its value as an action list, and inlines that list into the current action list. No `CALL` action is allowed in the final Action list!

Additional arguments are substitutions in the form of `key=value`. After the referenced pipeline is loaded from the config, but before its processing, there is a string substitution phase. The "{key}" placeholders in the pipeline string are replaced by the corresponding value from the substitution list. An error is generated if no substitution is found for *key*.

### del

Syntax: `del headername`

The referenced header is marked as deleted, but not really removed from the linked list.

### delay

Syntax: `delay timestamp_field delay_value`

The `timestamp_field` is in the same form as the left hand side of an `edit` assignment.

The `delay_value` is a constant that has to be of type *time*.

### drop

Syntax: `drop`

This action must be the last action in the pipeline.

### edit

Syntax: `edit assignment [more assignments]`

The syntax for `assignment` is the following: `place.field=value`, where `place.field` can be:

* `headername.fieldname` referring to a header field in the packet
* `packet.propertyname` referring to a property slot in the packet descriptor; currently supported slots for writing:
    * `seq` 32 bit, designed to hold `rtag.seq`
    * `tstamp` 32b bit, designed to hold `ttag.tstamp`

The `value` can be:

* `headername.fieldname`, the bit length of this value must be the same as the left hand side
* `interfacename.propertyname`, the valid properties depend on the type of the interface
    * TODO document the valid properties
* `packet.propertyname`, the bit length of this value must be the same as the left hand side; currently supported slots for reading:
    * seq
    * tstamp
    * recvtime: this is a read-only property, generated by the interface upon packet reception
* value generator object; currently supported generator types:
    * sequence generator object, referred by its name
    * timestamp generator, referred as `timestamp`
* constant: constants are typed, the left-hand-side of the expression determines what format the constant needs to be (constants are stored in network byte order)
    * number in decimal or hexadecimal format
    * time with one of the following suffixes: *s*, *ms*, *us*, *ns*
    * MAC address
    * IPv4 address
    * IPv6 address

The above types are checked in the above order, so if you have an interface named *eth*, and you write `something=eth.mac` it won't work, because it will be understood as a header name, and the `eth` header has `smac` and `dmac` fields.

### eliminate

Syntax: `eliminate recovery_object sequence_field`

The `sequence_field` is in the form of `header.field` just like in value assignment.

The `recovery_object` refers to a sequence recovery object by name.

The `eliminate` command can be omitted, the object name is enough to deduce the action type.

### object

Syntax: `objectname [object-specific parameters]`

Some objects instantiated in the *objects* section can be used as actions, and they infer the appropriate action type. Such objects are:

* pof object as *pof* action
* sequence recovery object as *eliminate* action

### pof

Syntax: `pof pof_object sequence_field`

The `sequence_field` is in the form of `header.field` just like in value assignment.

The `pof_object` refers to a pof object by name.

The `pof` command can be omitted, the object name is enough to deduce the action type.

### replicate

Syntax: `replicate pipelinename [more pipelinename]`

The `pipelinename` is a key in the *streams* section with the name `pipelinename`. This processes the referenced pipelines and the result is stored in the action descriptor. Unlike `call` this does not do string substitutions.

This action must be the last action in the pipeline.

### send

Syntax: `send interface`

The `interface` refers to an interface defined in the *interfaces* section by name.


## Compiling the action descriptors into Actions

The result of this parsing is a linked list of action descriptors that will be compiled into an array of Action structures when creating the action pipeline..

## Additional comments

The followings are half-thought-out. Beware.

In the config we have action pipelines that look like this:

```
[objects]
seqgen1 = gen reset=1

[sreams]
s1:packet = eth; svlan; cvlan
s1:actions = edit svlan.vid=10; add after svlan rtag seq=seqgen1; send if3
s2:packet = eth; cvlan
s2:actions = add after eth svlan vid=20; add after svlan rtag seq=seqgen1; replicate s2path1 s2path2
s2path1 = edit eth.dmac=X:Y:Z cvlan.vid=20; send if3
s2path2 = send if4
```

Each element in the action list is processed and translated into IR instructions. The IR instructions are internal to the compiler, they represent the actions on a symbolic level, and a linked list is formed from them. This list of IR instructions will be compiled into an array of Action structures in the pipeline.

### How do the pipeline IR instructions store their values?

They refer to headers, fields, interfaces, objects etc. by name. We fill these when we create the instruction from what we read from the config.

They also have slots to store the numeric data that we deduce from the symbolic names.

We must match the header/object names as we read the action, not later. We process the `packet` line first so we have the list of headers (in a similar IR form).

### What optimizations do we want to do on the pipeline IR?

* Merge subsequent Edit instructions
* Reorder? Which instructions are safe to swap? What do we gain by this?
