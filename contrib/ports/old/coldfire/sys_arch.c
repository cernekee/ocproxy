/* @(#)sys_arch.c
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
 * Author: David Haas   <dhaas@alum.rpi.edu>
 *
 */

#include "lwip/debug.h"

#include "lwip/sys.h"
#include "lwip/opt.h"
#include "lwip/stats.h"

#include "nucleus.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

struct sys_thread {
    struct sys_thread *next;
    struct sys_timeouts timeouts;
    int errno_i;
    NU_TASK *pthread;
    void (*function)(void *arg);
    void *arg;
};

struct sys_hisr 
{
    struct sys_hisr *next;
    NU_HISR *hisr;
    void (*disablefun) (void);
    u32_t vector;
};

static int num_sem = 0;                 // Number of semaphores created
static int num_mbox = 0;                // Number of mailboxes created
static int num_thread = 0;              // Number of threads created
static int num_hisr = 0;                // Number of hisrs created
static struct sys_thread *threads = NULL;
static struct sys_hisr *hisrs = NULL;

#define TICKS_PER_SECOND 10000
#define MS_TO_TICKS(MS) (MS * (TICKS_PER_SECOND / 1000))
#define TICKS_TO_MS(TICKS) ((unsigned long)((1000ULL * TICKS) / TICKS_PER_SECOND))
#define TICKS_TO_HUNDMICROSEC(TICKS) TICKS

#define SYS_MBOX_SIZE 128               // Number of elements in mbox queue
#define SYS_STACK_SIZE 2048             // A minimum Nucleus stack for coldfire
#define SYS_HISR_STACK_SIZE 2048             // A minimum Nucleus stack for coldfire

/*---------------------------------------------------------------------------------*/
void
sys_init(void)
{
    return;
}

/*---------------------------------------------------------------------------------*/
static void
sys_thread_entry(UNSIGNED argc, VOID *argv)
{
    /* argv is passed as a pointer to our thread structure */
    struct sys_thread *p_thread = (struct sys_thread *)argv;

    p_thread->function(p_thread->arg);
}

/*---------------------------------------------------------------------------------*/
static struct sys_thread * 
introduce_thread(NU_TASK *id, void (*function)(void *arg), void *arg)
{
  struct sys_thread *thread;
  sys_prot_t old_level;
  
  thread = (struct sys_thread *) calloc(1,sizeof(struct sys_thread));
    
  if (thread) {
      old_level = sys_arch_protect();
      thread->next = threads;
      thread->timeouts.next = NULL;
      thread->pthread = id;
      thread->function = function;
      thread->arg = arg;
      threads = thread;
      sys_arch_unprotect(old_level);
  }
    
  return thread;
}

/*---------------------------------------------------------------------------------*/
/* We use Nucleus task as thread. Create one with a standard size stack at a standard
 * priority. */
sys_thread_t
sys_thread_new(char *name, void (* function)(void *arg), void *arg, int stacksize, int prio)
{
    NU_TASK *p_thread;
    u8_t *p_stack;
    STATUS status;
    char thread_name[8] = "        ";
    struct sys_thread *st;
    
    /** @todo Replace SYS_STACK_SIZE by "stacksize" parameter, perhaps use "name" if it is prefered */
    
    p_stack = (u8_t *) malloc(SYS_STACK_SIZE);
    if (p_stack)
    {
        p_thread = (NU_TASK *) calloc(1,sizeof(NU_TASK));
        if (p_thread)
        {
            /* get a new thread structure */
            st = introduce_thread(p_thread, function, arg);
            if (st)
            {
                num_thread = (num_thread +1) % 100; // Only count to 99
                sprintf(thread_name, "lwip%02d", num_thread);
                thread_name[strlen(thread_name)] = ' ';
                
                status = NU_Create_Task(p_thread,
                                        thread_name,
                                        sys_thread_entry,
                                        0,
                                        st,
                                        p_stack,
                                        SYS_STACK_SIZE,
                                        prio,
                                        0,                          //Disable timeslicing
                                        NU_PREEMPT,
                                        NU_START);
                if (status == NU_SUCCESS)
                    return p_thread;
            }
            
        }
    }
    abort();
}

