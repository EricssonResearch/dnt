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
