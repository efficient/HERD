rm temp
sudo apt-get install bc
DIR=$@
total_sum=0
total_files=0
for file in $DIR/*; do
	echo "Processing file $file"
	tail -20 $file >> temp
	total_files=`expr $total_files + 1`
done

sum=`awk '{ sum += $1 } END { print sum }' temp`
echo "Sum = $sum"
N=`wc -l temp | cut -f1 -d' '`
avg=`python -c "print $sum / $N"`

throughput=`python -c "print $avg * $total_files"`
echo "Directory $DIR's average = $avg, throughput $throughput" 
