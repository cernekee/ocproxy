/*
 * Copyright (c) 2001,2002 Florian Schulze.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the authors nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * pktdrv.c - This file is part of lwIP pktif
 *
 ****************************************************************************
 *
 * This file is derived from an example in lwIP with the following license:
 *
 * Copyright (c) 2001, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "pktdrv.h"
#include "lwipcfg_msvc.h"

#pragma warning (disable: 4201) /* don't warn about union without name */

#define WIN32_LEAN_AND_MEAN
/* get the windows definitions of the following 4 functions out of the way */
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <packet32.h>
#include <ntddndis.h>

/** @todo use the lwip header file */
#define ETHARP_HWADDR_LEN      6

#define MAX_NUM_ADAPTERS       10
#define ADAPTER_NAME_LEN       4096
#define PACKET_ADAPTER_BUFSIZE 512000
#define PACKET_INPUT_BUFSIZE   256000

#define PACKET_OID_DATA_SIZE   255

/* Packet Adapter informations */
struct packet_adapter {
  input_fn         input;
  void            *input_fn_arg;
  LPADAPTER        lpAdapter;
  LPPACKET         lpPacket;
  UINT             bs_drop;
  NDIS_MEDIA_STATE fNdisMediaState;
  /* buffer to hold the data coming from the driver */
  char             buffer[PACKET_INPUT_BUFSIZE];
};

static int get_link_state(struct packet_adapter *pa, NDIS_MEDIA_STATE *linkstate);


/** Get a list of adapters
 *
 * @param adapter_list void* array: list where the adapters are stored
 * @param list_len size of adapter_list (number of void*)
 * @param buffer here the actual data is stored, adapter_list points into this buffer
 * @param buf_len size of buffer in bytes
 * @return number of adapters found or negative on error
 */
static int
get_adapter_list(char** adapter_list, int list_len, void* buffer, size_t buf_len)
{
  int i;
  char *temp, *start;
  ULONG AdapterLength;

  memset(adapter_list, 0, list_len*sizeof(void*));
  memset(buffer, 0, buf_len);

  /* obtain the name of the adapters installed on this machine
     (a list of strings separated by '\0') */
  AdapterLength = buf_len;
  if (PacketGetAdapterNames((char*)buffer, &AdapterLength)==FALSE){
    printf("Unable to retrieve the list of the adapters!\n");
    return 0;
  }

  /* get the start of each adapter name in the list and put it into
   * the AdapterList array */
  i = 0;
  temp = (char*)buffer;
  start = (char*)buffer;
  while ((*temp != '\0') || (*(temp - 1) != '\0')) {
    if (*temp == '\0') {
      adapter_list[i] = start;
      start = temp + 1;
      i++;
      if (i >= list_len) {
        break;
      }
    }
    temp++;
  }
  return i;
}

/** Get the index of an adapter by its GUID
 *
 * @param adapter_guid GUID of the adapter
 * @return index of the adapter or negative on error
 */
int
get_adapter_index(const char* adapter_guid)
{
  char *AdapterList[MAX_NUM_ADAPTERS];
  int i;
  char AdapterName[ADAPTER_NAME_LEN]; /* string that contains a list of the network adapters */
  int AdapterNum;

  if ((adapter_guid != NULL) && (adapter_guid[0] != 0)) {
    AdapterNum = get_adapter_list(AdapterList, MAX_NUM_ADAPTERS, AdapterName, ADAPTER_NAME_LEN);
    if (AdapterNum > 0) {
      for (i = 0; i < AdapterNum; i++) {
        if(strstr((char*)AdapterList[i], adapter_guid)) {
          return i;
        }
      }
    }
  }
  return -1;
}

/**
 * Open a network adapter and set it up for packet input
 *
 * @param adapter_num the index of the adapter to use
 * @param mac_addr the MAC address of the adapter is stored here (if != NULL)
 * @param input a function to call to receive a packet
 * @param arg argument to pass to input
 * @param linkstate the initial link state
 * @return an adapter handle on success, NULL on failure
 */
