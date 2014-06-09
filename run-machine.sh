# Action:
#	1. Run num_processes client processes

num_processes=3			# Number of processes per client machine
export ROCE=0
export APT=1

hi=`expr $num_processes - 1`
for i in `seq 0 $hi`; do
	id=`expr $@ \* $num_processes + $i`
	echo "Running client id $id"
	touch client-tput/client-$id

	if [ $APT -eq 1 ]	# There is only one socket on Apt's r320 nodes
	then
		sudo -E ./main $id < servers 1>client-tput/client-$id 2>client-tput/client-$id &
	else
		if [ $ROCE -eq 1 ]	# Susitna's RoCE RNIC is connected to CPU 0
		then
			core=`expr 0 + $id`
			sudo -E numactl --physcpubind $core --interleave 0,1 ./main $id < servers &
		else				# Susitna's IB RNIC is connected to CPU 3
			core=`expr 32 + $id`
			sudo -E numactl --physcpubind $core --interleave 4,5 ./main $id < servers &
		fi
	fi
	
	sleep .1
done

# When we run this script remotely, the client processes die when this script dies
# So, sleep.
sleep 10000
