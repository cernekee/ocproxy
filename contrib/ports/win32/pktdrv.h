#ifndef __PKTDRV_H__
#define __PKTDRV_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*input_fn)(void *arg, void *packet, int len);

enum link_adapter_event {
  LINKEVENT_UNCHANGED,
  LINKEVENT_UP,
  LINKEVENT_DOWN
};

void*                   init_adapter    (int adapter_num, char *mac_addr, input_fn input, void *arg, enum link_adapter_event *linkstate);
void                    shutdown_adapter(void *adapter);
int                     packet_send     (void *adapter, void *buffer, int len);
void                    update_adapter  (void *adapter);
enum link_adapter_event link_adapter    (void *adapter);
int                     get_adapter_index(const char* adapter_guid);

#ifdef __cplusplus
}
#endif

#endif