void*
init_adapter(int adapter_num, char *mac_addr, input_fn input, void *arg, enum link_adapter_event *linkstate)
{
  char *AdapterList[MAX_NUM_ADAPTERS];
#ifndef PACKET_LIB_QUIET
  int i;
#endif /* PACKET_LIB_QUIET */
  char AdapterName[ADAPTER_NAME_LEN]; /* string that contains a list of the network adapters */
  int AdapterNum;
  PPACKET_OID_DATA ppacket_oid_data;
  unsigned char ethaddr[ETHARP_HWADDR_LEN];
  struct packet_adapter *pa;
  NDIS_MEDIA_STATE mediastate;
  
  pa = (struct packet_adapter *)malloc(sizeof(struct packet_adapter));
  if (!pa) {
    printf("Unable to alloc the adapter!\n");
    return NULL;
  }

  memset(pa, 0, sizeof(struct packet_adapter));
  pa->input = input;
  pa->input_fn_arg = arg;

  AdapterNum = get_adapter_list(AdapterList, MAX_NUM_ADAPTERS, AdapterName, ADAPTER_NAME_LEN);

  /* print all adapter names */
  if (AdapterNum <= 0) {
    free(pa);
    return NULL; /* no adapters found */
  }
#ifndef PACKET_LIB_QUIET
  for (i = 0; i < AdapterNum; i++) {
    LPADAPTER lpAdapter;
    printf("%2i: %s\n", i, AdapterList[i]);
    /* set up the selected adapter */
    lpAdapter = PacketOpenAdapter(AdapterList[i]);
    if (lpAdapter && (lpAdapter->hFile != INVALID_HANDLE_VALUE)) {
      ppacket_oid_data = (PPACKET_OID_DATA)malloc(sizeof(PACKET_OID_DATA) + PACKET_OID_DATA_SIZE);
      if (ppacket_oid_data) {
        ppacket_oid_data->Oid = OID_GEN_VENDOR_DESCRIPTION;
        ppacket_oid_data->Length = PACKET_OID_DATA_SIZE;
        if (PacketRequest(lpAdapter, FALSE, ppacket_oid_data)) {
          printf("     Name: \"%s\"\n", ppacket_oid_data->Data);
        }
        free(ppacket_oid_data);
      }
      PacketCloseAdapter(lpAdapter);
      lpAdapter = NULL;
    }
  }
#endif /* PACKET_LIB_QUIET */
  /* invalid adapter index -> check this after printing the adapters */
  if (adapter_num < 0) {
    printf("Invalid adapter_num: %d\n", adapter_num);
    free(pa);
    return NULL;
  }
  /* adapter index out of range */
  if (adapter_num >= AdapterNum) {
    printf("Invalid adapter_num: %d\n", adapter_num);
    free(pa);
    return NULL;
  }
#ifndef PACKET_LIB_QUIET
  printf("Using adapter_num: %d\n", adapter_num);
#endif /* PACKET_LIB_QUIET */
  /* set up the selected adapter */
  pa->lpAdapter = PacketOpenAdapter(AdapterList[adapter_num]);
  if (!pa->lpAdapter || (pa->lpAdapter->hFile == INVALID_HANDLE_VALUE)) {
    free(pa);
    return NULL;
  }
  /* alloc the OID packet  */
  ppacket_oid_data = (PPACKET_OID_DATA)malloc(sizeof(PACKET_OID_DATA) + PACKET_OID_DATA_SIZE);
  if (!ppacket_oid_data) {
    PacketCloseAdapter(pa->lpAdapter);
    free(pa);
    return NULL;
  }
  /* get the description of the selected adapter */
  ppacket_oid_data->Oid = OID_GEN_VENDOR_DESCRIPTION;
  ppacket_oid_data->Length = PACKET_OID_DATA_SIZE;
  if (PacketRequest(pa->lpAdapter, FALSE, ppacket_oid_data)) {
    printf("Using adapter: \"%s\"", ppacket_oid_data->Data);
  }
  /* get the MAC address of the selected adapter */
  ppacket_oid_data->Oid = OID_802_3_PERMANENT_ADDRESS;
  ppacket_oid_data->Length = ETHARP_HWADDR_LEN;
  if (!PacketRequest(pa->lpAdapter, FALSE, ppacket_oid_data)) {
    printf("ERROR getting the adapter's HWADDR, maybe it's not an ethernet adapter?\n");
    PacketCloseAdapter(pa->lpAdapter);
    free(pa);
    return NULL;
  }
  /* copy the MAC address */
  memcpy(&ethaddr, ppacket_oid_data->Data, ETHARP_HWADDR_LEN);
  free(ppacket_oid_data);
  if (mac_addr != NULL) {
    /* copy the MAC address to the user supplied buffer, also */
    memcpy(mac_addr, &ethaddr, ETHARP_HWADDR_LEN);
  }
  printf(" [MAC: %02X:%02X:%02X:%02X:%02X:%02X]\n", ethaddr[0], ethaddr[1], ethaddr[2],
          ethaddr[3], ethaddr[4], ethaddr[5]);
  /* some more adapter settings */
  PacketSetBuff(pa->lpAdapter, PACKET_ADAPTER_BUFSIZE);
  PacketSetReadTimeout(pa->lpAdapter, 1);
  PacketSetHwFilter(pa->lpAdapter, NDIS_PACKET_TYPE_ALL_LOCAL | NDIS_PACKET_TYPE_PROMISCUOUS);
  /* set up packet descriptor (the application input buffer) */
  if ((pa->lpPacket = PacketAllocatePacket()) == NULL) {
    printf("ERROR setting up a packet descriptor\n");
    PacketCloseAdapter(pa->lpAdapter);
    free(pa);
    return NULL;
  }
  PacketInitPacket(pa->lpPacket,(char*)pa->buffer, sizeof(pa->buffer));

  if(get_link_state(pa, &mediastate)) {
    *linkstate = (mediastate == NdisMediaStateConnected ? LINKEVENT_UP : LINKEVENT_DOWN);
  }

  return pa;
}

