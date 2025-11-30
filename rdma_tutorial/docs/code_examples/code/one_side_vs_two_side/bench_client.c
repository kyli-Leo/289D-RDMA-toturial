// gcc bench_client.c -o bench_client -lrdmacm -libverbs
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
          "Usage: %s <server_ip> <port> [--mode read|send] [--msg N] "
          "[--iters N] [--window N]\n",
          p);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    usage(argv[0]);
    return 1;
  }

  const char *ip = argv[1];
  int port = atoi(argv[2]);
  enum Mode mode = MODE_READ;
  size_t msg = 4096;
  uint64_t iters = 100000;
  uint64_t window = 64;

  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
      mode = !strcmp(argv[i + 1], "send") ? MODE_SEND : MODE_READ;
      i++;
    } else if (!strcmp(argv[i], "--msg") && i + 1 < argc) {
      msg = strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--iters") && i + 1 < argc) {
      iters = strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
      window = strtoull(argv[++i], NULL, 0);
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  struct rdma_event_channel *ec = rdma_create_event_channel();
  struct rdma_cm_id *id;
  struct rdma_cm_event *e;
  if (rdma_create_id(ec, &id, NULL, RDMA_PS_TCP))
    die("create_id");

  struct addrinfo *res;
  char ps[16];
  snprintf(ps, sizeof(ps), "%d", port);
  if (getaddrinfo(ip, ps, NULL, &res))
    die("getaddrinfo");

  if (rdma_resolve_addr(id, NULL, res->ai_addr, 2000))
    die("resolve_addr");
  if (rdma_get_cm_event(ec, &e))
    die("event1");
  rdma_ack_cm_event(e);
  if (rdma_resolve_route(id, 2000))
    die("resolve_route");
  if (rdma_get_cm_event(ec, &e))
    die("event2");
  rdma_ack_cm_event(e);

  struct ibv_qp_init_attr qa = {0};
  qa.qp_type = IBV_QPT_RC;
  qa.cap.max_send_wr = (uint32_t)(window + 32);
  qa.cap.max_recv_wr = 4;
  qa.cap.max_send_sge = qa.cap.max_recv_sge = 1;
  qa.sq_sig_all = 0;
  if (rdma_create_qp(id, id->pd, &qa))
    die("create_qp");

  struct rdma_conn_param p = {0};
  if (rdma_connect(id, &p))
    die("connect");

  if (rdma_get_cm_event(ec, &e))
    die("event3");
  struct Info info;
  memcpy(&info, e->param.conn.private_data, sizeof(info));
  rdma_ack_cm_event(e);
  if (info.len < msg) {
    fprintf(stderr, "server buffer too small (%u < %zu)\n", info.len, msg);
    return 1;
  }

  char *buf = NULL;
  if (posix_memalign((void **)&buf, 4096, msg))
    die("alloc");
  memset(buf, 0xab, msg);

  int access = IBV_ACCESS_LOCAL_WRITE;
  struct ibv_mr *mr = ibv_reg_mr(id->pd, buf, msg, access);
  if (!mr)
    die("reg_mr");

  uint64_t posted = 0, done = 0;
  struct ibv_wc wc[32];
  struct timespec ts0, ts1;
  clock_gettime(CLOCK_MONOTONIC, &ts0);

  while (done < iters) {
    while (posted - done < window && posted < iters) {
      struct ibv_sge s = {.addr = (uintptr_t)buf,
                          .length = (uint32_t)msg,
                          .lkey = mr->lkey};
      struct ibv_send_wr wr = {0}, *bad = NULL;
      wr.wr_id = posted;
      wr.sg_list = &s;
      wr.num_sge = 1;
      wr.send_flags = IBV_SEND_SIGNALED;

      if (mode == MODE_READ) {
        wr.opcode = IBV_WR_RDMA_READ;
        wr.wr.rdma.remote_addr = info.addr;
        wr.wr.rdma.rkey = info.rkey;
      } else {
        wr.opcode = IBV_WR_SEND;
      }

      if (ibv_post_send(id->qp, &wr, &bad))
        die("post_send");
      posted++;
    }

    int n = ibv_poll_cq(id->send_cq, 32, wc);
    if (n < 0)
      die("poll_cq");
    for (int i = 0; i < n; ++i) {
      if (wc[i].status)
        die("wc");
      done++;
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &ts1);
  double sec = (ts1.tv_sec - ts0.tv_sec) +
               (ts1.tv_nsec - ts0.tv_nsec) / 1e9;
  double mops = iters / sec / 1e6;
  double bw = (iters * msg) / sec / (1024.0 * 1024.0 * 1024.0);
  printf("[client] %s done: %.2f Mops, %.2f GiB/s (msg=%zu bytes, window=%lu)\n",
         mode == MODE_READ ? "read" : "send", mops, bw, msg,
         (unsigned long)window);

  rdma_disconnect(id);
  ibv_dereg_mr(mr);
  free(buf);
  rdma_destroy_qp(id);
  rdma_destroy_id(id);
  rdma_destroy_event_channel(ec);
  freeaddrinfo(res);
  return 0;
}
