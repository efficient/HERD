HERD
====

A Highly Efficient key-value system for RDMA

This version of HERD has been tested for the following configuration:

1. Software
  * OS: Ubuntu 12.04 (kernel 3.2.0)
  * RDMA drivers: `mlx4` from MLNX OFED 2.2 and 2.0. I suggest using the MLNX OFED version for Ubuntu 12.04.
2. Hardware
  * RNICs: 
    * ConnectX-3 353A (InfiniBand) with PCIe 2.0 x8
	* ConnectX-3 313A (RoCE) with PCIe 2.0 x8
	* ConnectX-3 354A (InfiniBand) with PCIe 3.0 x8

Initial setup:
-------------

1. I assume that the machines are named: `node-i.RDMA.fawn.apt.emulab.net` starting from `i = 1`.
  * The experiment requires at least `(1 + (NUM_CLIENTS / num_processes))` machines.
	`node-1` is the server machine.
  	`NUM_CLIENTS` is the total number of client processes, defined in `common.h`.
	`num_processes` is the number of client processes per machine, defined in
	`run-machine.sh`.
  * To modify the code for your machine names, change the machine names in `bomb.sh` and `run-server.sh`.

2. The HERD folder should be a subdir in your home directory. This is required 
by the bash scripts. Create this folder on the server machine and mount it at
all clients via NFS.

3. Add the `scripts` folder to your `PATH`.

4. Increase the `shmmax` and `shmall` parameters to very large values by running `init.sh`.

5. Create hugepages at the server machine. Use the `hugepages-create.sh` script
in the `scripts` folder. I suggest creating 4096 hugepages on each socket.
	```bash
	# To create 4096 hugepages on socket 0
	$ hugepages-create.sh 0 4096
	```

Quick start:
-----------

* Run `make` on every machine to build the executables

* Run `run-servers.sh` at node-1. The script will ssh into all the client machines
(`NUM_CLIENT_MACHINES` in number) and run the `run_machine.sh` script.

* If you do not want to run clients automatically from the server, delete the 
2nd loop from `run-servers.sh`. Then:
	
```bash	
# At node-1 (server)
./run-server.sh
# At node-2 (client 0)
./run-machine.sh 0
# At node-i (client i - 2)
./run-machine.sh (i - 2)
```

* To kill the server processes, run `kill.sh` at the server machine. To kill the 
client processes remotely, run `bomb.sh` at the server machine.



<!---
Algorithm details:
====

SERVER's ALGORITHM (one iteration)

1. Poll for a new request. The polling must be done on the last byte
of the request area slot. We must check (char) key != 0 and not just
key != 0. The latter can lead to a situation where the request is 
detected before the key is written entirely by the HCA (for example,
only the first 4 bytes have been writtesn). 

If no new request is found in FAIL_LIM tries, go to 2.

2. Move the pipeline forward and get a pipeline item as the return
value. The pipeline item contains the request type, the client
number from which this request was received, and the request area
slot (RAS) from which this request was received.
	2.1. If the request type is a valid type (GET_TYPE or PUT_TYPE),
	send a response to the client. Otherwise, do nothing.

3. Add the new request to the pipeline. The item that we're adding
is the one that was polled in step 1.

We zero out the polled field of the request and store it into the
pipeline item. This is a must do. Here's what happens if we don't
zero out the polled field. Although the client will not write
into the same request slot till we send a response for the slot, the 
server's round-robin polling will detect this request again.

We also zero out the len field of the request. This is useful because
clients do not WRITE to the len field for GETs. So, when a new
request is detected in (1), len == 0 means that the request is a
GET, otherwise it's a PUT.

OUTSTANDING REQUESTS / RESPONSES:
----

The number of outstanding responses from a server is WS_SERVER.
A server polls for SEND completions once per WS_SERVER SENDs.

The number of outstanding requests from a client is WINDOW_SIZE.
A client polls for a RECV completion WINDOW_SIZE iterations after
a request was posted. The client polls for SEND completions *very*
rarely: once every S_DEPTH iterations. This is because the RECV
completions, which are polled frequently, give an indication of 
SEND completions.

The client uses parameters CL_BTCH_SZ and CL_SEMI_BTCH_SZ to post
RECV batches.
--->
