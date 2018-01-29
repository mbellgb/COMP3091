#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include "net/ip/resolv.h"

#include <string.h>
#include <stdbool.h>

#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"

#define SEND_INTERVAL		15 * CLOCK_SECOND
#define MAX_PAYLOAD_LEN		40
#define UDP_CONNECTION_ADDR fe80:0:0:0:19:f5ff:fe89:1af0
#define SERVER_PORT 9876

#ifndef DEVICE_ID
#define DEVICE_ID -1
#endif

#ifndef PAIR_ID
#define PAIR_ID -1
#endif

static struct uip_udp_conn *client_conn;
PROCESS(udp_client_process, "UDP client process");
AUTOSTART_PROCESSES(&resolv_process,&udp_client_process);

static void tcpip_handler(void)
{
  char *str;

  if(uip_newdata()) {
    str = uip_appdata;
    str[uip_datalen()] = '\0';
    printf("Response from the server: '%s'\n", str);
  }
}

static char buf[MAX_PAYLOAD_LEN];
static void timeout_handler(void)
{
  printf("Client sending to: ");
  PRINT6ADDR(&client_conn->ripaddr);
  sprintf(buf, "{\"command\":\"id\",\"device_id\":%d,\"pair_id\":%d,\"type\":\"detector\"}", DEVICE_ID, PAIR_ID);
  printf(" (msg: %s)\n", buf);
#if SEND_TOO_LARGE_PACKET_TO_TEST_FRAGMENTATION
  uip_udp_packet_send(client_conn, buf, UIP_APPDATA_SIZE);
#else /* SEND_TOO_LARGE_PACKET_TO_TEST_FRAGMENTATION */
  uip_udp_packet_send(client_conn, buf, strlen(buf));
#endif /* SEND_TOO_LARGE_PACKET_TO_TEST_FRAGMENTATION */
}

static void print_local_addresses(void)
{
  int i;
  uint8_t state;

  PRINTF("Client IPv6 addresses: ");
  for(i = 0; i < UIP_DS6_ADDR_NB; i++) {
    state = uip_ds6_if.addr_list[i].state;
    if(uip_ds6_if.addr_list[i].isused &&
       (state == ADDR_TENTATIVE || state == ADDR_PREFERRED)) {
      PRINT6ADDR(&uip_ds6_if.addr_list[i].ipaddr);
      PRINTF("\n");
    }
  }
}
#if UIP_CONF_ROUTER
static void set_global_address(void)
{
  uip_ipaddr_t ipaddr;

  uip_ip6addr(&ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&ipaddr, 0, ADDR_AUTOCONF);
}
#endif /* UIP_CONF_ROUTER */


static resolv_status_t set_connection_address(uip_ipaddr_t *ipaddr)
{
#ifndef UDP_CONNECTION_ADDR
#if RESOLV_CONF_SUPPORTS_MDNS
#define UDP_CONNECTION_ADDR       contiki-udp-server.local
#elif UIP_CONF_ROUTER
#define UDP_CONNECTION_ADDR       aaaa:0:0:0:0212:7404:0004:0404
#else
#define UDP_CONNECTION_ADDR       fe80:0:0:0:9:f5ff:fe89:1af0
#endif
#endif /* !UDP_CONNECTION_ADDR */

#define _QUOTEME(x) #x
#define QUOTEME(x) _QUOTEME(x)

  resolv_status_t status = RESOLV_STATUS_ERROR;

  if(uiplib_ipaddrconv(QUOTEME(UDP_CONNECTION_ADDR), ipaddr) == 0) {
    uip_ipaddr_t *resolved_addr = NULL;
    status = resolv_lookup(QUOTEME(UDP_CONNECTION_ADDR),&resolved_addr);
    if(status == RESOLV_STATUS_UNCACHED || status == RESOLV_STATUS_EXPIRED) {
      PRINTF("Attempting to look up %s\n",QUOTEME(UDP_CONNECTION_ADDR));
      resolv_query(QUOTEME(UDP_CONNECTION_ADDR));
      status = RESOLV_STATUS_RESOLVING;
    } else if(status == RESOLV_STATUS_CACHED && resolved_addr != NULL) {
      PRINTF("Lookup of \"%s\" succeded!\n",QUOTEME(UDP_CONNECTION_ADDR));
    } else if(status == RESOLV_STATUS_RESOLVING) {
      PRINTF("Still looking up \"%s\"...\n",QUOTEME(UDP_CONNECTION_ADDR));
    } else {
      PRINTF("Lookup of \"%s\" failed. status = %d\n",QUOTEME(UDP_CONNECTION_ADDR),status);
    }
    if(resolved_addr)
      uip_ipaddr_copy(ipaddr, resolved_addr);
  } else {
    status = RESOLV_STATUS_CACHED;
  }

  return status;
}

PROCESS_THREAD(udp_client_process, ev, data)
{
  static struct etimer et;
  uip_ipaddr_t ipaddr;

  PROCESS_BEGIN();
  PRINTF("UDP client process started\n");

#if UIP_CONF_ROUTER
  set_global_address();
#endif

  print_local_addresses();

  static resolv_status_t status = RESOLV_STATUS_UNCACHED;
  while(status != RESOLV_STATUS_CACHED) {
    status = set_connection_address(&ipaddr);

    if(status == RESOLV_STATUS_RESOLVING) {
      PROCESS_WAIT_EVENT_UNTIL(ev == resolv_event_found);
    } else if(status != RESOLV_STATUS_CACHED) {
      PRINTF("Can't get connection address.\n");
      PROCESS_YIELD();
    }
  }

  /* new connection with remote host */
  client_conn = udp_new(&ipaddr, UIP_HTONS(SERVER_PORT), NULL);
  udp_bind(client_conn, UIP_HTONS(3001));

  PRINTF("Created a connection with the server ");
  PRINT6ADDR(&client_conn->ripaddr);
  PRINTF(" local/remote port %u/%u\n",
	UIP_HTONS(client_conn->lport), UIP_HTONS(client_conn->rport));

  etimer_set(&et, SEND_INTERVAL);
  while(1) {
    PROCESS_YIELD();
    if(etimer_expired(&et)) {
      timeout_handler();
      etimer_restart(&et);
    } else if(ev == tcpip_event) {
      tcpip_handler();
    }
  }

  PROCESS_END();
}