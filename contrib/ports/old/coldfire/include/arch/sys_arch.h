/* @(#)sys_arch.h
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: David Haas
 *
 */

#ifndef _SYS_ARCH_H
#define _SYS_ARCH_H 1

#include <nucleus.h>
#include <stdlib.h>
#include "netif/etharp.h"

#define SYS_MBOX_NULL NULL
#define SYS_SEM_NULL  NULL

/* sockets needs this definition. include time.h if this is a unix system */
struct timeval {
  long tv_sec;
  long tv_usec;
};

typedef NU_SEMAPHORE * sys_sem_t;
typedef NU_QUEUE * sys_mbox_t;
typedef NU_TASK * sys_thread_t;
typedef u32_t sys_prot_t;

/* Functions specific to Coldfire/Nucleus */
void
sys_setvect(u32_t vector, void (*isr_function)(void), void (*dis_funct)(void));
void
sys_get_eth_addr(struct eth_addr *eth_addr);
int *
sys_arch_errno(void);


#include "arch/errno.h"

#endif /* _SYS_ARCH_H */

