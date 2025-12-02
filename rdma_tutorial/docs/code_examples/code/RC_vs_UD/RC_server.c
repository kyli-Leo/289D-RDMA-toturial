// rdma_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include "rdma_common.h" // 包含 die 的声明

// ---------------------- 配置 ----------------------
// 切换连接类型: IBV_QPT_RC (RC) 或 IBV_QPT_UC (UC)
#define RDMA_Q_TYPE IBV_QPT_RC // <--- 请在此处切换 RC/UC

static struct context *s_ctx = NULL;
static struct rdma_cm_id *listener = NULL; 

// 【已删除：void die(const char *reason) 的实现】

static void build_context(struct ibv_context *ibv_ctx) {
    if (s_ctx) return;
    s_ctx = (struct context *)malloc(sizeof(struct context));
    if (!s_ctx) die("malloc s_ctx failed");

    s_ctx->ctx = ibv_ctx;
    s_ctx->pd = ibv_alloc_pd(s_ctx->ctx);
    if (!s_ctx->pd) die("ibv_alloc_pd failed");
    s_ctx->cq = ibv_create_cq(s_ctx->ctx, 16, NULL, NULL, 0); 
    if (!s_ctx->cq) die("ibv_create_cq failed");
}

static void build_qp(struct rdma_cm_id *id) {
    struct ibv_qp_init_attr qp_attr = {
        .qp_context = id,
        .send_cq = s_ctx->cq,
        .recv_cq = s_ctx->cq,
        .cap = {
            .max_send_wr = 16, 
            .max_recv_wr = 1,
            .max_send_sge = 1,
            .max_recv_sge = 1,
        },
        .qp_type = RDMA_Q_TYPE, 
        .sq_sig_all = 0,
    };
    
    if (rdma_create_qp(id, s_ctx->pd, &qp_attr)) die("rdma_create_qp failed");
}

static int on_connection_request(struct rdma_cm_id *id) {
    printf("Client connected! Accepting connection (Type: %s)...\n", 
           RDMA_Q_TYPE == IBV_QPT_RC ? "RC" : "UC");

    build_context(id->verbs);

    struct connection *conn = (struct connection *)malloc(sizeof(struct connection));
    if (!conn) die("malloc connection failed");
    conn->id = id;
    id->context = conn;

    build_qp(id);
    conn->qp = id->qp;
    
    if (posix_memalign((void **)&conn->buffer, 4096, MESSAGE_SIZE)) die("posix_memalign failed");
    memset(conn->buffer, 0, MESSAGE_SIZE);
    
    int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ; 
    conn->mr = ibv_reg_mr(s_ctx->pd, conn->buffer, MESSAGE_SIZE, access);
    if (!conn->mr) die("ibv_reg_mr failed");

    struct remote_mr_info mr_info = {
        .addr = (uintptr_t)conn->buffer,
        .rkey = conn->mr->rkey,
        .len = MESSAGE_SIZE 
    };
    
    struct rdma_conn_param cm_params = {0};
    cm_params.private_data = &mr_info;
    cm_params.private_data_len = sizeof(mr_info);
    
    cm_params.responder_resources = 16;
    cm_params.initiator_depth = 16;
    
    if (rdma_accept(id, &cm_params)) die("rdma_accept failed");
    return 0;
}

static void on_disconnect(struct rdma_cm_id *id) {
    struct connection *conn = (struct connection *)id->context;
    
    printf("RDMA_CM_EVENT_DISCONNECTED received. Cleaning up.\n");
    
    if (conn) {
        if (conn->mr) ibv_dereg_mr(conn->mr);
        if (conn->buffer) free(conn->buffer);
        free(conn);
    }
    if (id->qp) rdma_destroy_qp(id);
    rdma_destroy_id(id);
}

static int on_event(struct rdma_cm_event *event) {
    int ret = 0;
    switch (event->event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            on_connection_request(event->id);
            break;
        case RDMA_CM_EVENT_ESTABLISHED:
            printf("Connection established. Waiting for client RDMA operation...\n");
            break; 
        case RDMA_CM_EVENT_DISCONNECTED:
            on_disconnect(event->id);
            ret = 1; 
            break;
        default:
            fprintf(stderr, "Unhandled event: %s\n", rdma_event_str(event->event));
            break;
    }
    return ret;
}

int main(int argc, char **argv) {
    struct rdma_event_channel *ec = rdma_create_event_channel();
    if (!ec) die("rdma_create_event_channel failed");

    if (rdma_create_id(ec, &listener, NULL, RDMA_PS_TCP)) die("rdma_create_id failed");

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(atoi(DEFAULT_PORT));

    if (rdma_bind_addr(listener, (struct sockaddr *)&a)) die("rdma_bind_addr failed");
    if (rdma_listen(listener, 1)) die("rdma_listen failed");
    
    printf("Starting RDMA Server on port %s (Type: %s)...\n", 
           DEFAULT_PORT, RDMA_Q_TYPE == IBV_QPT_RC ? "RC" : "UC");
    printf("RDMA Server listening...\n");

    struct rdma_cm_event *event = NULL;
    while (rdma_get_cm_event(ec, &event) == 0) {
        struct rdma_cm_event event_copy = *event;
        rdma_ack_cm_event(event);

        if (on_event(&event_copy)) {
             break;
        }
    }
    
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(ec);
    
    if (s_ctx) {
        if (s_ctx->cq) ibv_destroy_cq(s_ctx->cq);
        if (s_ctx->pd) ibv_dealloc_pd(s_ctx->pd);
        free(s_ctx);
    }
    
    printf("Server finished. Exiting.\n");
    return 0;
}