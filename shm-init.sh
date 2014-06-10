# Increase the shmmax and shmall parameters so that a process can 
# map more memory via shmget (default is 32 MB)

sudo sysctl -w kernel.shmmax=2147483648		# Bytes
sudo sysctl -w kernel.shmall=2147483648		# Pages

sudo sysctl -p /etc/sysctl.conf

# If you want to generate the zipf workloads at the client machines, uncomment:
## for i in `seq 2 30`; do
## 	ssh node-$i.RDMA.fawn.apt.emulab.net "cd ~/HERD/YCSB/src; ./gen-zipf.sh" &
## done
