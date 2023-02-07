# Packet handling in R2DTWO

This document indended to overview the packet structure and header handling.
The packet itself contains the received payload plus the new headers together with metadatas.

## Payload and header geometry

Desired to provide consistent packet handling accross all part of R2DTWO.
That means we only have one packet representation at every part of the processing.
As a result we dont do conversions and unnecessary memory movements/allocations accross the datapath.
Also, we can have unified API for handling the packet everywhere.

After the parse phase, we can have the packet structure with the payload and header metadata.
The following figure show a VLAN header deletion.
As one can notice, only the header array metadata structure changes (memmove) the buffer remains intact.

```
                                     struct header hdrs[]
                                    ┌──────┬──────┬──────┬──────┐
                                    │      │      │      │      │
                       ┌────────────┤      │      │      │      │
                       │            │ETH   │CVLAN │IP    │PAYLOA│
                       │            └──────┴─┬────┴──┬───┴──┬───┘
                       │                     │       │      │
                       │        ┌────────────┘       │      │
                       │        │                    │      │
                       ▼        │           ┌────────┘      └─┐
char[2000]       char*          │           │                 │                    char*
 buff │           data │        │           │                 │                    scratch│
      │                │        │           │                 │                           │
      ▼                ▼        ▼           ▼                 ▼                           ▼
      ┌───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
      │                                                                                                                       │
      │                                                                                                                       │
      │                                                                                                                       │
      └───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

       │                                                                                                                     │
       │                                                                                                                     │
       \───────────────────────────────────────────────────────\ /───────────────────────────────────────────────────────────/
                                                                ┼
                                                            sizeof(buff)





                                     struct header hdrs[]
                                    ┌──────┬──────┬──────┐
                                    │      │      │      │
                       ┌────────────┤      │      │      │
                       │            │ETH   │IP    │PAYLOA│
                       │            └──────┴─┬────┴──┬───┘
                       │                     │       │
                       │                     │       │
                       │                     │       │
                       ▼                    ┌┘       └────────┐
char[2000]       char*          x           │                 │                    char*
 buff │           data │        │           │                 │                    scratch│
      │                │        │           │                 │                           │
      ▼                ▼        ▼           ▼                 ▼                           ▼
      ┌───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
      │                                                                                                                       │
      │                                                                                                                       │
      │                                                                                                                       │
      └───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘

       │                                                                                                                     │
       │                                                                                                                     │
       \───────────────────────────────────────────────────────\ /───────────────────────────────────────────────────────────/
                                                                ┼
                                                            sizeof(buff)
```

## New header

New header added to the scratch area of the buffer.
Be aware that the scratch must not overlap with the data, that corrupts the packet.
The scratch area could be in the headroom of the buffer (before the data) thats implementation dependent.

## Assumptions

This header handling has the following limitations which must be kept in mind.

0. The `buff` is statically sized, part of the packet structure. Must be large enough to handle the maximal possible received datagram + maximal possible new headers
1. The scratch area monotonically increasing, but must not exceed `buff + 2000` (the allocated buffer's size). Also, if scratch implemented as `scratch = &buff` it most not exceed `data`
2. Removal of a header delete it, no way to get it back. Deletion do not modify the buffer, but the headers structure and next/prev hdrtypes reflect the change immediately
3. Every new header's data added to the scratch area. Deletion do not modify the buffer, but the headers structure and next/prev hdrtypes reflect the change immediately
4. Without edit action, the bytes in the buff area remains intact after the parsing, even at the last part of the processing (e.g. send)