/**
 * Close the adapter (no more packets can be sent or received)
 *
 * @param adapter adapter handle received by a call to init_adapter, invalid on return
 */
void
shutdown_adapter(void *adapter)
{
  struct packet_adapter *pa = (struct packet_adapter*)adapter;
  if (pa != NULL) {
    if (pa->lpPacket) {
      PacketFreePacket(pa->lpPacket);
    }
    if (pa->lpAdapter) {
      PacketCloseAdapter(pa->lpAdapter);
    }
    free(pa);
  }
}

/**
 * Send a packet
 *
 * @param adapter adapter handle received by a call to init_adapter
 * @param buffer complete packet to send (including ETH header; without CRC)
 * @param len length of the packet (including ETH header; without CRC)
 */
int
packet_send(void *adapter, void *buffer, int len)
{
  struct packet_adapter *pa = (struct packet_adapter*)adapter;
  LPPACKET lpPacket;

  if (pa == NULL) {
    return -1;
  }
  if ((lpPacket = PacketAllocatePacket()) == NULL) {
    return -1;
  }
  PacketInitPacket(lpPacket, buffer, len);
  if (!PacketSendPacket(pa->lpAdapter, lpPacket, TRUE)) {
    return -1;
  }
  PacketFreePacket(lpPacket);

  return 0;
}

/**
 * Process a packet buffer (which can hold multiple packets) and feed
 * every packet to process_input().
 *
 * @param adapter adapter handle received by a call to init_adapter
 * @param lpPacket the packet buffer to process
 */
