#ifndef __PCAP_HELPER_H__
#define __PCAP_HELPER_H__

#include <stdlib.h>

struct in_addr;


int get_adapter_index_from_addr(struct in_addr* netaddr, char *guid, size_t guid_len);

#endif /* __PCAP_HELPER_H__ */