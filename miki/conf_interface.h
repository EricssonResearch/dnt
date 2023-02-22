
#ifndef R2_CONF_INTERFACE_H
#define R2_CONF_INTERFACE_H

struct IniSection;
struct Interface;

// returns an array of interfaces and the length of the array
// the interfaces are just created, but not opened
struct Interface *process_interfaces(struct IniSection *interfaces_section, unsigned *iface_count);


#endif // R2_CONF_INTERFACE_H
