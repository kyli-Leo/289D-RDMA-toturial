#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hip/hip_runtime.h>

struct Info {
  uint64_t addr;
  uint32_t rkey, len;
} __attribute__((packed));

static void die(const char *m) {
  perror(m);
  exit(1);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return 1;
  }
  int port = atoi(argv[1]);

  struct rdma_event_channel *ec = rdma_create_event_channel();
  struct rdma_cm_id *lid, *id;
  struct rdma_cm_event *e;
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);

  if (rdma_create_id(ec, &lid, NULL, RDMA_PS_TCP))
    die("create_id");
  if (rdma_bind_addr(lid, (struct sockaddr *)&a))
    die("bind");
  if (rdma_listen(lid, 1))
    die("listen");
  printf("[server] listening on %d ...\n", port);

  if (rdma_get_cm_event(ec, &e))
    die("get_event");
  id = e->id;
  rdma_ack_cm_event(e);

  struct ibv_qp_init_attr qa = {0};
  qa.qp_type = IBV_QPT_RC;
  qa.cap.max_send_wr = qa.cap.max_recv_wr = 8;
  qa.cap.max_send_sge = qa.cap.max_recv_sge = 1;
  qa.sq_sig_all = 1;
  if (rdma_create_qp(id, id->pd, &qa))
    die("create_qp");

    
  size_t len = 4096;

  char *d_buf = NULL;
  hipError_t herr;

  herr = hipMalloc((void**)&d_buf, len);
  if (herr != hipSuccess) {
    fprintf(stderr, "hipMalloc failed: %s\n", hipGetErrorString(herr));
    return 1;
  }

  herr = hipMemset(d_buf, 0, len);
  if (herr != hipSuccess) {
    fprintf(stderr, "hipMemset failed: %s\n", hipGetErrorString(herr));
    return 1;
  }

  struct ibv_mr *mr = ibv_reg_mr(
      id->pd, d_buf, len, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (!mr)
    die("reg_mr(GPU)");

  struct Info info = {(uint64_t)d_buf, mr->rkey, (uint32_t)len};
  struct rdma_conn_param p = {0};
  p.private_data = &info;
  p.private_data_len = sizeof(info);
  if (rdma_accept(id, &p))
    die("accept");

  if (rdma_get_cm_event(ec, &e))
    die("event2");
  rdma_ack_cm_event(e);

  sleep(2);

  char host_buf[64] = {0};
  herr = hipMemcpy(host_buf, d_buf, sizeof(host_buf), hipMemcpyDeviceToHost);
  if (herr != hipSuccess) {
    fprintf(stderr, "hipMemcpy D2H failed: %s\n", hipGetErrorString(herr));
    return 1;
  }

  printf("[server] got: '%.*s'\n", 64, host_buf);

  rdma_disconnect(id);
  ibv_dereg_mr(mr);
  hipFree(d_buf);
  rdma_destroy_qp(id);
  rdma_destroy_id(id);
  rdma_destroy_id(lid);
  rdma_destroy_event_channel(ec);
  return 0;
}
