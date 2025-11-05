# Queue Pairs

## 1. What is a Queue Pair (QP)?

A **Queue Pair (QP)** is the fundamental RDMA endpoint consisting of a **Send Queue (SQ)** and a **Receive Queue (RQ)**.  
Applications post **work requests (WRs)** to these queues; the NIC (HCA) executes them and reports results via **Completion Queues (CQs)**.  
QPs are created within a **Protection Domain (PD)** and operate on data in **registered Memory Regions (MRs)**.

- **SQ:** outbound operations such as Send, RDMA Write/Read, and (on RC) atomics  
- **RQ:** inbound buffers for Send/Recv traffic  
- **Identifiers:** QP Number (QPN) and per-WR `wr_id` for app bookkeeping  
- **Completions:** CQEs indicate status, opcode, length, and `wr_id`

---

## 2. QP Types & Lifecycle

### 2.1 QP Transport Types (Capabilities and Usage)

| Type | Reliability | Operations Supported | Typical Usage |
|------|--------------|----------------------|----------------|
| **RC (Reliable Connected)** | Reliable, in-order | Send/Recv, RDMA Read/Write, Atomics | Data planes, storage, ML training |
| **UC (Unreliable Connected)** | Best-effort, in-order | Send/Recv, RDMA Write only | Streams with occasional loss |
| **UD (Unreliable Datagram)** | Best-effort, unordered | Send/Recv only | Discovery, control paths, multicast |

**Key Notes:**
- **RDMA Read and Atomics** require **RC** and proper MR access flags on the responder.  
- **UD** is connectionless at the transport layer; you specify destination info via **Address Handle (AH)** and **QKey**.

---

### 2.2 QP State Machine & Configuration Fields

**RESET → INIT** *(make the QP locally valid)*  
- Configure **port number**, **P_Key index**, and **QP access flags** (local write, remote write/read/atomic).  

**INIT → RTR (Ready-To-Receive)** *(program the remote path; your RQ can accept)*  
- Set **remote QPN**, **initial receive PSN**, **path MTU**, and **addressing attributes** (LID/GID, DGID, SGID index, hop limit, traffic class).  
- Define responder credits: maximum inbound RDMA Reads and minimum RNR timer.

**RTR → RTS (Ready-To-Send)** *(enable your SQ; you can transmit)*  
- Set **initial send PSN**, **timeout**, **retry counts**, **RNR retry**, and **max outbound RDMA Reads**.

**Operational Rules of Thumb**
- **Always pre-post enough Recv WQEs** before peer sends to avoid **RNR (Receiver-Not-Ready)**.  
- Exchange **QPN, LID/GID, PSNs, MTU, and path attributes** before moving to **RTS**.

---

## 3. Using a QP (WRs, CQ Polling, and Bring-Up)

### 3.1 Preparing to Receive
- Define **receive buffers** with **scatter-gather entries (SGEs)** bound to registered MRs.  
- Maintain a **pool of Recv WQEs** sized for message rate and latency (bandwidth–delay product).  
- **Replenish** RQ entries promptly after completions to keep steady queue depth.

---

### 3.2 Sending and RDMA Operations
- **Send/Recv**: available on RC, UC, and UD.  
- **RDMA Write / Write with Immediate**: available on RC and UC; use remote address and `rkey`.  
- **RDMA Read / Atomics**: RC-only; require NIC support and correct permissions.  
- Use **signaling policy** (e.g., signal every N sends) to balance CQ load and visibility.  
- Use **inline data** for small payloads to skip DMA reads and reduce latency.

---

### 3.3 Completions and Progress
- **Poll the CQ** regularly to retrieve completions.  
- For each completion:
  - Verify **success status**
  - Match on `wr_id` to identify the request
  - Repost Recvs or free buffers as needed  
- Keep a consistent **signaling strategy** (e.g., signal all Recvs, every Nth Send).  
  This ensures continued progress visibility without overwhelming the CQ.

---

### 3.4 End-to-End Bring-Up Checklist
1. Open device/context → allocate **PD** → create **CQ(s)**.  
2. Create **QP** with desired capabilities (WR depth, SGE limits, inline size) and type (RC/UC/UD).  
3. Move **RESET → INIT** (configure port, P_Key, access flags).  
4. Exchange connection data (**QPN, LID/GID, PSNs, MTU, path attrs**) via out-of-band or **RDMA CM**.  
5. Move **INIT → RTR** (remote path, QPN/PSN, addressing, MTU, read credits).  
6. Move **RTR → RTS** (local PSN, timeout, retry counts, outbound read credits).  
7. **Pre-post Recvs** on the responder; **post Sends/Reads/Writes** on the requester.  
8. **Poll CQ** for completions, handle errors, and maintain RQ depth.

---
