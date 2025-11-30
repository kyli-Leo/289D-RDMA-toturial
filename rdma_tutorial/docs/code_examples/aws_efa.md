> **Notice:**  
> Because our current devices do not support EFA, we could not execute the RDMA test.  
> Here is the functional description of the code:  
> [efa_rdma_write.cc](https://github.com/uccl-project/uccl/blob/main/misc/efa_rdma_write.cc)


# EFA RDMA Write Example

This project demonstrates how to build a simple **RDMA Write with Immediate** pipeline using **Amazon EFA** and its **SRD QP** capability. Although the current testing environment does not support EFA, this document describes the functional behavior of the program.

The example shows:

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

  * one large message
  * one small message
* Polls CQ for completion

All routing is handled through **SRD**, which uses UD-style addressing but provides reliable, ordered delivery similar to RC.

---

## 2. RDMA Context

All RDMA resources are grouped into a simple context structure containing:

* Device context
* Protection domain
* Extended completion queue
* SRD QP
* Memory region (host or GPU)
* Address handle
* Local/remote buffer information

`init_rdma()` handles device selection, CQ creation, MR registration, and QP creation.
This prepares the RDMA environment used by both sides.

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

This one-time handshake bootstraps the RDMA data path.

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

Polling continues until the expected number of completions arrive.

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

The server is fully passive in the data movement.

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
6. Poll CQ for completion

This side performs the actual RDMA data transfer.

---

## 9. How the Example Fits Together

* TCP is used only once to exchange metadata
* Both sides create AH for UD-style routing
* SRD QP enables RDMA write/read
* Client writes directly into server’s MR
* Server receives the writes via CQ
* Immediate data is delivered alongside each write
* The server prints the final message stored in its buffer

---

## 10. Conclusion

This example demonstrates the minimal workflow required to use **EFA SRD QP for RDMA operations**:

1. **Metadata exchange** establishes addressing and memory sharing
2. **SRD QP** combines UD routing with RC-style features
3. **Immediate data** allows tagging or sequencing
4. **Extended CQ polling** retrieves rich completion information
5. **Client-driven RDMA writes** update the server buffer without CPU involvement

Although the program cannot be executed without EFA hardware, the code illustrates the complete RDMA data path from initialization to completion handling.

