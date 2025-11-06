# Memory Registration

## 1. What is Memory Registration?

As introduction said, **Memory Registration** is responsible for granting permission for remote access. It is the process of informing the **RDMA NIC**, also known as **Host Channel Adapter**. The most important point of it is that a region of application memory may be directly accessed via direct memory access.

It enables:

<!-- - Pinning the memory pages so they are not swapped or moved by the OS （If the operating system moves the memory to other location, the NIC will not be able to find it. In this case, the data will be likely read or written incorrectly, potentially causing errors.） -->
- **Pinning the memory pages** so they are not swapped or moved by the OS. If the operating system moves the memory to another location, the NIC will not be able to find it. In this case, the data will likely be read or written incorrectly, potentially causing errors.

- Installing virtual→physical address translations in the NIC
- Returning access keys (`lkey`, `rkey`) so Work Requests (WRs) can reference the Memory Region (MR)

> Notice: Only registered memory can be used in RDMA operations!


## 2. Why Register Memory?

RDMA NICs cannot:

- Access virtual addresses
- Handle pageable memory

Registration ensures:

- Pages remain resident
- NIC can DMA directly
- Remote access is permission-controlled (`rkey`)

> Without registration, RDMA Read/Write will fail.


## 3. Memory Registration Workflow

- **Allocate CPU or GPU memory**  
   This creates a virtual buffer, but it is not yet DMA-safe.

- **Register the memory region**  
   The application calls `ibv_reg_mr()` (or library equivalent) to register the buffer with the NIC. This pins pages, installs address translations, and prepares NIC bookkeeping.

- **Obtain `lkey` / `rkey`**  
   The returned MR object contains:  
    - `lkey` → used by local NIC to perform DMA  
    - `rkey` → shared with peers to authorize RDMA reads / writes / atomics  

- **Exchange `(address, rkey)` with peer**  
   To allow remote RDMA, peers must obtain the buffer base address and `rkey`.

- **Issue Work Requests (WRs)**  
   DMA operations reference the MR via SGEs containing the base address, length, and `lkey`.

- **Deregister when no longer needed**  
   `ibv_dereg_mr()` releases pinned pages and NIC metadata. WRs referencing the MR must be completed first.

Example (verbs):
```c
struct ibv_mr* mr = ibv_reg_mr(
    pd,          // Protection Domain
    addr,        // Base address
    length,      // Size (bytes)
    access_flags // e.g., LOCAL_WRITE | REMOTE_WRITE
);

/* ... use mr->lkey / mr->rkey ... */

ibv_dereg_mr(mr);   // Cleanup
```

## 4. Some concepts
> **Memory Keys**

| Key  | Scope  | Purpose                                            |
|------|--------|----------------------------------------------------|
| `lkey` | Local  | Required in SGEs for DMA by local NIC              |
| `rkey` | Remote | Required for RDMA Read/Write/Atomic by remote peer |


> **Access Flags**

| Flag            | Meaning                   |
|-----------------|---------------------------|
| `LOCAL_WRITE`   | Local NIC may write to MR |
| `REMOTE_WRITE`  | Remote RDMA Write allowed |
| `REMOTE_READ`   | Remote RDMA Read allowed  |
| `REMOTE_ATOMIC` | Atomic ops allowed        |