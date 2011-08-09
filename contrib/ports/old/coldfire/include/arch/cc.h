/*
 * Copyright (c) 2001-2003, Swedish Institute of Computer Science.
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
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 * $Id: cc.h,v 1.1 2007/06/14 12:34:00 kieranm Exp $
 */
#ifndef __CC_H__
#define __CC_H__

#include <types.h>
#include <string.h>

/* Specific code for NBS Card Technology */
#ifdef CARDTECH
#include <errorlog.h>
#endif

#define BYTE_ORDER BIG_ENDIAN
#define IMM_ADDRESS		(0x10000000)
#define FEC_LEVEL		4

// typedef unsigned   char    u8_t;
// typedef signed     char    s8_t;
// typedef unsigned   short   u16_t;
// typedef signed     short   s16_t;
// typedef unsigned   long    u32_t;
// typedef signed     long    s32_t;

typedef u32_t mem_ptr_t;

/* Compiler hints for packing structures */
#define PACK_STRUCT_BEGIN #pragma pack(1,1,0)
#define PACK_STRUCT_STRUCT
#define ALIGN_STRUCT_8_BEGIN #pragma pack(1,8,0)
#define ALIGN_STRUCT_END #pragma pack()
#define PACK_STRUCT_END #pragma pack()
#define PACK_STRUCT_FIELD(x) x


#define _SYS_TYPES_FD_SET
#define	NBBY	8		/* number of bits in a byte */

#ifndef	FD_SETSIZE
#define	FD_SETSIZE	64
#endif /* FD_SETSIZE */

typedef	long	fd_mask;
#define	NFDBITS	(sizeof (fd_mask) * NBBY)	/* bits per mask */
#ifndef	howmany
#define	howmany(x,y)	(((x)+((y)-1))/(y))
#endif /* howmany */

typedef	struct _types_fd_set {
	fd_mask	fds_bits[howmany(FD_SETSIZE, NFDBITS)];
} _types_fd_set;

#define fd_set _types_fd_set

#define	FD_SET(n, p)	((p)->fds_bits[(n)/NFDBITS] |= (1L << ((n) % NFDBITS)))
#define	FD_CLR(n, p)	((p)->fds_bits[(n)/NFDBITS] &= ~(1L << ((n) % NFDBITS)))
#define	FD_ISSET(n, p)	((p)->fds_bits[(n)/NFDBITS] & (1L << ((n) % NFDBITS)))
#define	FD_ZERO(p)	 do {                   \
     size_t __i;                                \
     char *__tmp = (char *)p;                   \
     for (__i = 0; __i < sizeof (*(p)); ++__i)  \
       *__tmp++ = 0;                            \
} while (0)

/* prototypes for printf() and abort() */
#include <stdio.h>
#include <stdlib.h>
/* Plaform specific diagnostic output */
#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(x)	do {printf x;} while(0)
#endif

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(x) do {printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     x, __LINE__, __FILE__); fflush(NULL); abort();} while(0)
#endif

asm u32_t GET_CALLER_PC (void)
{
! "d0"
    move.l 4(a6),d0
}


#endif /* __CC_H__ */
