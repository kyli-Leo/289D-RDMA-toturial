// rdma_client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <sys/time.h>
#include <inttypes.h> 
#include <netdb.h> 
#include <time.h>
#include "rdma_common.h" 

// ---------------------- 配置 ----------------------
#define RDMA_Q_TYPE IBV_QPT_RC // <--- 请在此处切换 RC/UC
#define CLIENT_WINDOW 64 

// ---------------------- 全局变量 ----------------------
static struct context *s_ctx = NULL;
static struct connection *s_conn = NULL;
static struct remote_mr_info s_remote_mr_info; 

static void build_context(struct ibv_context *ibv_ctx) {
    if (s_ctx) return;
    s_ctx = (struct context *)malloc(sizeof(struct context));
    if (!s_ctx) die("malloc s_ctx failed");

    s_ctx->ctx = ibv_ctx;
    s_ctx->pd = ibv_alloc_pd(s_ctx->ctx);
    if (!s_ctx->pd) die("ibv_alloc_pd failed");
    s_ctx->cq = ibv_create_cq(s_ctx->ctx, CLIENT_WINDOW + 32, NULL, NULL, 0); 
    if (!s_ctx->cq) die("ibv_create_cq failed");
}

static void build_qp(struct rdma_cm_id *id) {
    struct ibv_qp_init_attr qp_attr = {
        .qp_context = id,
        .send_cq = s_ctx->cq,
        .recv_cq = s_ctx->cq,
        .cap = {
            .max_send_wr = (uint32_t)CLIENT_WINDOW + 32,
            .max_recv_wr = 4, 
            .max_send_sge = 1,
            .max_recv_sge = 1,
        },
        .qp_type = RDMA_Q_TYPE, 
        .sq_sig_all = 0,
    };
    
    if (rdma_create_qp(id, s_ctx->pd, &qp_attr)) die("rdma_create_qp failed");
}

static void run_performance_test(struct connection *conn) {
    enum ibv_wr_opcode opcode = IBV_WR_RDMA_WRITE; 
    const char *op_str = "RDMA WRITE";
    
    printf("Starting %s test (MsgSize=%d, Iters=%" PRIu64 ", Window=%" PRIu64 ", Type: %s)...\n",
           op_str, MESSAGE_SIZE, (uint64_t)NUM_TRANSFERS, (uint64_t)CLIENT_WINDOW, 
           RDMA_Q_TYPE == IBV_QPT_RC ? "RC" : "UC");
    
    // 【强制刷新】确保以上信息一定能被打印出来
    fflush(stdout);

    // =========================================================
    // 【新增调试信息和鲁棒性检查】
    printf("---------------- DEBUG INFO START -------------------\n");
    if (!conn || !conn->qp || !conn->mr || !s_ctx || !s_ctx->cq) {
        // 任何一个关键 RDMA 资源指针为空都应该立即报错
        fprintf(stderr, "FATAL ERROR: Critical RDMA resource is NULL!\n");
        fprintf(stderr, "conn=%p, conn->qp=%p, conn->mr=%p, s_ctx->cq=%p\n", 
                (void*)conn, (void*)(conn ? conn->qp : NULL), (void*)(conn ? conn->mr : NULL), (void*)(s_ctx ? s_ctx->cq : NULL));
        return; 
    }

    printf("DEBUG: QP Pointer: %p\n", (void*)conn->qp);
    printf("DEBUG: Client MR (Local) Addr/LKey: 0x%" PRIx64 "/0x%x\n", 
           (uint64_t)conn->mr->addr, conn->mr->lkey);
    printf("DEBUG: Server MR (Remote) Addr/RKey: 0x%" PRIx64 "/0x%x (Len: %u)\n", 
           s_remote_mr_info.addr, s_remote_mr_info.rkey, s_remote_mr_info.len);
    printf("---------------- DEBUG INFO END ---------------------\n");
    fflush(stdout); // 确保调试信息也被打印

    if (s_remote_mr_info.len < MESSAGE_SIZE) {
        fprintf(stderr, "Server buffer size (%u) is smaller than client message size (%d).\n",
                s_remote_mr_info.len, MESSAGE_SIZE);
        return;
    }

    // =========================================================

    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    
    uint64_t posted = 0;
    uint64_t done = 0;
    struct ibv_wc wc[32];

    while (done < NUM_TRANSFERS) {
        while (posted - done < CLIENT_WINDOW && posted < NUM_TRANSFERS) {
            struct ibv_sge s = {
                .addr = (uintptr_t)conn->mr->addr, 
                .length = (uint32_t)conn->mr->length, 
                .lkey = conn->mr->lkey
            };
            
            struct ibv_send_wr wr = {0}, *bad = NULL;
            wr.wr_id = posted;
            wr.sg_list = &s;
            wr.num_sge = 1;
            wr.send_flags = IBV_SEND_SIGNALED;
            wr.opcode = opcode;

            wr.wr.rdma.remote_addr = s_remote_mr_info.addr;
            wr.wr.rdma.rkey = s_remote_mr_info.rkey;
            
            if (ibv_post_send(conn->qp, &wr, &bad)) die("post_send failed");
            posted++;
        }

        int n = ibv_poll_cq(s_ctx->cq, 32, wc);
        if (n < 0) die("poll_cq failed");

        for (int i = 0; i < n; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "RDMA error: status=%d (%s), vendor_err=0x%x\n", 
                        wc[i].status, ibv_wc_status_str(wc[i].status), wc[i].vendor_err);
                die("WC failed");
            }
            done++;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &ts1);

    double sec = (ts1.tv_sec - ts0.tv_sec) + (ts1.tv_nsec - ts0.tv_nsec) / 1e9;
    double mops = NUM_TRANSFERS / sec / 1e6;
    uint64_t total_bytes = (uint64_t)NUM_TRANSFERS * MESSAGE_SIZE;
    double bw = (double)total_bytes / sec / (1024.0 * 1024.0 * 1024.0);
    
    printf("------------------------------------------------------------------\n");
    printf("Completed %s Test:\n", op_str);
    printf("Total Transfers: %" PRIu64 "\n", (uint64_t)NUM_TRANSFERS); 
    printf("Total Data Transferred (Bytes): %" PRIu64 "\n", total_bytes); 
    printf("Total Time (s): %.4f\n", sec);
    printf("Throughput: %.2f Mops (Million Operations per Second)\n", mops);
    printf("Bandwidth: %.2f GiB/s\n", bw);
    printf("------------------------------------------------------------------\n");
}

