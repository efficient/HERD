for i in `seq 0 64`; do
	sudo ipcrm -M $i 2>/dev/null
done
sudo ipcrm -M 3185 2>/dev/null		#Request area shm key
