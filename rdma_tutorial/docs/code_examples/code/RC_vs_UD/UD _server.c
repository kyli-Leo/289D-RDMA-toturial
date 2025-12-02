#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 1
#define QKEY 0x11111111
#define MSG_SIZE 64
// 修正后的 UD 头部长度：40 字节 (对应 GRH 长度)
#define UD_HEADER_LEN 40 

int main() {
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    struct ibv_pd *pd = ibv_alloc_pd(ctx);

    struct ibv_cq *cq = ibv_create_cq(ctx, 10, NULL, NULL, 0);

    // UD QP 初始化属性
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

    // QP 状态转换: INIT, RTR, RTS 
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = PORT,
        .qkey = QKEY
    };
    ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
        IBV_QP_PORT | IBV_QP_QKEY);
    attr.qp_state = IBV_QPS_RTR;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE);
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = 0;
    ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN);

    // 注册 MR 和 Post RECV
    // 缓冲区大小是 UD_HEADER_LEN (40) + MSG_SIZE (64) = 104 字节
    size_t buffer_len = UD_HEADER_LEN + MSG_SIZE;
    char *buf = malloc(buffer_len);
    struct ibv_mr *mr = ibv_reg_mr(pd, buf, buffer_len,
                                   IBV_ACCESS_LOCAL_WRITE);
    
    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = buffer_len, 
        .lkey = mr->lkey
    };
    struct ibv_recv_wr rwr = {
        .wr_id = 1,
        .sg_list = &sge,
        .num_sge = 1
    };
    struct ibv_recv_wr *bad_rwr;
    ibv_post_recv(qp, &rwr, &bad_rwr);

    // 打印自身信息
    union ibv_gid my_gid;
    ibv_query_gid(ctx, PORT, 3, &my_gid); 

    printf("[UD Server] Ready.\n");
    printf("  QPN  = %u\n", qp->qp_num);
    printf("  QKey = 0x%x\n", QKEY);
    printf("  GID  = %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
        my_gid.raw[0]<<8 | my_gid.raw[1],
        my_gid.raw[2]<<8 | my_gid.raw[3],
        my_gid.raw[4]<<8 | my_gid.raw[5],
        my_gid.raw[6]<<8 | my_gid.raw[7],
        my_gid.raw[8]<<8 | my_gid.raw[9],
        my_gid.raw[10]<<8 | my_gid.raw[11],
        my_gid.raw[12]<<8 | my_gid.raw[13],
        my_gid.raw[14]<<8 | my_gid.raw[15]);

    // 等待 RECV 完成并处理消息
    struct ibv_wc wc;
    while (1) {
        int n = ibv_poll_cq(cq, 1, &wc);
        if (n > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Work Completion Status Error: %s\n", ibv_wc_status_str(wc.status));
                break;
            }

            // 使用 40 字节的头部偏移量
            if (wc.byte_len > UD_HEADER_LEN) {
                size_t payload_len = wc.byte_len - UD_HEADER_LEN; // 62 - 40 = 22
                
                // 将用户数据移动到缓冲区的起始位置 (从 buf + 40 开始)
                memmove(buf, buf + UD_HEADER_LEN, payload_len);

                // 在有效载荷的末尾添加空字符终止符
                buf[payload_len] = '\0';
                
                printf("[UD Server] Received message: %s\n", buf);
            } else {
                 printf("[UD Server] Received message, but payload is too short or empty (Length: %u)\n", wc.byte_len);
            }
            break;
        }
    }

    free(buf);
    return 0;
}