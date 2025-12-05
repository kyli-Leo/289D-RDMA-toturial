This example extends the basic RDMA Read pattern to GPU memory on both sides using HIP (ROCm). It demonstrates how a client can pull data directly from a GPU buffer on the server into a GPU buffer on the client via a single one-sided RDMA Read.

On the server side, the program:

- Accepts an RDMA connection and creates a reliable connected Queue Pair.
- Allocates a 4 KB buffer in GPU memory with hipMalloc, initializes it, and copies an application-defined message such as `"Hello RDMA READ from GPU (server)"` into this device buffer using hipMemcpyHostToDevice.
- Registers the GPU buffer as a memory region with `IBV_ACCESS_REMOTE_READ`, making it readable by the remote peer.
- Packs the GPU buffer’s (`addr, rkey, len`) into a small Info struct and sends it to the client through the RDMA CM private-data field during rdma_accept.
- After the client has issued the RDMA Read, the server optionally copies the GPU buffer back to host memory and prints it, confirming that the data remained intact.

On the client side, the program:

- Resolves the server’s address and route, creates a Queue Pair, and establishes an RDMA connection with rdma_connect.
- Receives the server’s GPU memory metadata (`addr, rkey, len`) via private data from the connection event.
- Allocates a GPU buffer with hipMalloc to act as the destination of the RDMA Read, initializes it with zeros, and registers this device pointer as a local memory region with `IBV_ACCESS_LOCAL_WRITE`.
- Posts a single `IBV_WR_RDMA_READ` work request that pulls data directly from the server’s GPU buffer into the client’s GPU buffer, and waits for completion by polling the send completion queue.
- After completion, copies the first bytes from GPU to host using `hipMemcpyDeviceToHost` and prints the string that was fetched from the server.

The following are the actual implementations:

```
--8<-- "code/basic_read_gpu/client.cpp"
```

```
--8<-- "code/basic_read_gpu/server.cpp"
```