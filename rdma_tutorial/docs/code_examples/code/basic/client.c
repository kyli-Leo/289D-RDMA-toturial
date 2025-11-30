// gcc client.c -o client -lrdmacm -libverbs
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>

struct Info { uint64_t addr; uint32_t rkey, len; } __attribute__((packed));
static void die(const char *m){ perror(m); exit(1); }

int main(int argc,char**argv){
  if(argc<3){ fprintf(stderr,"Usage: %s <server_ip> <port>\n",argv[0]); return 1; }
  const char* ip=argv[1]; int port=atoi(argv[2]);

  struct rdma_event_channel *ec=rdma_create_event_channel();
  struct rdma_cm_id *id; struct rdma_cm_event *e;
  if(rdma_create_id(ec,&id,NULL,RDMA_PS_TCP)) die("create_id");

  struct addrinfo *res; char ps[16]; snprintf(ps,sizeof(ps),"%d",port);
  if(getaddrinfo(ip,ps,NULL,&res)) die("getaddrinfo");

  if(rdma_resolve_addr(id,NULL,res->ai_addr,2000)) die("resolve_addr");
  if(rdma_get_cm_event(ec,&e)) die("event1"); rdma_ack_cm_event(e);
  if(rdma_resolve_route(id,2000)) die("resolve_route");
  if(rdma_get_cm_event(ec,&e)) die("event2"); rdma_ack_cm_event(e);

  struct ibv_qp_init_attr qa={0};
  qa.qp_type=IBV_QPT_RC; qa.cap.max_send_wr=qa.cap.max_recv_wr=8;
  qa.cap.max_send_sge=qa.cap.max_recv_sge=1; qa.sq_sig_all=1;
  if(rdma_create_qp(id,id->pd,&qa)) die("create_qp");

  struct rdma_conn_param p={0};
  if(rdma_connect(id,&p)) die("connect");

  if(rdma_get_cm_event(ec,&e)) die("event3");
  struct Info info; memcpy(&info,e->param.conn.private_data,sizeof(info));
  rdma_ack_cm_event(e);

  const char* msg="Hello RDMA";
  size_t n=strlen(msg)+1; char *buf=aligned_alloc(4096,n); memcpy(buf,msg,n);
  struct ibv_mr *mr=ibv_reg_mr(id->pd,buf,n,IBV_ACCESS_LOCAL_WRITE);
  if(!mr) die("reg_mr");

  struct ibv_sge s={.addr=(uintptr_t)buf,.length=(uint32_t)n,.lkey=mr->lkey};
  struct ibv_send_wr wr={0},*bad=NULL; wr.opcode=IBV_WR_RDMA_WRITE; wr.sg_list=&s; wr.num_sge=1;
  wr.wr.rdma.remote_addr=info.addr; wr.wr.rdma.rkey=info.rkey;

  struct ibv_wc wc; if(ibv_post_send(id->qp,&wr,&bad)) die("post_send");
  while(ibv_poll_cq(id->qp->send_cq,1,&wc)==0){} if(wc.status) die("wc");
  printf("[client] write done (%zu bytes)\n", n);

  rdma_disconnect(id);
  ibv_dereg_mr(mr); free(buf);
  rdma_destroy_qp(id); rdma_destroy_id(id); rdma_destroy_event_channel(ec);
  freeaddrinfo(res);
  return 0;
}
