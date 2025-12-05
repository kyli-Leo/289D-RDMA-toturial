# RDMA Write GPU Direct Example
This example extends the minimal RDMA Write program by moving both the source and destination buffers onto the GPU using HIP (ROCm), demonstrating a GPU-direct RDMA Write.

On the server side, the program:

- Accepts an RDMA connection and creates a Queue Pair.
- Allocates a 4 KB buffer in GPU memory using hipMalloc, initializes it with hipMemset, and registers this device pointer as an RDMA memory region (ibv_reg_mr with remote-write access).
- Exposes the GPU buffer’s (addr, rkey, len) to the client through RDMA connection private data.
- After the client performs an RDMA Write, the server copies the first 64 bytes from GPU to host using hipMemcpyDeviceToHost and prints the received string.

On the client side, the program:

- Resolves the server address and route, creates a Queue Pair, and connects via rdma_cm.
- Receives the server’s GPU memory metadata (addr, rkey, len) via private data.
- Allocates a GPU buffer with hipMalloc, copies the message "Hello RDMA from GPU" from host to device using hipMemcpyHostToDevice, and registers this GPU buffer as an RDMA memory region.
- Issues a single RDMA Write from its GPU buffer directly into the server’s GPU buffer and waits for completion by polling the send completion queue

The following are the actual implementations:

```
--8<-- "code/basic_write_gpu/client.cpp"
```

```
--8<-- "code/basic_write_gpu/server.cpp"
```