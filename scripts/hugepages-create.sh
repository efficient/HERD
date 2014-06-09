# We need 1GB log + 256 MB index for each server. This is 512 + 128 hugepages
# per server. This is 5120 hugepages.

#sudo numactl echo 0 > /proc/sys/vm/nr_hugepages_mempolicy
#sudo numactl echo 8192 > /proc/sys/vm/nr_hugepages_mempolicy

sudo sysctl -w vm.nr_hugepages=$@
