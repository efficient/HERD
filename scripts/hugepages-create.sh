if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters"
	echo "Usage: ./hugepages-create.sh <socket-id> <number of hugepages>"
	exit
fi

echo "Creating $2 hugepages on socket $1"
echo $2 > /sys/devices/system/node/node$1/hugepages/hugepages-2048kB/nr_hugepages
