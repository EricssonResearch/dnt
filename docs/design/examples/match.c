#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

enum proto_types {
	ETH,
	VLAN,
	IPV4,
};


enum eth_fields {
	ETH_DMAC = 0x1,
	ETH_SMAC = 0x2,
	ETH_TYPE = 0x4,
};

struct eth_hdr {
	uint64_t dmac : 48;
	uint64_t smac : 48;
	uint16_t type : 16;
} __attribute__((packed, scalar_storage_order("big-endian")));

enum vlan_fields {
	VLAN_PCP = 1U,
	VLAN_VID = 1U << 1,
	VLAN_TPID = 1U << 2
};

struct vlan_hdr {
	uint16_t pcp : 3;
	uint16_t dei : 1; //deprecated
	uint16_t vid : 12;
	uint16_t tpid : 16;
} __attribute__((packed, scalar_storage_order("big-endian")));


enum ip_fields {
	IP_VERSION = 1U,
	IP_IHL = 1U << 1,
	IP_TOS = 1U << 2,
	IP_TTL = 1U << 3,
	IP_PROTO = 1U << 4,
	IP_SRC = 1U << 5,
	IP_DST = 1U << 6,
	IP_CSUM = 1U << 7,
};

struct ip_hdr {
	uint8_t version : 4;
	uint8_t ihl	: 4;
	uint8_t tos	: 8;
	uint16_t len	: 16;
	uint16_t id	: 16;
	uint16_t flags	: 3;
	uint16_t frag	: 13;
	uint8_t ttl	: 8;
	uint8_t proto	: 8;
	uint16_t csum	: 16;
	uint32_t src	: 32;
	uint32_t dst	: 32;
} __attribute__((packed, scalar_storage_order("big-endian")));


size_t header_size(int proto)
{
	size_t ret = 0;
	switch (proto) {	
	case ETH:
		ret = sizeof(struct eth_hdr);	
		break;
	case VLAN:
		ret = sizeof(struct vlan_hdr);	
		break;
	case IPV4:
		ret = sizeof(struct ip_hdr);	
		break;
	default:
		;//undefined proto error
		break;
	}

	return ret;
}

struct header {
	struct protocol *proto;
	char *data;
};

struct packet;

struct protocol {
	bool (*match)(struct packet *p);
	void (*edit)(struct packet *p);
	void (*push)(struct packet *p, size_t index);
	void (*pop)(struct packet *p, size_t index);
	enum proto_types (*get_next_type)(struct packet *p, size_t index);
	void (*set_next_type)(struct packet *p, enum proto_types proto);
};


struct fields_values {
	off_t offset;
	uint16_t select;
	char hdr_data[40];
	struct protocol *proto;
};

bool eth_match(struct packet *p)
{
	bool match = true;
};

struct packet {
	struct header hdrs[10];
	size_t num_of_hdrs;
	char *data;
	char *scratch;
	char buff[2000];
};


int main()
{
}