/*-----------------------------------------------------------------------------------*/
static struct sys_thread *
current_thread(void)
{
    struct sys_thread *st;
    sys_prot_t old_level;
    NU_TASK *pt;
    
    pt = NU_Current_Task_Pointer();
    old_level = sys_arch_protect();
    
    for(st = threads; st != NULL; st = st->next)
    {    
        if (st->pthread == pt)
        {
            sys_arch_unprotect(old_level);
            return st;
        }
    }

    sys_arch_unprotect(old_level);
    st = introduce_thread(pt, 0, 0);
    
    if (!st) {
        abort();
    }

    return st;
}


/*---------------------------------------------------------------------------------*/
struct sys_timeouts *
sys_arch_timeouts(void)
{
    struct sys_thread *thread;

    thread = current_thread();
    return &thread->timeouts;
}
/*---------------------------------------------------------------------------------*/
int *
sys_arch_errno(void)
{
    struct sys_thread *thread;

    thread = current_thread();
    return &thread->errno_i;
}

/*---------------------------------------------------------------------------------*/
sys_sem_t
sys_sem_new(u8_t count)
{
    STATUS status;
    NU_SEMAPHORE *sem;
    char sem_name[8] = "        ";

#ifdef SYS_STATS
    lwip_stats.sys.sem.used++;
    if (lwip_stats.sys.sem.used > lwip_stats.sys.sem.max)
    {
        lwip_stats.sys.sem.max = lwip_stats.sys.sem.used;
    }
#endif /* SYS_STATS */
    
    /* Get memory for new semaphore */
    sem = (NU_SEMAPHORE *) calloc(1,sizeof(NU_SEMAPHORE));

    if (sem)
    {
        /* Create a unique name for semaphore based on number created */
        num_sem = (num_sem + 1) % 100;  // Only count to 99
        sprintf(sem_name, "lwip%02d", num_sem);
        sem_name[strlen(sem_name)] = ' ';

        /* Ask nucleus to create semaphore */
        NU_Create_Semaphore(sem,
                            sem_name,
                            count,
                            NU_FIFO);
    }
    return sem;
}

/*---------------------------------------------------------------------------------*/
void
sys_sem_free(sys_sem_t sem)
{
    if (sem != SYS_SEM_NULL)
    {
#ifdef SYS_STATS
        lwip_stats.sys.sem.used--;
#endif /* SYS_STATS */
        NU_Delete_Semaphore(sem);
        free(sem);
    }
}

/*---------------------------------------------------------------------------------*/
void
sys_sem_signal(sys_sem_t sem)
{
    NU_Release_Semaphore(sem);
}

/*---------------------------------------------------------------------------------*/
u32_t
sys_arch_sem_wait(sys_sem_t sem, u32_t timeout)
{
    UNSIGNED timestart, timespent;
    STATUS status;

    /* Get the current time */
    timestart = NU_Retrieve_Clock();
    /* Wait for the semaphore */
    status = NU_Obtain_Semaphore(sem,
                                 timeout ? MS_TO_TICKS(timeout) : NU_SUSPEND);
    /* This next statement takes wraparound into account. It works. Really! */
    timespent = TICKS_TO_HUNDMICROSEC(((s32_t) ((s32_t) NU_Retrieve_Clock() - (s32_t) timestart)));
    
    if (status == NU_TIMEOUT)
        return SYS_ARCH_TIMEOUT;
    else
        /* Round off to milliseconds */
        return (timespent+5)/10;
}

/*---------------------------------------------------------------------------------*/
sys_mbox_t
sys_mbox_new(void)
{
    u32_t *p_queue_mem;
    NU_QUEUE *p_queue;
    char queue_name[8] = "        ";

    /* Allocate memory for queue */
    p_queue_mem = (u32_t *) calloc(1,(SYS_MBOX_SIZE * sizeof(u32_t)));
    if (p_queue_mem)
    {
        /* Allocate memory for queue control block */
        p_queue = (NU_QUEUE *) calloc(1,sizeof(NU_QUEUE));
        if (p_queue)
        {
            /* Create a unique name for mbox based on number created */
            num_mbox = (num_mbox + 1) % 100;
            sprintf(queue_name, "lwip%02d", num_mbox);
            queue_name[strlen(queue_name)] = ' ';
            
            NU_Create_Queue(p_queue,
                            queue_name,
                            p_queue_mem,
                            SYS_MBOX_SIZE,
                            NU_FIXED_SIZE,
                            1,
                            NU_FIFO);
#ifdef SYS_STATS
            lwip_stats.sys.mbox.used++;
            if (lwip_stats.sys.mbox.used > lwip_stats.sys.mbox.max) {
                lwip_stats.sys.mbox.max = lwip_stats.sys.mbox.used;
            }
#endif /* SYS_STATS */
            return p_queue;
        }
        else
            free(p_queue_mem);
    }
    return SYS_MBOX_NULL;
}