static void
ProcessPackets(void *adapter, LPPACKET lpPacket)
{
  struct packet_adapter *pa = (struct packet_adapter*)adapter;
  ULONG  ulLines, ulBytesReceived;
  char  *base;
  char  *buf;
  u_int off = 0;
  u_int tlen, tlen1;
  struct bpf_hdr *hdr;
  void *cur_packet;
  int cur_length;

  if (pa == NULL) {
    return;
  }

  ulBytesReceived = lpPacket->ulBytesReceived;

  buf = (char*)lpPacket->Buffer;

  off=0;

  while (off < ulBytesReceived) {
    hdr = (struct bpf_hdr *)(buf + off);
    tlen1 = hdr->bh_datalen;
    cur_length = tlen1;
    tlen = hdr->bh_caplen;
    off += hdr->bh_hdrlen;

    ulLines = (tlen + 15) / 16;
    if (ulLines > 5) {
      ulLines = 5;
    }

    base =(char*)(buf + off);
    cur_packet = base;
    off = Packet_WORDALIGN(off + tlen);

    pa->input(pa->input_fn_arg, cur_packet, cur_length);
  }
}

/**
 * Check for newly received packets. Called in the main loop: 'interrupt' mode is not
 * really supported :(
 *
 * @param adapter adapter handle received by a call to init_adapter
 */
void
update_adapter(void *adapter)
{
  struct packet_adapter *pa = (struct packet_adapter*)adapter;
  struct bpf_stat stat;

  /* print the capture statistics */
  if(PacketGetStats(pa->lpAdapter, &stat) == FALSE) {
    printf("Warning: unable to get stats from the kernel!\n");
  } else {
    if (pa->bs_drop != stat.bs_drop) {
      printf("%d packets received.\n%d Packets dropped.\n", stat.bs_recv, stat.bs_drop);
      pa->bs_drop = stat.bs_drop;
    }
  }

  if ((pa != NULL) && (PacketReceivePacket(pa->lpAdapter, pa->lpPacket, TRUE))) {
    ProcessPackets(adapter, pa->lpPacket);
  }
}

/** Get the current linkg status
 * @param adapter adapter handle received by a call to init_adapter
 * @param linkstate the current link state
 * @return 1: succeeded, 0: failed
 */
static int
get_link_state(struct packet_adapter *pa, NDIS_MEDIA_STATE *linkstate)
{
  int ret = 0;
  if(pa != NULL) {
    PPACKET_OID_DATA ppacket_oid_data;

    /* get the media connect status of the selected adapter */
    ppacket_oid_data = (PPACKET_OID_DATA)malloc(sizeof(PACKET_OID_DATA) + sizeof(NDIS_MEDIA_STATE));
    if (ppacket_oid_data != NULL) {
      ppacket_oid_data->Oid    = OID_GEN_MEDIA_CONNECT_STATUS;
      ppacket_oid_data->Length = sizeof(NDIS_MEDIA_STATE);
      if (PacketRequest(pa->lpAdapter, FALSE, ppacket_oid_data)) {
        *linkstate = (*((PNDIS_MEDIA_STATE)(ppacket_oid_data->Data)));
        ret = 1;
      }
      free(ppacket_oid_data);
    }
  }

  return ret;
}

/**
 * Check for link state changes. Called in the main loop: 'interrupt' mode is not
 * really supported :(
 *
 * @param adapter adapter handle received by a call to init_adapter
 * @return one of the link_adapter_event values
 */
enum link_adapter_event
link_adapter(void *adapter)
{
  struct packet_adapter *pa = (struct packet_adapter*)adapter;

  if (pa != NULL) {
    NDIS_MEDIA_STATE fNdisMediaState;
    if (get_link_state(pa, &fNdisMediaState)) {
      if (pa->fNdisMediaState != fNdisMediaState) {
        pa->fNdisMediaState = fNdisMediaState;
        return ((fNdisMediaState == NdisMediaStateConnected) ? LINKEVENT_UP : LINKEVENT_DOWN);
      }
    }
  }

  return LINKEVENT_UNCHANGED;
}
