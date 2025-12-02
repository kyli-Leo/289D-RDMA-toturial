#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 1
#define QKEY 0x11111111
#define MSG_SIZE 64

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <server_gid> <server_qpn> <qkey>\n", argv[0]);
        return 1;
    }

    // Parse server GID
    union ibv_gid remote_gid;
    sscanf(argv[1],
        "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"
        "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
        &remote_gid.raw[0], &remote_gid.raw[1],
        &remote_gid.raw[2], &remote_gid.raw[3],
        &remote_gid.raw[4], &remote_gid.raw[5],
        &remote_gid.raw[6], &remote_gid.raw[7],
        &remote_gid.raw[8], &remote_gid.raw[9],
        &remote_gid.raw[10], &remote_gid.raw[11],
        &remote_gid.raw[12], &remote_gid.raw[13],
        &remote_gid.raw[14], &remote_gid.raw[15]);

    uint32_t remote_qpn = strtoul(argv[2], NULL, 0);
    uint32_t qkey = strtoul(argv[3], NULL, 0);

    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);

    struct ibv_cq *cq = ibv_create_cq(ctx, 10, NULL, NULL, 0);

    // UD QP
    struct ibv_qp_init_attr qp_init = {
        .send_cq = cq,
        .recv_cq = cq,
        .qp_type = IBV_QPT_UD,
        .cap = {
            .max_send_wr = 10,
            .max_recv_wr = 10,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init);

    // INIT
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = PORT,
        .qkey = QKEY
    };
    ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
        IBV_QP_PORT | IBV_QP_QKEY);

    // RTR
    attr.qp_state = IBV_QPS_RTR;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE);

    // RTS
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN);

    // Create AH (RoCE needs GID + sgid_index)
    struct ibv_ah_attr ah_attr = {
        .is_global = 1,
        .port_num = PORT
    };
    ah_attr.grh.dgid = remote_gid;
    ah_attr.grh.sgid_index = 3;  // RoCE v2 global GID
    ah_attr.grh.hop_limit = 1;

    struct ibv_ah *ah = ibv_create_ah(pd, &ah_attr);

    // Message buffer
    char *msg = "Hello from UD client!";
    struct ibv_mr *mr = ibv_reg_mr(pd, (void*)msg, MSG_SIZE, 0);

    struct ibv_sge sge = {
        .addr = (uintptr_t)msg,
        .length = strlen(msg) + 1,
        .lkey = mr->lkey
    };

    struct ibv_send_wr wr = {
        .wr_id = 1,
        .opcode = IBV_WR_SEND,
        .sg_list = &sge,
        .num_sge = 1,
        .send_flags = IBV_SEND_SIGNALED
    };

    wr.wr.ud.ah = ah;
    wr.wr.ud.remote_qpn = remote_qpn;
    wr.wr.ud.remote_qkey = qkey;

    struct ibv_send_wr *bad_wr;
    ibv_post_send(qp, &wr, &bad_wr);

    printf("[UD Client] Message sent.\n");
    return 0;
}