/*---------------------------------------------------------------------------------*/
void
sys_mbox_free(sys_mbox_t mbox)
{
    VOID *p_queue_mem;
    CHAR name[8];
    UNSIGNED queue_size;
    UNSIGNED available;
    UNSIGNED messages;
    OPTION message_type;
    UNSIGNED message_size;
    OPTION suspend_type;
    UNSIGNED tasks_waiting;
    NU_TASK *first_task;
    STATUS status;

    if (mbox != SYS_MBOX_NULL)
    {
        /* First we need to get address of queue memory. Ask Nucleus
           for information about the queue */
        status = NU_Queue_Information(mbox,
                                      name,
                                      &p_queue_mem,
                                      &queue_size,
                                      &available,
                                      &messages,
                                      &message_type,
                                      &message_size,
                                      &suspend_type,
                                      &tasks_waiting,
                                      &first_task);
        if (status == NU_SUCCESS)
            free(p_queue_mem);
        NU_Delete_Queue(mbox);
        free(mbox);
#ifdef SYS_STATS
        lwip_stats.sys.mbox.used--;
#endif /* SYS_STATS */
    }
    
}

/*---------------------------------------------------------------------------------
  This function sends a message to a mailbox. It is unusual in that no error
  return is made. This is because the caller is responsible for ensuring that
  the mailbox queue will not fail. The caller does this by limiting the number
  of msg structures which exist for a given mailbox.
  ---------------------------------------------------------------------------------*/
void
sys_mbox_post(sys_mbox_t mbox, void *msg)
{
    UNSIGNED status;
    
    LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_post: mbox %p msg %p\n", mbox, msg));
    status = NU_Send_To_Queue(mbox,
                              &msg,
                              1,
                              NU_NO_SUSPEND);
    LWIP_ASSERT("sys_mbox_post: mbx post failed", status == NU_SUCCESS);
}
/*---------------------------------------------------------------------------------*/
u32_t
sys_arch_mbox_fetch(sys_mbox_t mbox, void **msg, u32_t timeout)
{
    UNSIGNED timestart, timespent;
    STATUS status;
    void *ret_msg;
    UNSIGNED actual_size;

    /* Get the current time */
    timestart = NU_Retrieve_Clock();

    /* Wait for message */
    status = NU_Receive_From_Queue(mbox,
                                   &ret_msg,
                                   1,
                                   &actual_size,
                                   timeout ? MS_TO_TICKS(timeout) : NU_SUSPEND);

    if (status == NU_SUCCESS)
    {
        if (msg)    
            *msg = ret_msg;
        LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_fetch: mbox %p msg %p\n", mbox, ret_msg));
    } else {
        if (msg)
            *msg = 0;
        if (status == NU_TIMEOUT)
            LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_fetch: timeout on mbox %p\n", mbox));
        else
            LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_fetch: Queue Error %i on mbox %p\n", status, mbox));
    }
   
    
    /* This next statement takes wraparound into account. It works. Really! */
    timespent = TICKS_TO_HUNDMICROSEC(((s32_t) ((s32_t) NU_Retrieve_Clock() - (s32_t) timestart)));
    
    if (status == NU_TIMEOUT)
        return SYS_ARCH_TIMEOUT;
    else
        /* Round off to milliseconds */
        return (timespent+5)/10;
}

/*---------------------------------------------------------------------------------*/
static void
sys_arch_lisr(INT vector_number)
{
    struct sys_hisr *p_hisr = hisrs;

    /* Determine which HISR to activate */
    while (p_hisr != NULL)
    {
        if (vector_number == p_hisr->vector)
        {
            if (p_hisr->disablefun)
                (*p_hisr->disablefun)();
            NU_Activate_HISR(p_hisr->hisr);
            break;
        }
        p_hisr = p_hisr->next;
    }
    return;
}


