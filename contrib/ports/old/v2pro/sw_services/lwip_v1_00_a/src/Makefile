###############################################################################
# Copyright (c) 2001-2003 Swedish Institute of Computer Science.
# All rights reserved. 
#
# Copyright (c) 2003 Xilinx, Inc.
# All rights reserved. 
# 
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions 
# are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission. 
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO 
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; 
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS".
# BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS ONE POSSIBLE 
# IMPLEMENTATION OF THIS FEATURE, APPLICATION OR STANDARD, XILINX 
# IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION IS FREE FROM 
# ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE FOR OBTAINING 
# ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.  XILINX 
# EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO THE 
# ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY 
# WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE 
# FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY 
# AND FITNESS FOR A PARTICULAR PURPOSE.
#
# This file is part of the lwIP TCP/IP stack.
#
# Author: Chris Borrelli <Chris.Borrelli@xilinx.com>
#

#
# This Makefile is meant to be run from EDK (libgen).  Please do not try
# try using this Makefile stand-alone on the command line; it will not work
#

COMPILER=powerpc-eabi-gcc
ARCHIVER=powerpc-eabi-ar
ARFLAGS=rs
CP=cp
COMPILERFLAGS=-Wall -gdwarf
#EXTRA_COMPILER_FLAGS = -DIPv4 -fpack-struct -msdata=eabi -mcpu=403
EXTRA_COMPILER_FLAGS = -DIPv4 -msdata=eabi -mcpu=403
LWIPDIR=$(TOPDIR)/lwip/src
ARCHDIR=$(TOPDIR)/contrib/ports/v2pro
LIB=liblwip4.a
RELEASEDIR=../../../lib
INCLUDEDIR=../../../include

INCLUDES= \
	-I$(LWIPDIR)/include \
	-I$(LWIPDIR)/include/ipv4 \
	-I$(ARCHDIR) \
	-I$(ARCHDIR)/include \
	-I$(INCLUDEDIR) 

INCLUDEFILES=$(TOPDIR)/lwip/src/include/ipv4/lwip \
             $(TOPDIR)/lwip/src/include/lwip \
             $(TOPDIR)/lwip/src/include/netif \
             $(TOPDIR)/contrib/ports/v2pro/include/netif \
             $(TOPDIR)/contrib/ports/v2pro/include/arch \
             $(TOPDIR)/contrib/ports/v2pro/lwipopts.h

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=$(LWIPDIR)/core/mem.c \
          $(LWIPDIR)/core/memp.c \
          $(LWIPDIR)/core/netif.c \
          $(LWIPDIR)/core/pbuf.c \
          $(LWIPDIR)/core/stats.c \
          $(LWIPDIR)/core/sys.c \
          $(LWIPDIR)/core/tcp.c \
          $(LWIPDIR)/core/tcp_in.c \
          $(LWIPDIR)/core/tcp_out.c \
          $(LWIPDIR)/core/udp.c \

CORE4FILES=$(LWIPDIR)/core/ipv4/icmp.c \
           $(LWIPDIR)/core/ipv4/ip.c \
	        $(LWIPDIR)/core/inet.c \
           $(LWIPDIR)/core/ipv4/ip_addr.c \
           $(LWIPDIR)/core/ipv4/ip_frag.c

# NETIFFILES: Files implementing various generic network interface functions.
NETIFFILES=$(LWIPDIR)/netif/etharp.c

# ARCHFILES: Archiecture specific files.
ARCHFILES=$(ARCHDIR)/lib_arch.c \
          $(ARCHDIR)/perf.c \
          $(ARCHDIR)/netif/xemacif.c \
          xemacif_g.c

# LWIPFILES: All the above.
LWIPFILES=$(COREFILES) $(CORE4FILES) $(NETIFFILES) $(ARCHFILES)
LWIPFILESW=$(wildcard $(LWIPFILES))
LWIPOBJS=$(notdir $(LWIPFILESW:.c=.o))

help:
	@echo ""
	@echo "This Makefile generates a library for the v2pro port of lwIP."
	@echo ""
	@echo "   Below are the targets:"
	@echo ""
	@echo "      make lwiplibs"
	@echo "            This is the target that will compile all lwIP files, and"
	@echo "            build the library, $(LIB).  The final library will be"
	@echo "            copied to $(RELEASEDIR)."
	@echo ""
	@echo "      make lwipinclude"	
	@echo "            Copies the lwIP include files into the EDK directory,"
	@echo "            $(INCLUDEDIR)"
	@echo ""
	@echo "      make clean"
	@echo "            Removes all object files and the $(LIB) library from"
	@echo "            the local directory."
	@echo ""
	@echo "      make depend"
	@echo "            Generates the .depend file, which contains the"
	@echo "            dependancies needed to compile each .c file.  The"
	@echo "            .depend file will be generated automatically for"
	@echo "            other targets, so it is not nessessary to run"
	@echo "            make depend."
	@echo ""

%.o:
	@echo ""
	@echo "Building File: $@"
	@echo ""
	$(COMPILER) $(COMPILERFLAGS) $(EXTRA_COMPILER_FLAGS) $(INCLUDES) -c $(<:.o=.c)

all ipv4 lwiplibs: lwipinclude $(LIB)
.PHONY: all ipv4 lwiplibs lwip_include help

lwipinclude:
	@for i in $(INCLUDEFILES); do \
      echo ${CP} -r $$i ${INCLUDEDIR}; \
      ${CP} -r $$i ${INCLUDEDIR}; \
   done

include:
	@exit

libs:
	@exit

clean:
	@echo ""
	@echo "Cleaning up..."
	@echo ""
	@rm -f *.o *.a .depend* core

depend dep: .depend
	@exit

$(LIB): $(LWIPOBJS)
	@echo ""
	@echo "Building lwIP Library, $@"
	$(ARCHIVER) $(ARFLAGS) $@ $?
	@cp $@ $(RELEASEDIR)/

.depend: $(LWIPFILES) $(APPFILES)
	@echo ""
	@echo "Building dependancies..."
	$(COMPILER) $(COMPILERFLAGS) $(EXTRA_COMPILER_FLAGS) $(INCLUDES) \
               -MM $^ > .depend || rm -f .depend

ifneq ($(MAKECMDGOALS), clean)
ifneq ($(MAKECMDGOALS), depend)
ifneq ($(MAKECMDGOALS), dep)
ifneq ($(MAKECMDGOALS), libs)
ifneq ($(MAKECMDGOALS), include)
ifneq ($(MAKECMDGOALS), help)
-include .depend
endif
endif
endif
endif
endif
endif
