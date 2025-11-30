# UCCL Optimizations

## Chained post
UCCL pre-allocates a `wr_ex` pool for each RDMAContext, with each chunk corresponding to one `wr_ex` and links multiple work requests into a chain using `wr->next`. When sending data and ACKs, only one  `ibv_post_send` is used to send the entire chain to the NIC. This approach reduces the number of doorbell calls and the initialization/allocation overhead of each WR. Combined with TXTracking, it reuses the original `wr_ex` during retransmission, significantly reducing CPU overhead and improving NIC utilization.

Then I will explain in detail in combination with the source code in `transport.h`.

### WrExBuffPool
```
/**
 * @brief Buffer pool for work request extension.
 */
class WrExBuffPool : public BuffPool {
  static constexpr size_t kWrSize = sizeof(struct wr_ex);
  static constexpr uint32_t kNumWr = 4096;
  static_assert((kNumWr & (kNumWr - 1)) == 0, "kNumWr must be power of 2");

public:
  WrExBuffPool()
    : BuffPool(kNumWr, kWrSize, nullptr, [](uint64_t buff) {
        struct wr_ex* wr_ex = reinterpret_cast<struct wr_ex*>(buff);
        auto wr = &wr_ex->wr;
        wr->sg_list = &wr_ex->sge;
        wr->num_sge = 1;
        wr->next = nullptr;
        wr->opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr->wr_id = 0;
    }) {}

  ~WrExBuffPool() = default;
};
```
WrExBuffPool is for work request extension. Every `wr_ex` include the work request `wr` and Scatter-Gather Element `sge`. The constructor function initialize each `wr` in the `wr_ex` by default inheriting the original data and for each `wr`, only one  `sge` needs to be done, and store them in a chain-like form.

In class `RDMAContext`, it has a optional WrExBuffPool. In this way, when sending data, no malloc WQE is performed. Instead, a batch of already initialized `wr_ex` is taken from `wr_ex_pool_`and used. Then, the data is organized for batch transmission by modifying sge/imm_data/next.
 ```
 // Buffer pool for work request extension items.
std::optional<WrExBuffPool> wr_ex_pool_;
 ```

 ### TXTracking
```
class TXTracking {
 public:
  struct ChunkTrack {
    struct ucclRequest* ureq;
    struct wr_ex* wr_ex;
    uint64_t timestamp;
    uint32_t csn;
    bool last_chunk;
  };

  ...

    inline void track_chunk(struct ucclRequest* ureq, 
    struct wr_ex* wr_ex, uint64_t timestamp, uint32_t csn,   bool last_chunk) { 
        unacked_chunks_.push_back({ureq, wr_ex, timestamp, csn last_chunk});
}

```
Each chunk corresponds to a `wr_ex`, facilitating retransmission and statistics.The `unacked_chunks_` stores all the `wr_ex` that have not been acknowledged, and it is used for Measure the round-trip time (RTT), packet loss, and queue delay, and during retransmission, you can simply retrieve the original `wr_ex` and resend it (participating in the chained post process again in `RDMAContext::try_retransmit_chunk`)


> **Conclusion:**  
>1. By using chain post, many chunks only needs a doorbell like realiazation `ibv_post_send(qpw.qp, wr, &bad)` in `transport.cc`, which can reduce kernel interaction.
>2. The `WrExBuffPool` can help pre-initialization to reduce the per-WR overhead
>3. By `TXTracking`, it use `wr_ex` and retransmission can also continue to use chained post


## Scatter gather list
The system separately allocates memory pools for retransmission chunks and retransmission headers. When constructing WQE, the `retr_chunk_hdr` and the original chunk data  are attached to the same `sg_list` of the WQE using the `ibv_sge` array. One doorbell is issued by the NIC to "gather" the complete message from multiple scattered addresses. This not only avoids the memory copy of the header and data, but also ensures that the control information and data are atomically delivered in a single operation. At the same time, in combination with the chained post at the upper layer, multiple WQE with SGLs are connected in series and submitted together, significantly reducing CPU overhead and the number of NIC doorbell requests.


Then I will explain in detail in combination with the source code in `rdma_io.h` and `transport.h`.
```
struct __attribute__((packed)) retr_chunk_hdr {
  // Target address for the lost chunk.
  uint64_t remote_addr;
  uint32_t imm_data;
};
static_assert(sizeof(struct retr_chunk_hdr) == 12,
              "retr_chunk_hdr size is not 12 bytes");

/**
 * @brief Buffer pool for retransmission chunks (original chunk + retransmission
 * header). Original chunk and retransmission header are transmitted through
 * scatter-gather list.
 */
class RetrChunkBuffPool : public BuffPool {
 public:
  static constexpr uint32_t kNumChunk = 4096;
  ...
  RetrChunkBuffPool(struct ibv_mr* mr)
      : BuffPool(kNumChunk, kRetrChunkSize, mr) {}
};

class RetrHdrBuffPool : public BuffPool {
 public:
  static constexpr size_t kHdrSize = sizeof(struct retr_chunk_hdr);
  static constexpr uint32_t kNumHdr = 4096;
  ...
  RetrHdrBuffPool(struct ibv_mr* mr) : BuffPool(kNumHdr, kHdrSize, mr) {}
};

```
This is to retransmit the designed header and  chunk buffer pool. when sending retransmission, it uses a WQE + multiple SGEs in the scatter-gather method.

Two parts of data are required for retransmission: 

1.A very small retransmission header `retr_chunk_hdr` (12 bytes), which contains: `remote_addr`: Lost chunk - The destination address to be written back on the opposite end；
`imm_data`: The original immediate data for the write operation (including FID/RID/CSN, etc.) 

2.A complete original chunk of data

In class `RDMAContext`,there is a corresponding code.
```
// Buffer pool for retransmission chunks.
std::optional<RetrChunkBuffPool> retr_chunk_pool_;
// Buffer pool for retransmission headers.
std::optional<RetrHdrBuffPool> retr_hdr_pool_;
// ...
struct ibv_mr* retr_mr_;
struct ibv_mr* retr_hdr_mr_;
```
This is an example where `ibv_sge` is passed in as a parameter in `transport.h` and ounting by `wr->sg_list = sge`.In this way, it can sse the scatter-gather list to send multiple buffers at once.
```
void rc_recv(void* data, int size, struct Mhandle* mhandle,
             struct ibv_send_wr* wr, struct ibv_sge* sge,
             struct ucclRequest* ureq);
```

> **Conclusion:**  
>1. header with `RetrHdrBuffPool`, data with `RetrChunkBuffPool`, and each of them is in its own MR. By "gathering" two SGEs at the NIC end into one packet, the CPU does not need to concatenate, nor does it need to combine the `retr_chunk_hdr` and chunk data into a continuous buffer, thus avoiding additional copying.
>2. Since the header and chunk are multiple SGEs of a WQE, they can be guaranteed to arrive together. 
>3. The dedicated `retr_mr_`/`retr_hdr_mr_` for retransmission does not affect the normal data path MR; the retransmission logic does not need to modify the original sending code; .