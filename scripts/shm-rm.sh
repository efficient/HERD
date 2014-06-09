for i in `seq 0 64`; do		# Lossy index and circular log
	sudo ipcrm -M $i
done
sudo ipcrm -M 3185			# Request region at server
sudo ipcrm -M 3186			# Response region at server
