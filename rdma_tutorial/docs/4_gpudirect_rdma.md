# GPU direct RDMA


 In order to read and write remote GPU memory, we need to allow direct access to it. In ealier days, the usual solution is to have a mapping between the GPU memory and the host memory, copy the GPU memory to the host memory then perform regular RDMA. 

 Nowadays with specialized graphics card and NICs, we can directly support direct RDMA access from GPU to NICs. That technique is called *GPUDirect RDMA*.

 <figure>
  <img src="images/GPUDirect_driver_model.png" alt="GPUDirect RDMA within the Linux Device Driver Model" width="80%">
  <figcaption>GPUDirect RDMA within the Linux Device Driver Model</figcaption>
</figure>

## Implementation details

### Kernel requirements
To directly map the GPU memory to a memory region, a specialized driver is required. Nvidia provides a kernel module *nvidia-peermem* to facilicate this.

### Memory details
Note that only GPU memory in CUDA VA could be used in GPUDirect RDMA. Further more, pinning and unpinning GPU memory is required during data transfer. While the most straightforward way is just to pin it every time before transfer and unpin afterward, this is not recommended as both operations are costly. It is best to implement a cache system that would provide pinned memory every time it is required and unpin it lazily.
