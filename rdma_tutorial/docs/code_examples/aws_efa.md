# EFA RDMA Write Example


> **Notice:**  
> Because our current devices do not support EFA, we could not execute the RDMA test.  
> Here is the functional description of the code:  
> [efa_rdma_write.cc](https://github.com/uccl-project/uccl/blob/main/misc/efa_rdma_write.cc)



This page examplain the code of [efa_rdma_write.cc](https://github.com/uccl-project/uccl/blob/main/misc/efa_rdma_write.cc) about how to build a simple **RDMA Write with Immediate** pipeline using **Amazon EFA** and its **SRD QP** capability. 

The code example of [efa_rdma_write.cc](https://github.com/uccl-project/uccl/blob/main/misc/efa_rdma_write.cc) shows:

* RDMA resource initialization (PD, CQ, MR, QP)
* Exchanging QPN/GID/rkey/addr over TCP
* Creating an EFA SRD QP that supports RDMA write/read
* Using UD-style addressing (AH + QPN + QKEY)
* Client sending two RDMA writes to the server
* Server receiving the writes through extended CQ polling

---

## 1. Overview

The implementation follows a **client–server model**:

### **Server**

* Registers a memory region
* Posts two receive buffers
* Waits for two `RDMA_WRITE_WITH_IMM` completions
* Prints the received message

### **Client**

* Exchanges metadata with the server  
* Creates an Address Handle (AH) with server’s GID  
* Performs two RDMA writes:  
    - one large message  
    - one small message  
* Polls CQ for completion

---
## 2. RDMA Context

All RDMA resources are collected in a lightweight context struct:
```c
struct rdma_context {
    struct ibv_context* ctx;
    struct ibv_pd* pd;
    struct ibv_cq_ex* cq_ex;
    struct ibv_qp* qp;
    struct ibv_mr* mr;
    struct ibv_ah* ah;
    char* local_buf;
    uint32_t remote_rkey;
    uint64_t remote_addr;
};
```

`init_rdma()` performs:

* Selecting and opening an RDMA device

* Allocating a protection domain

* Creating an extended CQ (`ibv_cq_ex`)

* Allocating and registering memory regions

* Creating an SRD QP

---

## 3. Metadata Exchange

Before RDMA operations begin, both peers must know:

* **QPN** (Queue Pair Number)
* **GID** (Global Identifier)
* **rkey** (remote key of the MR)
* **addr** (virtual address of the remote buffer)

A lightweight TCP connection (`exchange_qpns()`) is used to exchange this metadata:

* Server waits for client’s connection
* Client retries `connect` until server is ready
* Both sides send their metadata
* TCP closes immediately after exchange

```c
struct metadata {
    uint32_t qpn;
    union ibv_gid gid;
    uint32_t rkey;
    uint64_t addr;
};
```

---

## 4. SRD QP on EFA

The program uses an **EFA SRD QP**, which has two key properties:

### **UD-style addressing**

* Requires Address Handle (AH)
* Requires QPN and QKEY
* Uses GRH with remote GID

### **RC-style semantics**

* Supports RDMA Write
* RDMA Write With Immediate
* RDDA Read

This hybrid design is unique to EFA and enables reliable, ordered data transfer with UD routing.

---

## 5. Address Handle (AH)

After exchanging metadata, each side constructs an AH using the remote GID.
Later, each work request attaches:

* AH
* remote QPN
* QKEY

This is how an SRD QP performs routing.

---

## 6. Completion Polling (CQE)

The example uses the **extended CQ API (`ibv_cq_ex`)**, which provides:

* opcode
* byte length
* work request ID
* immediate data

The server polls for:

```
IBV_WC_RECV_RDMA_WITH_IMM
```

The client polls for:

```
IBV_WC_RDMA_WRITE
```

---

## 7. Server Workflow

**`run_server()` performs:**

1. Fill local metadata
2. Exchange metadata via TCP
3. Create AH with client’s GID
4. Initialize receive buffer
5. Post two receive WQEs
6. Wait for two RDMA writes from client
7. Print the received buffer

Example receive posting (simplified):
```c
ibv_recv_wr wr = { .num_sge = 1, .wr_id = 1 };
ibv_post_recv(rdma->qp, &wr, &bad);
```
---

## 8. Client Workflow

**`run_client()` performs:**

1. Fill local metadata
2. Connect to server and exchange metadata
3. Create AH with the server’s GID
4. Prepare local message
5. Issue two RDMA writes with immediate:
    * one full-size message
    * one small 8-byte message
  ```c
  ibv_wr_rdma_write_imm(wr, remote_rkey, remote_addr, imm_data);
  ```
6. Poll CQ for completion

---

## 9. How the Example Fits Together

* TCP is used only once to exchange metadata
* Both sides create AH for UD-style routing
* SRD QP enables RDMA write/read
* Client writes directly into server’s MR
* Server receives the writes via CQ
* Immediate data is delivered alongside each write
* The server prints the final message stored in its buffer

