# Memory Registration

## 1. What is Memory Registration?

As introduction said, **Memory Registration** is responsible for granting permission for remote access. It is the process of informing the **RDMA NIC**, also known as **Host Channel Adapter**. The most important point of it is that a region of application memory may be directly accessed via direct memory access.

It enables:
- Pinning the memory pages so they are not swapped or moved by the OS
- Installing virtual→physical address translations in the NIC
- Returning access keys (`lkey`, `rkey`) so Work Requests (WRs) can reference the Memory Region (MR)

Only registered memory can be used in RDMA operations.


## 2. Why Register Memory?

RDMA NICs cannot:
- Access virtual addresses
- Handle pageable memory

Registration ensures:
- Pages remain resident
- NIC can DMA directly
- Remote access is permission-controlled (`rkey`)

Without registration, RDMA Read/Write will fail.


## 3. Memory Registration Workflow

1. Allocate CPU or GPU memory
2. Register memory region
3. Obtain `lkey` / `rkey` from the MR
4. (For RDMA) exchange `(address, rkey)` with peer
5. Issue WRs referencing the MR
6. Deregister when no longer needed

Example (verbs):
```c
mr = ibv_reg_mr(pd, addr, length, access_flags);
/* ... */
ibv_dereg_mr(mr);
```

## Some concepts
> **Memory Keys**

| Key  | Scope  | Purpose                                            |
|------|--------|----------------------------------------------------|
| lkey | Local  | Required in SGEs for DMA by local NIC              |
| rkey | Remote | Required for RDMA Read/Write/Atomic by remote peer |


> **Access Flags**

| Flag            | Meaning                   |
|-----------------|---------------------------|
| `LOCAL_WRITE`   | Local NIC may write to MR |
| `REMOTE_WRITE`  | Remote RDMA Write allowed |
| `REMOTE_READ`   | Remote RDMA Read allowed  |
| `REMOTE_ATOMIC` | Atomic ops allowed        |