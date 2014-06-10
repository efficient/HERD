# Action:
#	Kill clients by ssh-ing into client machines

export APT=1

if [ $APT -eq 1 ]
then
	for i in `seq 2 110`; do
		ssh -oStrictHostKeyChecking=no node-$i.RDMA.fawn.apt.emulab.net "cd HERD; ./local-kill.sh" &
	done
else 
	for i in `seq 2 20`; do
		ssh anuj$i.RDMA.fawn.susitna.pdl.cmu.local "cd HERD; ./local-kill.sh"
	done
fi
