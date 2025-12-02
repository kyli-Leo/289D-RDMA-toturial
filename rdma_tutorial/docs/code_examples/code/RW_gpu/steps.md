#  **1. Start the Server**

Open **Terminal A** and log into the machine where the server will run. Check the machine’s available network interfaces:

```bash
hostname -I
```

From the output, choose an IPv4 address that is **not** `127.x.x.x`.
In most RDMA clusters, the correct address is typically a **10.x.x.x** or **192.x.x.x** address.

**Example:**

```
45.76.29.254 10.162.224.130 172.17.0.1 ...
```

In this example, the valid RDMA-capable address would be:

```
10.162.224.130
```

Start the server:

```bash
./server 7471
```

Expected output:

```
[server] listening on 7471 ...
```

Keep this terminal open.

---

# **2. Start the Client**

Open **Terminal B** and log into the machine where the client will run. Run the client using the server’s selected IP address:

```bash
./client <server_ip> 7471
```

Example:

```bash
./client 10.162.224.130 7471
```

---

# **Expected Successful Output**

### **Client Side**

```
[client][GPU] RDMA write done (20 bytes)
```

### **Server Side**

```
[server] got: 'Hello RDMA from GPU'
```

This confirms that the **GPU → GPU RDMA write** completed successfully.
