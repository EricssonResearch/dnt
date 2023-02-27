
#ifndef R2_CONF_PACKET_H
#define R2_CONF_PACKET_H

struct HeaderDescriptor;

// parse the "*:packet" line for a stream
// @stream name is used in error messages
// modifies the given @line
struct HeaderDescriptor *process_packet_line(const char *stream, char *line);

#endif // R2_CONF_PACKET_H
