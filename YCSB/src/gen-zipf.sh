sudo mkdir /dev/zipf
sudo chown akalia:fawn /dev/zipf

# As this script is executed by the server on all clients (via ssh),
# do not recompile zipf.java. This can lead to some clients seeing
# a bad zipf class (ToCToU).
java zipf