static void clean_up(struct rdma_cm_id *id) {
    if (s_conn) {
        if (s_conn->mr) ibv_dereg_mr(s_conn->mr);
        if (s_conn->buffer) free(s_conn->buffer);
        free(s_conn);
        s_conn = NULL;
    }
    if (id && id->qp) rdma_destroy_qp(id);
    if (id) rdma_destroy_id(id);

    if (s_ctx) {
        if (s_ctx->cq) ibv_destroy_cq(s_ctx->cq);
        if (s_ctx->pd) ibv_dealloc_pd(s_ctx->pd);
        free(s_ctx);
        s_ctx = NULL;
    }
}


static int on_event(struct rdma_cm_event *event) {
    int ret = 0;
    switch (event->event) {
        case RDMA_CM_EVENT_ADDR_RESOLVED:
            printf("RDMA_CM_EVENT_ADDR_RESOLVED received. Resolving route...\n");
            if (rdma_resolve_route(event->id, 2000)) die("rdma_resolve_route failed");
            break;
        case RDMA_CM_EVENT_ROUTE_RESOLVED:
            printf("RDMA_CM_EVENT_ROUTE_RESOLVED received. Creating QP and connecting...\n");
            build_context(event->id->verbs);
            build_qp(event->id);

	    // 【修复点】：将新创建的 QP 赋值给全局连接结构体
            s_conn->qp = event->id->qp; // <--- 关键修复
            
            if (posix_memalign((void **)&s_conn->buffer, 4096, MESSAGE_SIZE)) die("posix_memalign failed");
            memset(s_conn->buffer, 0xab, MESSAGE_SIZE);
            
            int access = IBV_ACCESS_LOCAL_WRITE;
            s_conn->mr = ibv_reg_mr(s_ctx->pd, s_conn->buffer, MESSAGE_SIZE, access);
            if (!s_conn->mr) die("ibv_reg_mr failed");
            
            struct rdma_conn_param p = {0};
            p.initiator_depth = 16;
            p.responder_resources = 16;

            if (rdma_connect(event->id, &p)) die("rdma_connect failed");
            break;
        case RDMA_CM_EVENT_ESTABLISHED:
            printf("RDMA_CM_EVENT_ESTABLISHED! Connection successful.\n");
            
            if (event->param.conn.private_data && 
                event->param.conn.private_data_len >= sizeof(s_remote_mr_info)) {
                
                memcpy(&s_remote_mr_info, 
                       event->param.conn.private_data, 
                       sizeof(s_remote_mr_info));
                
                run_performance_test(s_conn); 
            } else {
                fprintf(stderr, "Error: Failed to receive remote MR info from Server. Cannot run RDMA test.\n");
            }
            
            if (rdma_disconnect(event->id)) die("rdma_disconnect failed");
            break;
        case RDMA_CM_EVENT_DISCONNECTED:
            printf("RDMA_CM_EVENT_DISCONNECTED received. Cleaning up.\n");
            ret = 1; 
            break;
        default:
            fprintf(stderr, "Unknown CM event: %s\n", rdma_event_str(event->event));
            break;
    }
    return ret;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *server_ip = argv[1];

    struct rdma_event_channel *ec = rdma_create_event_channel();
    if (!ec) die("rdma_create_event_channel failed");

    struct rdma_cm_id *id = NULL;
    struct rdma_cm_event *event = NULL;
    
    if (rdma_create_id(ec, &id, NULL, RDMA_PS_TCP)) die("rdma_create_id failed");

    s_conn = (struct connection *)malloc(sizeof(struct connection));
    if (!s_conn) die("malloc s_conn failed");
    s_conn->id = id;
    s_conn->qp = NULL; // 初始化为 NULL
    s_conn->mr = NULL; 
    s_conn->buffer = NULL;
    id->context = s_conn;
    
    struct addrinfo *res;
    char ps[16];
    snprintf(ps, sizeof(ps), "%s", DEFAULT_PORT);
    
    if (getaddrinfo(server_ip, ps, NULL, &res)) die("getaddrinfo failed");

    if (rdma_resolve_addr(id, NULL, res->ai_addr, 5000)) die("rdma_resolve_addr failed");
    
    printf("Attempting to connect to %s:%s (System selects local IP, Type: %s)...\n", 
           server_ip, DEFAULT_PORT,
           RDMA_Q_TYPE == IBV_QPT_RC ? "RC" : "UC");
           
    freeaddrinfo(res); 

    while (rdma_get_cm_event(ec, &event) == 0) {
        struct rdma_cm_event event_copy = *event;
        rdma_ack_cm_event(event);

        if (on_event(&event_copy)) {
             break;
        }
    }
    
    clean_up(id);
    rdma_destroy_event_channel(ec);
    
    printf("Client finished. Exiting.\n");
    return 0;
}