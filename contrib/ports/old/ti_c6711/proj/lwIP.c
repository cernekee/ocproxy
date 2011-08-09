/*
*********************************************************************************************************
*                                              lwIP TCP/IP Stack
*                                    	 port for uC/OS-II RTOS on TIC6711 DSK
*
* File : tcp_ip.c
* By   : ZengMing @ DEP,Tsinghua University,Beijing,China
*********************************************************************************************************
*/
#include    <stdio.h>
#include    <string.h>
#include    <ctype.h>
#include    <stdlib.h>

#include "..\uCOS-II\TI_C6711\DSP_C6x_Vectors\DSP_C6x_Vectors.H"

// -- Architecture Files --
#include "..\lwIP\arch\TI_C6711\sys_arch.c"
#include "..\lwIP\arch\TI_C6711\netif\ne2kif.c"
// -- Generic network interface --
#include "..\lwIP\src\netif\loopif.c"
#include "..\lwIP\src\netif\etharp.c"

// -- common file
#include "..\lwIP\src\core\mem.c"
#include "..\lwIP\src\core\memp.c"
#include "..\lwIP\src\core\netif.c"
#include "..\lwIP\src\core\pbuf.c"
#include "..\lwIP\src\core\stats.c"
#include "..\lwIP\src\core\sys.c"
#include "..\lwIP\src\core\tcp.c"
#include "..\lwIP\src\core\tcp_in.c"
#include "..\lwIP\src\core\tcp_out.c"
#include "..\lwIP\src\core\udp.c"
#include "..\lwIP\src\core\raw.c"
// -- target ipv4 --
#include "..\lwIP\src\core\ipv4\icmp.c"
#include "..\lwIP\src\core\ipv4\ip.c"
#include "..\lwIP\src\core\inet.c"
#include "..\lwIP\src\core\ipv4\ip_addr.c"
#include "..\lwIP\src\core\ipv4\ip_frag.c"
// -- sequential and socket APIs --
#include "..\lwIP\src\api\api_lib.c"
#include "..\lwIP\src\api\api_msg.c"
#include "..\lwIP\src\api\tcpip.c"
#include "..\lwIP\src\api\err.c"
#include "..\lwIP\src\api\sockets.c"

// sample task code (http demo)
#include "sample_http.c"

struct netif ne2kif_if;
//struct netif loop_if;

void ethernet_hardreset(void);	//These reset codes are built for C6711 DSP
void tcpip_init_done_ok(void * arg);

void Task_lwip_init(void * pParam)
{
  struct ip_addr ipaddr, netmask, gw;
  sys_sem_t sem;
  
  ethernet_hardreset();//hard reset of EthernetDaughterCard
  
  #if LWIP_STATS
  stats_init();
  #endif
  // initial lwIP stack
  sys_init();
  mem_init();
  memp_init();
  pbuf_init();
  netif_init(); 
  lwip_socket_init();

  printf("TCP/IP initializing...\n");  
  sem = sys_sem_new(0);
  tcpip_init(tcpip_init_done_ok, &sem);
  sys_sem_wait(sem);
  sys_sem_free(sem);
  printf("TCP/IP initialized.\n");
  
  //add loop interface //set local loop-interface 127.0.0.1
  /*
  IP4_ADDR(&gw, 127,0,0,1);
  IP4_ADDR(&ipaddr, 127,0,0,1);
  IP4_ADDR(&netmask, 255,0,0,0);
  netif_add(&loop_if, &ipaddr, &netmask, &gw, NULL, loopif_init,
	    tcpip_input);*/

  //add ne2k interface
  IP4_ADDR(&gw, 166,111,32,1);
  IP4_ADDR(&ipaddr, 166,111,33,120);
  IP4_ADDR(&netmask, 255,255,254,0);

  netif_add(&ne2kif_if, &ipaddr, &netmask, &gw, NULL, ne2k_init, tcpip_input);
  netif_set_default(&ne2kif_if);
  netif_set_up(&ne2kif_if); // new step from lwip 1.0.0
  
  printf("Applications started.\n");
  
  //------------------------------------------------------------
  //All thread(task) of lwIP must have their PRI between 10 and 14.
  //  sys_thread_new("httpd_init", httpd_init, (void*)"httpd", DEFAULT_THREAD_STACKSIZE, 10);
  //------------------------------------------------------------
  httpd_init();//sample_http
  
  printf("lwIP threads created!\n");
  
  DSP_C6x_TimerInit(); // Timer interrupt enabled
  DSP_C6x_Int4Init();  // Int4(Ethernet Chip int) enabled

  /* Block for ever. */
  sem = sys_sem_new(0);
  sys_sem_wait(sem);
  printf(" never goes here, should not appear!\n");
}

//---------------------------------------------------------
void tcpip_init_done_ok(void * arg)
{
  sys_sem_t *sem;
  sem = arg;
  sys_sem_signal(*sem);
}


/*-----------------------------------------------------------*/
/*  This function do the hard reset of EthernetDaughterCard  *
 *   through the DaughterBoardControl0 signal in DB-IF		 */  
/*-----------------------------------------------------------*/
void ethernet_hardreset(void)	//These reset codes are built for C6711 DSK
{
	u32_t i;

	OS_ENTER_CRITICAL();

//SET LED1 ON AND /RST pin Low
	*(unsigned char *)0x90080003 = 0x06;
	for (i=0;i<DELAY;i++);
//SET LED2 ON AND /RST pin High  _|-|_
	*(unsigned char *)0x90080003 = 0xfd;
 	for (i=0;i<DELAY;i++);	  
//SET LED3 ON AND /RST pin Low
	*(unsigned char *)0x90080003 = 0x03;
	for (i=0;i<DELAY_2S;i++);
	
	OS_EXIT_CRITICAL();
}



