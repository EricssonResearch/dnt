#include <sys/types.h>
#include <sys/socket.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum type {
	ETH, CVLAN, SVLAN, RTAG, TTAG, IP, UDP, PAYLOAD
};

size_t hdrsize[] = {14, 4, 4, 6, 6, 20, 16};

struct header {
	int type;
	char *data;
};

struct packet {
	struct header hdrs[10];
	size_t hdrs_len;
	char buff[2000]; //recvd on the interface
	char *scratch; //for new headers
	char *data; //starting of the valid data (might by unnecessary)
} packet;

struct packet *new_packet()
{
	struct packet *p = calloc(1, sizeof(struct packet));
	p->scratch = p->buff + 1500;
	p->data = p->buff;
	return p;
}

void add_header(int type, struct packet *pkt, int idx)
{
	if(idx < 0 || idx > 9)
		return;

	struct header new_hdr = {type, pkt->scratch};
	if(idx == pkt->hdrs_len) //after last header
		pkt->hdrs[idx] = new_hdr;
	else {
		memmove(&pkt->hdrs[idx+1], &pkt->hdrs[idx], (pkt->hdrs_len - idx) * sizeof(struct header));
		pkt->hdrs[idx] = new_hdr;
	}
	pkt->hdrs_len += 1;
	pkt->scratch += hdrsize[type];
	//handle next/prev header type if any
}

void del_header(int type, struct packet *pkt, int idx)
{
	if(idx < 0 || idx > pkt->hdrs_len - 1)
		return;

	if(idx == pkt->hdrs_len - 1) //removal of the last header
		; //just forget about the last header
	else {
		memmove(&pkt->hdrs[idx-1], &pkt->hdrs[idx], (pkt->hdrs_len - idx) * sizeof(struct header));
	}
	pkt->hdrs_len -= 1;
	//handle next/prev header types if any
}



int main()
{
	char data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x08, 0x00, /*MAC hdr*/
			0x81, 0x00, 0x71, 0x01, /*CVLAN*/
			0x45, 0x00, 0x00, 0x25, 0x77, 0x76, 0x40, 0x00, 0x40, 0x11, 0xc5, 0x4f, 0x7f, 0x00, 0x00, 0x01, 0x7f, 0x00, 0x00, 0x01, /*IP hdr*/
			0xa2, 0xcb, 0x15, 0xb3, 0x00, 0x11, 0xfe, 0x24, /* UDP hdr */
			0x64, 0x65, 0x61, 0x64, 0x62, 0x65, 0x65, 0x66, 0x0a}; /* payload */

	struct packet *pkt = new_packet();
	pkt->data = data; //normally thats done by the interface recv

	/* "Parse the headers" lets say thats done by the parser */
	pkt->hdrs_len = 4;
	pkt->hdrs[0].type = ETH;
	pkt->hdrs[0].data = pkt->data;
	pkt->hdrs[1].type = CVLAN;
	pkt->hdrs[1].data = pkt->hdrs[0].data + hdrsize[ETH];
	pkt->hdrs[2].type = IP;
	pkt->hdrs[2].data = pkt->hdrs[1].data + hdrsize[CVLAN];
	pkt->hdrs[3].type = PAYLOAD;
	pkt->hdrs[3].data = pkt->hdrs[2].data + hdrsize[IP];
}

