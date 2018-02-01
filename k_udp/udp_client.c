#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>
#include "kernel_udp.h"

//############## CLIENT IP AND PORT (this module) #######
static unsigned char ipmy[5] = {127,0,0,1,'\0'};
static unsigned int myip[5];
static int margs;
module_param_array(myip, int, &margs, S_IRUGO);
MODULE_PARM_DESC(myip,"The client ip, default 127.0.0.1");

static int myport = 0; //random port
module_param(myport, int, S_IRUGO);
MODULE_PARM_DESC(myport,"The receiving port, default is a free random one chosen by system");
//######################################################

//############## SERVER IP AND PORT (udp_server) #######
static unsigned char serverip[5] = {127,0,0,4,'\0'};
static unsigned int destip[5];
static int sargs;
module_param_array(destip, int, &sargs, S_IRUGO);
MODULE_PARM_DESC(destip,"The server ip, default 127.0.0.1");

static int destport = 3000;
module_param(destport, int, S_IRUGO);
MODULE_PARM_DESC(destport,"The server port, default 3000");
//######################################################

static udp_service * udp_client;
static struct socket * udpc_socket;

#if THROUGHPUT_TEST
atomic_t sent_pkt;
struct timer_list timer;
struct timeval interval;
unsigned long long total_packets;

void count_sec(unsigned long a){
  printk(KERN_INFO "%s\t \t \t Sent %d/sec", udp_client->name, atomic_read(&sent_pkt));
  atomic_set(&sent_pkt, 0);
  mod_timer(&timer, jiffies + timeval_to_jiffies(&interval));
}
#endif

int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *client_socket = udpc_socket;


  unsigned char * in_buf = kmalloc(MAX_UDP_SIZE, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(strlen(HELLO)+1, GFP_KERNEL);

  address.sin_addr.s_addr = htonl(create_address(serverip));
  address.sin_family = AF_INET;
  address.sin_port = htons(destport);

  int res =  kernel_connect(client_socket, (struct sockaddr *)&address, sizeof(struct sockaddr),
		   0);
  if(res < 0){
    printk(KERN_INFO "Error %d", -res);
    check_sock_allocation(udp_client, client_socket);
    atomic_set(&udp_client->thread_running, 0);
    kfree(in_buf);
    kfree(out_buf);
    return 0;
  }

  memset(out_buf, '\0', strlen(HELLO)+1);
  memcpy(out_buf, HELLO, strlen(HELLO));

  #if LATENCY_TEST
    long long total = 0, counted = 0;
    long average = 0;
    struct timeval departure_time, arrival_time;
    do_gettimeofday(&departure_time);
  #endif

  #if THROUGHPUT_TEST
    atomic_set(&sent_pkt, 0);
    total_packets=0;
    setup_timer( &timer,  count_sec, 0);
    interval = (struct timeval) {1,0};
    mod_timer(&timer, jiffies + timeval_to_jiffies(&interval));
    printk("%s Performing a speed test: this module will count how many packets \
    it sends", udp_client->name);
  #endif

  #if PRINT_MESS
    udp_server_send(client_socket, &address, out_buf, strlen(out_buf)+1, MSG_WAITALL, udp_client->name);
  #endif

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      check_sock_allocation(udp_client, client_socket);
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }

    #if THROUGHPUT_TEST || LATENCY_TEST
    udp_server_send(client_socket, &address, out_buf, strlen(out_buf)+1, MSG_WAITALL, udp_client->name);
      #if THROUGHPUT_TEST
        atomic_inc(&sent_pkt);
        total_packets++;
      #endif
    #endif

    #if PRINT_MESS || LATENCY_TEST
    int ret = udp_server_receive(client_socket, &address, in_buf, MSG_WAITALL, udp_client);
    if(ret > 0 && memcmp(in_buf, OK, strlen(OK)) == 0){
      #if PRINT_MESS
        printk(KERN_INFO "%s Got OK", udp_client->name);
        printk(KERN_INFO "%s All done, terminating client", udp_client->name);
        check_sock_allocation(udp_client, client_socket);
        kfree(in_buf);
        kfree(out_buf);
        break;
      #else
      do_gettimeofday(&arrival_time);
      long res = (arrival_time.tv_sec * 1000000 + arrival_time.tv_usec) - (departure_time.tv_sec * 1000000 + departure_time.tv_usec );
      total += res;
      counted++;
      average = total/ counted;
      printk(KERN_INFO "Latency: %ld microseconds, average %ld", res, average);
      do_gettimeofday(&departure_time);
      #endif
    }
    #endif


  }

  return 0;
}

int client_listen(void)
{
  udp_server_init(udp_client, &udpc_socket, ipmy, myport);
  if(atomic_read(&udp_client->thread_running) == 1){
    connection_handler(NULL);
    atomic_set(&udp_client->thread_running, 0);
  }
  return 0;
}

void client_start(void){
  udp_client->u_thread = kthread_run((void *)client_listen, NULL, udp_client->name);
  if(udp_client->u_thread >= 0){
    atomic_set(&udp_client->thread_running,1);
    printk(KERN_INFO "%s Thread running", udp_client->name);
  }else{
    printk(KERN_INFO "%s Error in starting thread. Terminated", udp_client->name);
  }
}

static int __init client_init(void)
{
  udp_client = kmalloc(sizeof(udp_service), GFP_KERNEL);
  if(!udp_client){
    printk(KERN_INFO "Failed to initialize CLIENT");
  }else{
    check_params(ipmy, myip, margs);
    check_params(serverip, destip, sargs);
    init_service(udp_client, "Client:");
    client_start();
  }
  return 0;
}

static void __exit client_exit(void)
{
  #if THROUGHPUT_TEST
  del_timer(&timer);
  printk(KERN_INFO "%s Sent total of %llu packets",udp_client->name, total_packets);
  #endif
  udp_server_quit(udp_client, udpc_socket);
}

module_init(client_init)
module_exit(client_exit)
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");
