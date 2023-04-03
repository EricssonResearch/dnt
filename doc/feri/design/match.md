# Match on a packet

## Endianness problem

The package received by the r2dtwo interfaces are simple arrays of bytes.
Also their byte order is big-endian "network byte order" which means the most significant byte stored in the smallest memory address.
For example in an ethernet header, the first byte will be the most significant byte of the destination MAC address.
But storage of the bytes architecture dependent.
By simply casting an array of bytes to a C struct network header pointer, fields ends up different memory addresses by architecture.

## Solutions

Lets have a VLAN header defiend as a C struct bitfield:

```C
struct vlan_hdr {
    
	uint16_t tpid : 16;
	uint16_t pcp : 3;
	uint16_t dei : 1;
	uint16_t vid : 12;
};
```
Assuming `tpid=0x8100, vid=257, dei=1, pcp=3` field values, the memory layout is architecture dependent.
On a big-endian system this looks like the following in the memory (hex): `81 00 71 01`.
However on a little-endian system like x86 its end up like this: `00 81 01 10`.

### Solution A

Treating packet as array of bits instead array of bytes.
That way we can preassume big-endianness in the code.
For matching we can preconstruct bitfields with altogether with offsets and check when they matching.
Additionally we can define helpers to deal with multi-byte fields easily.
Also we can predefine bit offset for every filed.

### Solution B

Representing headers with metadata structures.
That way we cannot compary (or modify) the payload bytes directly.
Instead, we parse it first into the structure and compare it:
```
                       ┌──compare/match
                       │
                       ├──edit
       Header struct   │
        representor  ◄─┘
         ▲       │
         │       │
parse  ──┤       ├─serialize
function │       │ function
         │       ▼
        Header bytes
         received
```
The advantage of this solution is the ability to write it portable way, since we dont care about the endianness of the header representor structs.
Big disadvantage of this solution is we cannot directly manipulate the headers.

### Solution C

Similar to _Solution B_ but with handcrafted header representor struct.
That way we manually enforce big-endianness of the header struct.
Advantage is we can cast byte array into the header struct and the fields will matching.
Disadvantage is we have to mind about exotic endianness issues very hard to debug.

The following example for example force the endianness of the header struct with a GCC extension.
There are no equivalent in Clang at the moment.
As an alternative, it can be doable to define the structure differently with precompiler conditions on big and little endian archs.

```C
#include <sys/types.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

struct vlan_hdr {
	uint16_t tpid : 16;
	uint16_t pcp : 3;
	uint16_t dei : 1;
	uint16_t vid : 12;
} __attribute__((packed, scalar_storage_order("big-endian"))); //forced big-endian
//}; //uncomment to default endianness (memcmp result arch dependent)

int main()
{
    struct vlan_hdr v1 = {};
    char payload[] = {0x81, 0x00, 0x71, 0x01};
    struct vlan_hdr *v2 = (struct vlan_hdr *) payload;

    v1.tpid = 0x8100;
    v1.vid = 257;
    v1.pcp = 3;
    v1.dei = 1;

    printf("memcmp=%d\n", memcmp(&v1, v2, sizeof(struct vlan_hdr)));
    printf("sizeof(vlan_hdr)=%lu\n", sizeof(struct vlan_hdr));
    printf("%d %d %d %d\n", v1.tpid == v2->tpid, v1.vid == v2->vid, v1.pcp == v2->pcp, v1.dei == v2->dei);
}
```

Can be compiled with Clang too but the memset return value might be different.
Also, GCC 6 or later required for the attribute `scalar_storage_order`.

## Implementing the match

In the following we consider _Solution C_ for header representation.
Below the figure show the concept of the header matching.
The editing very similar.

```
                                Header structure filled with
                                 values used for match/edit

                             ┌────────────────┬───┬─┬────────────┐
             struct vlan_hdr │      TPID      │PCP│ │    VID     │
                             ├────────────────┼───┼─┼────────────┤
                       hdr:  │0000000000000000│000│0│000101000111│
                             └────────────────┴─┬─┴─┴────────────┘
                                                │
                                                ▼           ┌────────────────┬───┬─┬────────────┐
                                           ┌─┬─┬─┬─┐        │      TPID      │PCP│ │    VID     │
         Field access mask for match/edit: │0│0│0│1├──────► ├────────────────┼───┼─┼────────────┤
                                           └─┴─┴─┴─┘        │xxxxxxxxxxxxxxxx│xxx│x│000101000111│
                                                ▲           └────────────────┴───┴─┴────────────┘
                                                │
                             ┌────────────────┬─┴─┬─┬────────────┐                 │            │
(struct vlan_hdr *)pkt+off   │      TPID      │PCP│ │    VID     │                 └─────┬──────┘
                 ────────────┼────────────────┼───┼─┼────────────┼──────────             │
          pkt:   ...100101110│0000000001100100│110│0│000100000111│1100100...             │
                 ────────────┴────────────────┴───┴─┴────────────┴──────────             │
                             ▲                                                 match: hdr.vid == pkt.vid
                    off ─────┘                                                 edit:  pkt.vid = hdr.vid


                                           ┌─┬─┬─┬─┐
                                           │0│0│0│1│
                                           └┬┴┬┴┬┴┬┘
                                            │ │ │ │
                                      TPID ─┘ │ │ │
                                        PCP  ─┘ │ │
                                          DEI  ─┘ │
                                            VID  ─┘
```

There is a match object, which contains a pre-filled header.
This pre-filled header contains the fields we interested in, other fields can be undefined.
The match object also contains an "access mask" or "selector bits".
The purpose of this is simple: 0 - that field not relevant for the match.
1 - the field relevant and should match.
The access mask created by the config parser and each header type define its field bits.

## Example

Just to present the operation of the match, consider the following small example.
There we have a packet payload (`pkt`) assumed as VLAN header (told us by the config).
We seek to the index where the VLAN header starts: `&pkt[hdrs[1].offset]`.
The match object contains the pre-filled VLAN header (`struct vlan_hdr key`) as well as the access mask (`select`).
The matching itself done by the `vlan_match` function.

```C
struct vlan_hdr {
	uint16_t tpid : 16;
	uint16_t pcp : 3;
	uint16_t dei : 1;
	uint16_t vid : 12;
} __attribute__((packed, scalar_storage_order("big-endian")));

enum vlan_access_mask {
        TPID = 0x1,
        PCP = 0x2,
        DEI = 0x4,
        VID = 0x8,
}

struct match {
        vlan_hdr key;
        unsigned select;
};

bool vlan_match(const match *m, const char *pkt)
{
        bool matcing = true;
        const vlan_hdr *workitem = pkt;
        if(m->select & TPID)
                matching &= (m->key.tpid == workitem.tpid);
        if(m->select & PCP)
                matching &= (m->key.pcp == workitem.pcp);
        if(m->select & DEI)
                matching &= (m->key.dei== workitem.dei);
        if(m->select & VID)
                matching &= (m->key.vid == workitem.vid);
        return matcing;
}
```