/*---------------------------------------------------------------------------------*/
void
sys_setvect(u32_t vector, void (*isr_function)(void), void (*dis_funct)(void))
{
    /* The passed function is called as a high level ISR on the selected vector.
       It is assumed that all the functions in this module can be called by the
       isr_function.
    */
    struct sys_hisr *p_hisr = hisrs;
    INT old_level;
    NU_HISR *nucleus_hisr;
    u8_t *p_stack;
    STATUS status;
    char hisr_name[8] = "        ";
    void (*old_lisr)(INT);

    /* In this case a Nucleus HISR is created for the isr_function. This
     * requires it's own stack. Also get memory for Nucleus HISR. */
    nucleus_hisr = (NU_HISR *) calloc(1,sizeof(NU_HISR));
    if (nucleus_hisr)
    {
        p_stack = (u8_t *) malloc(SYS_HISR_STACK_SIZE);
        if (p_stack)
        {
    
            /* It is most efficient to disable interrupts for Nucleus for a short
               time. Chances are we are doing this while interrupts are disabled
               already during system initialization.
            */
            old_level = NU_Control_Interrupts(NU_DISABLE_INTERRUPTS);
            
            /* It is a simplification here that once an HISR is set up for a particular
             * vector it will never be set up again. This way if the init code is called
             * more than once it is harmless (no memory leaks)
             */
            while (p_hisr != NULL)
            {
                if (vector == p_hisr->vector)
                {
                    NU_Control_Interrupts(old_level);
                    free(p_stack);
                    free(nucleus_hisr);
                    return;
                }
                p_hisr = p_hisr->next;
            }

            /* Get a sys_hisr structure */
            p_hisr = (struct sys_hisr *) calloc(1,sizeof(struct sys_hisr));
            if (p_hisr)
            {
                p_hisr->next = hisrs;
                p_hisr->vector = vector;
                p_hisr->hisr = nucleus_hisr;
                p_hisr->disablefun = dis_funct;
                hisrs = p_hisr;
                
                NU_Control_Interrupts(old_level);

                num_hisr = (num_hisr + 1) % 100;
                sprintf(hisr_name, "lwip%02d", num_hisr);
                hisr_name[strlen(hisr_name)] = ' ';
                
                /* Ask Nucleus to create the HISR */
                status = NU_Create_HISR(p_hisr->hisr,
                                        hisr_name,
                                        isr_function,
                                        1,      //Priority 0-2
                                        p_stack,
                                        SYS_HISR_STACK_SIZE);
                if (status == NU_SUCCESS)
                {
                    /* Plug vector with system lisr now */
                    NU_Register_LISR(vector, sys_arch_lisr, &old_lisr);
                    return;     //Success
                }
            }
            NU_Control_Interrupts(old_level);
        }
    }
    /* Errors should be logged here */
    abort();
}

/*---------------------------------------------------------------------------------*/
/** sys_prot_t sys_arch_protect(void)

This optional function does a "fast" critical region protection and returns
the previous protection level. This function is only called during very short
critical regions. An embedded system which supports ISR-based drivers might
want to implement this function by disabling interrupts. Task-based systems
might want to implement this by using a mutex or disabling tasking. This
function should support recursive calls from the same task or interrupt. In
other words, sys_arch_protect() could be called while already protected. In
that case the return value indicates that it is already protected.

sys_arch_protect() is only required if your port is supporting an operating
system.
*/
sys_prot_t
sys_arch_protect(void)
{
    return NU_Control_Interrupts(NU_DISABLE_INTERRUPTS);
}

/*---------------------------------------------------------------------------------*/
/** void sys_arch_unprotect(sys_prot_t pval)

This optional function does a "fast" set of critical region protection to the
value specified by pval. See the documentation for sys_arch_protect() for
more information. This function is only required if your port is supporting
an operating system.
*/
void
sys_arch_unprotect(sys_prot_t pval)
{
    NU_Control_Interrupts(pval);
}

/*********************************************************************
 * void sys_get_eth_addr(struct eth_addr *eth_addr)
 *
 * Get configured ethernet address from nvram and return it
 * in a eth_addr structure.
 *********************************************************************/
void
sys_get_eth_addr(struct eth_addr *eth_addr)
{
    Cfg_lan *p_lan = config_get_lan_setup();

    eth_addr->addr[0] = (u8_t) ((p_lan->etheraddrhi >> 16) & 0xff);
    eth_addr->addr[1] = (u8_t) ((p_lan->etheraddrhi >> 8) & 0xff);
    eth_addr->addr[2] = (u8_t) ((p_lan->etheraddrhi) & 0xff);
    eth_addr->addr[3] = (u8_t) ((p_lan->etheraddrlo >> 16) & 0xff);
    eth_addr->addr[4] = (u8_t) ((p_lan->etheraddrlo >> 8) & 0xff);
    eth_addr->addr[5] = (u8_t) ((p_lan->etheraddrlo) & 0xff);
}
