/*
*********************************************************************************************************
*                                              lwIP TCP/IP Stack
*                                    	 port for uC/OS-II RTOS on TIC6711 DSK
*
* File : sys_arch.h
* By   : ZengMing @ DEP,Tsinghua University,Beijing,China
* Reference: YangYe's source code for SkyEye project
*********************************************************************************************************
*/
#ifndef __SYS_ARCH_H__
#define __SYS_ARCH_H__

#include    "os_cpu.h"
#include    "os_cfg.h"
#include    "ucos_ii.h"

#define LWIP_STK_SIZE      4096

#define LWIP_TASK_MAX    5	//max number of lwip tasks
#define LWIP_START_PRIO  5   //first prio of lwip tasks
	//!!!! so priority of lwip tasks is from 5-9 

#define SYS_MBOX_NULL   (void*)0
#define SYS_SEM_NULL    (void*)0

#define MAX_QUEUES        20
#define MAX_QUEUE_ENTRIES 20

typedef struct {
    OS_EVENT*   pQ;
    void*       pvQEntries[MAX_QUEUE_ENTRIES];
} TQ_DESCR, *PQ_DESCR;
    
typedef OS_EVENT* sys_sem_t;
typedef PQ_DESCR  sys_mbox_t;//the structure defined above
typedef INT8U     sys_thread_t;


/* Critical Region Protection */
/* These functions must be implemented in the sys_arch.c file.
   In some implementations they can provide a more light-weight protection
   mechanism than using semaphores. Otherwise semaphores can be used for
   implementation */
/** SYS_ARCH_DECL_PROTECT
 * declare a protection variable. This macro will default to defining a variable of
 * type sys_prot_t. If a particular port needs a different implementation, then
 * this macro may be defined in sys_arch.h.
 */
#define SYS_ARCH_DECL_PROTECT(lev) 
/** SYS_ARCH_PROTECT
 * Perform a "fast" protect. This could be implemented by
 * disabling interrupts for an embedded system or by using a semaphore or
 * mutex. The implementation should allow calling SYS_ARCH_PROTECT when
 * already protected. The old protection level is returned in the variable
 * "lev". This macro will default to calling the sys_arch_protect() function
 * which should be implemented in sys_arch.c. If a particular port needs a
 * different implementation, then this macro may be defined in sys_arch.h
 */
#define SYS_ARCH_PROTECT(lev) OS_ENTER_CRITICAL()
/** SYS_ARCH_UNPROTECT
 * Perform a "fast" set of the protection level to "lev". This could be
 * implemented by setting the interrupt level to "lev" within the MACRO or by
 * using a semaphore or mutex.  This macro will default to calling the
 * sys_arch_unprotect() function which should be implemented in
 * sys_arch.c. If a particular port needs a different implementation, then
 * this macro may be defined in sys_arch.h
 */
#define SYS_ARCH_UNPROTECT(lev) OS_EXIT_CRITICAL() 



#endif
