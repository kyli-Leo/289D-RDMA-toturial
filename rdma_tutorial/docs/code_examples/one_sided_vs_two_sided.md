## One-sided vs Two-sided RDMA: Implementation & Benchmark API

We implement a benchmark set that allows us to measure throughput (Mops) and bandwidth (GiB/s) for:
- One-sided `RDMA READ`
- One-sided `RDMA WRITE`
- Two-sided `SEND/RECV`

The framework can tune message size, iteration count, and in-flight window/recv depth to see how performance scales. 

### Build
```bash
$ cd docs/code_examples/code/one_side_vs_two_side
$ gcc bench_server.c -o bench_server -lrdmacm -libverbs
$ gcc bench_client.c -o bench_client -lrdmacm -libverbs
```

### Server API
```
./bench_server <port> [--mode read|write|send] [--msg N] [--iters N] [--recv-depth N]
```
- `--mode`: `read` exposes a buffer for client RDMA READ; `write` exposes a buffer for client RDMA WRITE; `send` preposts receives to accept SENDs.
- `--msg`: message size (bytes).
- `--iters`: total operations to expect.
- `--recv-depth`: number of receives preposted in SEND mode (must cover client window).

### Client API
```
./bench_client <server_ip> <port> [--mode read|write|send] [--msg N] [--iters N] [--window N]
```
- `--mode`: `read` issues one-sided RDMA READs; `write` issues one-sided RDMA WRITEs; `send` does two-sided SENDs.
- `--msg`: message size (bytes); must not exceed server-advertised buffer.
- `--iters`: total operations to issue.
- `--window`: outstanding WRs allowed in flight (match server `recv-depth` in SEND mode).

### Example runs
- RDMA READ, 8 KB, 100k ops, window 64:
```
server: ./bench_server 9000 --mode read --msg 8192 --iters 100000
client: ./bench_client 10.0.0.1 9000 --mode read --msg 8192 --iters 100000 --window 64
```
- RDMA SEND, 4 KB, 200k ops, recv depth 256/window 128:
```
server: ./bench_server 9000 --mode send --msg 4096 --iters 200000 --recv-depth 256
client: ./bench_client 10.0.0.1 9000 --mode send --msg 4096 --iters 200000 --window 128
```

Add test results below when ready.  
