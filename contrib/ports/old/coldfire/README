###  README --- c:/cygwin/home/dhaas/work/cfimage/lwip/arch/coldfire/

##
## Author: dhaas@alum.rpi.edu

These files are a port of lwip to coldfire (specifically the MCF5272 with
on-board FEC) under the Nucleus OS. Nucleus is pretty generic so it should be
fairly easy to port this to any other embedded OS. Nucleus memory managment
is not used. It is assumed you have a working malloc (which at least
long-word aligns memory).

The compiler used was Diab 4.3b. You will almost certainly need to change
cc.h for your compiler.

IMPORTANT NOTE: If you use the fec driver for a different processor which has
a data cache you will need to make sure the buffer descriptors and memory
used for pbufs are not in a cachable area. Otherwise the fec driver is
guarrenteed to malfunction. The 5272 which this was written for does not
support data cache so it did not matter and malloc was used.

