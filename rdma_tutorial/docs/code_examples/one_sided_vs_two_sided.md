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

### Test results

We would like to explore the impact of message size on MOPS and bandwidth for one-side and two-side RDMA. 

We test the difference between `write` and `send` with two machines connected with CoRE.

We iterate on the message size from 32 to 8192 bytes. 

Also it is important to note that the queue depth (window) has a great impact on the results as they allow the number of operations in flight. We test both `window=4` and `window=64` to understand how the system would perform in a shallow or deep queue.

Here's our results:
|experiment|mode |msg |window|iters |mops|gib |
|----------|-----|----|------|------|----|----|
|msg_sweep |write|32  |4     |200000|0.43|0.01|
|msg_sweep |send |32  |4     |200000|0.44|0.01|
|msg_sweep |write|64  |4     |200000|0.43|0.03|
|msg_sweep |send |64  |4     |200000|0.43|0.03|
|msg_sweep |write|128 |4     |200000|0.43|0.05|
|msg_sweep |send |128 |4     |200000|0.43|0.05|
|msg_sweep |write|256 |4     |200000|0.43|0.1 |
|msg_sweep |send |256 |4     |200000|0.43|0.1 |
|msg_sweep |write|512 |4     |200000|0.43|0.2 |
|msg_sweep |send |512 |4     |200000|0.42|0.2 |
|msg_sweep |write|1024|4     |200000|0.41|0.39|
|msg_sweep |send |1024|4     |200000|0.41|0.4 |
|msg_sweep |write|2048|4     |200000|0.41|0.78|
|msg_sweep |send |2048|4     |200000|0.41|0.78|
|msg_sweep |write|4096|4     |200000|0.4 |1.53|
|msg_sweep |send |4096|4     |200000|0.4 |1.54|
|msg_sweep |write|8192|4     |200000|0.39|2.96|
|msg_sweep |send |8192|4     |200000|0.39|2.98|
|msg_sweep |write|32  |64    |200000|3.34|0.1 |
|msg_sweep |send |32  |64    |200000|3.34|0.1 |
|msg_sweep |write|64  |64    |200000|3.31|0.2 |
|msg_sweep |send |64  |64    |200000|3.36|0.2 |
|msg_sweep |write|128 |64    |200000|3.27|0.39|
|msg_sweep |send |128 |64    |200000|3.34|0.4 |
|msg_sweep |write|256 |64    |200000|3.27|0.78|
|msg_sweep |send |256 |64    |200000|3.31|0.79|
|msg_sweep |write|512 |64    |200000|3.27|1.56|
|msg_sweep |send |512 |64    |200000|3.28|1.56|
|msg_sweep |write|1024|64    |200000|3.31|3.15|
|msg_sweep |send |1024|64    |200000|3.28|3.13|
|msg_sweep |write|2048|64    |200000|3.26|6.22|
|msg_sweep |send |2048|64    |200000|3.27|6.23|
|msg_sweep |write|4096|64    |200000|2.81|10.72|
|msg_sweep |send |4096|64    |200000|2.82|10.75|
|msg_sweep |write|8192|64    |200000|1.41|10.74|
|msg_sweep |send |8192|64    |200000|1.41|10.76|

![](code/one_side_vs_two_side/plots_msg_sweep_4/msg_sweep_gib_w4.png)

![](code/one_side_vs_two_side/plots_msg_sweep/msg_sweep_gib_w64.png)

![](code/one_side_vs_two_side/plots_msg_sweep_4/msg_sweep_mops_w4.png)

![](code/one_side_vs_two_side/plots_msg_sweep/msg_sweep_mops_w64.png)

As you can see in the resulted graphs, the message size would decrease MOPS but increase bandwidth.

When the queue depth is small (4), increasing the message size would greatly increase the bandwitdh and that does not converge to mature. However,  when the depth gets larger(64), the bandwidth converges at message size $2^{12}$ (4096) bytes and further increase of message size would only decrease MOPS.