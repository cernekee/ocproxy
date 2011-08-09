/*
*********************************************************************************************************
*                                              lwIP TCP/IP Stack
*                                    	 port for uC/OS-II RTOS on TIC6711 DSK
*
* File : sys_arch.c
* By   : ZengMing @ DEP,Tsinghua University,Beijing,China
* Reference: YangYe's source code for SkyEye project
*********************************************************************************************************
*/

//#include "lwip/debug.h"

#include "lwip/def.h"
#include "lwip/sys.h"
#include "lwip/mem.h"

#include "arch/sys_arch.h" 

static OS_MEM *pQueueMem;

const void * const pvNullPointer;

static char pcQueueMemoryPool[MAX_QUEUES * sizeof(TQ_DESCR) ];

struct sys_timeouts lwip_timeouts[LWIP_TASK_MAX+1];
struct sys_timeouts null_timeouts;

OS_STK LWIP_TASK_STK[LWIP_TASK_MAX+1][LWIP_STK_SIZE];


/*-----------------------------------------------------------------------------------*/
/* This func should be called first in lwip task!
 * -------------------------------------------------		*/
void sys_init(void)
{
    u8_t i;
    u8_t   ucErr;
    //init mem used by sys_mbox_t //use ucosII functions
    pQueueMem = OSMemCreate( (void*)pcQueueMemoryPool, MAX_QUEUES, sizeof(TQ_DESCR), &ucErr );
    //init lwip_timeouts for every lwip task
    for(i=0;i<LWIP_TASK_MAX+1;i++){
    	lwip_timeouts[i].next = NULL;
    }
}


/*-----------------------------------------------------------------------------------*/

sys_sem_t sys_sem_new(u8_t count)
{
    sys_sem_t pSem;
    pSem = OSSemCreate((u16_t)count );
    return pSem;
}


/*-----------------------------------------------------------------------------------*/

void sys_sem_free(sys_sem_t sem)
{
    u8_t     ucErr;
    (void)OSSemDel((OS_EVENT *)sem, OS_DEL_NO_PEND, &ucErr );
}

/*-----------------------------------------------------------------------------------*/

void sys_sem_signal(sys_sem_t sem)
{
    OSSemPost((OS_EVENT *)sem );
}


/*-----------------------------------------------------------------------------------*/

u32_t sys_arch_sem_wait(sys_sem_t sem, u32_t timeout)
{
  u8_t err;
  u32_t ucos_timeout;

  ucos_timeout = 0;
  if(timeout != 0){
  	ucos_timeout = (timeout * OS_TICKS_PER_SEC) / 1000;
  	if(ucos_timeout < 1)
  		ucos_timeout = 1;
  	else if(ucos_timeout > 65535) //ucOS only support u16_t pend
  		ucos_timeout = 65535;
  }
  	
  OSSemPend ((OS_EVENT *)sem,(u16_t)ucos_timeout, (u8_t *)&err);
  
  if(err == OS_TIMEOUT)
  	return 0;	// only when timeout!
  else
  	return 1;
}


/*-----------------------------------------------------------------------------------*/
sys_mbox_t sys_mbox_new(void)
{
    u8_t       ucErr;
    PQ_DESCR    pQDesc;
    
    pQDesc = OSMemGet( pQueueMem, &ucErr );
    if( ucErr == OS_NO_ERR ) {   
        pQDesc->pQ = OSQCreate( &(pQDesc->pvQEntries[0]), MAX_QUEUE_ENTRIES );       
        if( pQDesc->pQ != NULL ) {
            return pQDesc;
        }
    } 
    return SYS_MBOX_NULL;
}

/*-----------------------------------------------------------------------------------*/
void
sys_mbox_free(sys_mbox_t mbox)
{
    u8_t     ucErr;
    
    //clear OSQ EVENT
    OSQFlush( mbox->pQ );
    //del OSQ EVENT
    (void)OSQDel( mbox->pQ, OS_DEL_NO_PEND, &ucErr);
    //put mem back to mem queue
    ucErr = OSMemPut( pQueueMem, mbox );
}

/*-----------------------------------------------------------------------------------*/
void
sys_mbox_post(sys_mbox_t mbox, void *msg)
{
    if( !msg ) 
	msg = (void*)&pvNullPointer;
    (void)OSQPost( mbox->pQ, msg);
}

/*-----------------------------------------------------------------------------------*/
u32_t 
sys_arch_mbox_fetch(sys_mbox_t mbox, void **msg, u32_t timeout)
{
    u8_t     ucErr;
    u32_t ucos_timeout;

  ucos_timeout = 0;
  if(timeout != 0){
  ucos_timeout = (timeout * OS_TICKS_PER_SEC)/1000;
  if(ucos_timeout < 1)
  	ucos_timeout = 1;
  else if(ucos_timeout > 65535)	//ucOS only support u16_t timeout
  	ucos_timeout = 65535;
  }  
    
  if(msg != NULL){
    *msg = OSQPend( mbox->pQ, (u16_t)ucos_timeout, &ucErr );        
  }else{
    //just discard return value if msg==NULL
    OSQPend(mbox->pQ,(u16_t)ucos_timeout,&ucErr);
  }
    
  if( ucErr == OS_TIMEOUT ) {
        timeout = 0;
    } else {
      if(*msg == (void*)&pvNullPointer ) 
	  *msg = NULL;
      timeout = 1;
    }
    
  return timeout;
}



/*----------------------------------------------------------------------*/
struct 
sys_timeouts * sys_arch_timeouts(void)
{
  u8_t curr_prio;
  s16_t offset;
  
  OS_TCB curr_task_pcb;
  
  null_timeouts.next = NULL;
  
  OSTaskQuery(OS_PRIO_SELF,&curr_task_pcb);
  curr_prio = curr_task_pcb.OSTCBPrio;
  
  offset = curr_prio - LWIP_START_PRIO;

  if(curr_prio == TCPIP_THREAD_PRIO) 
  		return &lwip_timeouts[LWIP_TASK_MAX];
  else if(offset >= 0 && offset < LWIP_TASK_MAX)
  		return &lwip_timeouts[offset];
  else return &null_timeouts;
  
  //if not called by a lwip task ,return timeouts->NULL
}


/*------------------------------------------------------------------------*/
sys_thread_t sys_thread_new(char *name, void (* thread)(void *arg), void *arg, int stacksize, int prio)
{
  /** @todo Replace LWIP_TASK_STK by the use of "stacksize" parameter */
  if(prio == TCPIP_THREAD_PRIO){
	OSTaskCreate(thread, (void *)0x1111, &LWIP_TASK_STK[LWIP_TASK_MAX][LWIP_STK_SIZE-1], prio);
		return prio;
  }  
  else if(prio - LWIP_START_PRIO  < LWIP_TASK_MAX){  
	OSTaskCreate(thread, (void *)0x1111, &LWIP_TASK_STK[prio - LWIP_START_PRIO][LWIP_STK_SIZE-1], prio);
		return prio;
  }
  else {
		printf(" lwip task prio out of range ! error! ");
		return 0;
  }
}







