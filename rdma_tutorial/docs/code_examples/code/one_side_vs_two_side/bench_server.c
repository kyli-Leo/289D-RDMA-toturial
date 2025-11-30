// gcc bench_server.c -o bench_server -lrdmacm -libverbs
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct Info {
  uint64_t addr;
  uint32_t rkey, len;
} __attribute__((packed));

enum Mode { MODE_READ, MODE_SEND };

static void die(const char *m) {
  perror(m);
  exit(1);
}

static void usage(const char *p) {
  fprintf(stderr,
          "Usage: %s <port> [--mode read|send] [--msg N] [--iters N] "
          "[--recv-depth N]\n",
          p);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  enum Mode mode = MODE_READ;
  size_t msg = 4096;
  uint64_t iters = 100000;
  int recv_depth = 128;
  int port = atoi(argv[1]);

  for (int i = 2; i < argc; ++i) {
    if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
      mode = !strcmp(argv[i + 1], "send") ? MODE_SEND : MODE_READ;
      i++;
    } else if (!strcmp(argv[i], "--msg") && i + 1 < argc) {
      msg = strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--iters") && i + 1 < argc) {
      iters = strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--recv-depth") && i + 1 < argc) {
      recv_depth = atoi(argv[++i]);
    } else {
      usage(argv[0]);
      return 1;
    }
  }

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
  printf("[server] listening on %d (mode=%s msg=%zu iters=%lu)\n", port,
         mode == MODE_READ ? "read" : "send", msg, (unsigned long)iters);

  if (rdma_get_cm_event(ec, &e))
    die("get_event");
  id = e->id;
  rdma_ack_cm_event(e);

  struct ibv_qp_init_attr qa = {0};
  qa.qp_type = IBV_QPT_RC;
  qa.cap.max_send_wr = recv_depth + 16;
  qa.cap.max_recv_wr = recv_depth + 16;
  qa.cap.max_send_sge = qa.cap.max_recv_sge = 1;
  qa.sq_sig_all = 0;
  if (rdma_create_qp(id, id->pd, &qa))
    die("create_qp");

  size_t buf_len = msg * recv_depth;
  char *buf = NULL;
  if (posix_memalign((void **)&buf, 4096, buf_len))
    die("alloc");
  memset(buf, 0, buf_len);

  int access = IBV_ACCESS_LOCAL_WRITE;
  if (mode == MODE_READ)
    access |= IBV_ACCESS_REMOTE_READ;
  struct ibv_mr *mr = ibv_reg_mr(id->pd, buf, buf_len, access);
  if (!mr)
    die("reg_mr");

  struct Info info = {(uint64_t)buf, mr->rkey, (uint32_t)msg};
  struct rdma_conn_param p = {0};
  p.private_data = &info;
  p.private_data_len = sizeof(info);
  if (rdma_accept(id, &p))
    die("accept");

  if (rdma_get_cm_event(ec, &e))
    die("event2");
  rdma_ack_cm_event(e);

  if (mode == MODE_SEND) {
    struct ibv_recv_wr *bad;
    for (int i = 0; i < recv_depth; ++i) {
      struct ibv_sge s = {.addr = (uintptr_t)(buf + i * msg),
                          .length = (uint32_t)msg,
                          .lkey = mr->lkey};
      struct ibv_recv_wr wr = {.wr_id = (uint64_t)i,
                               .sg_list = &s,
                               .num_sge = 1};
      if (ibv_post_recv(id->qp, &wr, &bad))
        die("post_recv");
    }

    uint64_t done = 0;
    struct ibv_wc wc[32];
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    while (done < iters) {
      int n = ibv_poll_cq(id->recv_cq, 32, wc);
      if (n < 0)
        die("poll_cq");
      for (int i = 0; i < n; ++i) {
        if (wc[i].status)
          die("wc");
        done++;
        int slot = (int)wc[i].wr_id;
        struct ibv_sge s = {.addr = (uintptr_t)(buf + slot * msg),
                            .length = (uint32_t)msg,
                            .lkey = mr->lkey};
        struct ibv_recv_wr wr = {.wr_id = (uint64_t)slot,
                                 .sg_list = &s,
                                 .num_sge = 1};
        if (ibv_post_recv(id->qp, &wr, &bad))
          die("repost_recv");
      }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    double sec = (ts1.tv_sec - ts0.tv_sec) +
                 (ts1.tv_nsec - ts0.tv_nsec) / 1e9;
    double mops = iters / sec / 1e6;
    double bw = (iters * msg) / sec / (1024.0 * 1024.0 * 1024.0);
    printf("[server] recv done: %.2f Mops, %.2f GiB/s\n", mops, bw);
  } else {
    printf("[server] ready for client RDMA READ, waiting for disconnect...\n");
    if (rdma_get_cm_event(ec, &e))
      die("wait_disconnect");
    if (e->event != RDMA_CM_EVENT_DISCONNECTED)
      fprintf(stderr, "unexpected event %d\n", e->event);
    rdma_ack_cm_event(e);
  }

  rdma_disconnect(id);
  ibv_dereg_mr(mr);
  free(buf);
  rdma_destroy_qp(id);
  rdma_destroy_id(id);
  rdma_destroy_id(lid);
  rdma_destroy_event_channel(ec);
  return 0;
}
