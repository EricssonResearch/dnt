#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
	uint8_t dmac[6];
	uint8_t smac[6];
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
	off_t off;
	enum proto_types proto_type;
	struct protocol *proto;
};

struct packet;
struct value;

struct protocol {
	bool (*match)(const char *p, const struct value *v);
	void (*edit)(struct packet *p);
	void (*push)(struct packet *p, size_t index);
	void (*pop)(struct packet *p, size_t index);
	enum proto_types (*get_next_type)(struct packet *p, size_t index);
	void (*set_next_type)(struct packet *p, enum proto_types proto);
	struct value (*value_from_str)(const char *conf);
	void (*print)(const char *h);

};

// store header data for field edit/match
struct value {
	uint16_t select;
	char hdr_data[40];
};

struct value *eth_val_from_str(const char *conf)
{
	struct value *v = calloc(1, sizeof(struct value));
	struct eth_hdr *h = (struct eth_hdr *)&v->hdr_data;
	struct fields {
		const char *field;
		enum eth_fields key;
	} fk[] = {
		{ "smac", ETH_SMAC },
		{ "dmac", ETH_DMAC },
		{ "type", ETH_TYPE }
	};
	for(int i = 0; i < sizeof(fk)/sizeof(struct fields); ++i) {
		const char *conf_field = strstr(conf, fk[i].field);
		if(!conf_field)
			continue;
		v->select |= fk[i].key;
		switch(fk[i].key) {
			case ETH_DMAC: {
				uint8_t *mac = h->dmac;
				sscanf(conf_field, "dmac=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac, mac+1, mac+2, mac+3, mac+4, mac+5);
				break;
			}
			case ETH_SMAC: {
				uint8_t *mac = h->smac;
				sscanf(conf_field, "smac=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", mac, mac+1, mac+2, mac+3, mac+4, mac+5);
				break;
			}
			case ETH_TYPE: break;
			default: printf("ERROR, no such ETH field\n"); //exit program
		}
	}
	return v;
}

bool eth_match(const char *p, const struct value *v)
{
	bool match = true;
	const struct eth_hdr *eh = (struct eth_hdr *) p;
	const struct eth_hdr *ehv = (struct eth_hdr *) v->hdr_data;
	if(v->select & ETH_DMAC)
		match &= (eh->dmac == ehv->dmac);
	if(v->select & ETH_SMAC)
		match &= (eh->smac == ehv->smac);
	return match;
};

void eth_print(const char *h)
{
	const struct eth_hdr *eh = (struct eth_hdr *)h;
	printf("smac=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx ", eh->smac[0], eh->smac[1], eh->smac[2], eh->smac[3],eh->smac[4], eh->smac[5]);
	printf("dmac=%hhx:%hhx:%hhx:%hhx:%hhx:%hhx ", eh->dmac[0], eh->dmac[1], eh->dmac[2], eh->dmac[3],eh->dmac[4], eh->dmac[5]);
	printf("type: %x\n", eh->type);
}


struct packet {
	struct header hdrs[10];
	size_t num_hdrs;
	off_t off_data;
	off_t off_scratch;
	//char buff[2000];
	char *buff;
};

static struct protocol eth_proto = {
	.match = eth_match,
	.print = eth_print,

};

void packet_print(const struct packet *p)
{
	struct protocol *proto;
	for (int i = 0; i < p->num_hdrs; ++i) {
		proto = p->hdrs->proto;
		const struct header *h = &p->hdrs[i];
		proto->print(&p->buff[h->off]);
	}
}


int main()
{
	char data[2000] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x08, 0x00, /*MAC hdr*/
			0x81, 0x00, 0x71, 0x01, /*CVLAN*/
			0x45, 0x00, 0x00, 0x25, 0x77, 0x76, 0x40, 0x00, 0x40, 0x11, 0xc5, 0x4f, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01, /*IP hdr*/
			0xa2, 0xcb, 0x15, 0xb3, 0x00, 0x11, 0xfe, 0x24, /* UDP hdr */
			0x64, 0x65, 0x61, 0x64, 0x62, 0x65, 0x65, 0x66, 0x0a}; /* payload */
	struct packet pkt;
	pkt.buff = data;
	pkt.num_hdrs = 1;
	pkt.hdrs[0].off = 0;
	pkt.hdrs[0].proto_type = ETH;
	pkt.hdrs[0].proto = &eth_proto;

	packet_print(&pkt);

	struct value *my_eth = eth_val_from_str("dmac=00:00:00:00:00:ee smac=00:00:00:00:00:cc");
	eth_print(my_eth->hdr_data);
}

