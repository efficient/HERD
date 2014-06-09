NUM_SERVERS=512
hi=`expr $NUM_SERVERS - 1`
echo $hi
for i in `seq 0 $hi`; do
	echo "10.0.0.50"
	echo `expr 5500 + $i`
done
