#ifndef __CC_H__
#define __CC_H__

/* memset(), memcpy() */
#include <string.h>
/* printf() and abort() */
#include <stdio.h>
#include <stdlib.h>
/* isdigit() */
#include <ctype.h>

#include "arch/cpu.h"

typedef unsigned   char    u8_t;
typedef signed     char    s8_t;
typedef unsigned   short   u16_t;
typedef signed     short   s16_t;
typedef unsigned   long    u32_t;
typedef signed     long    s32_t;

typedef u32_t mem_ptr_t;

/* Define (sn)printf formatters for these lwIP types */
#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"

/* LW: Supported in at least >=v7.5 r2, but lwIP worked without the "_packed" attribute already */
#define PACK_STRUCT_BEGIN _packed
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(x) _ror(x,8)
#define LWIP_PLATFORM_HTONL(x) c16x_htonl(x)

_inline u32_t c16x_htonl(u32_t n)
{
  u16_t msw, lsw;

  msw = n >> 16;
  msw = _ror(msw,8);
  lsw = n;
  lsw = _ror(lsw,8);
  n = ((u32_t)lsw << 16) | (u32_t)msw;
  return n;
}

#ifdef LWIP_DEBUG

/* LW: forward declaration */
void debug_printf(char *format, ...);
void page_printf(char *format, ...);

/* Plaform specific diagnostic output */
#define LWIP_PLATFORM_DIAG(x) { debug_printf x; }
#define LWIP_PLATFORM_ASSERT(x) { debug_printf("\fline %d in %s\n", __LINE__, __FILE__);  while(1); }

#endif/* LWIP_DEBUG */

#endif /* __CC_H__ */
