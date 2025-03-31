/opt/mellanox/dpdk/bin/dpdk-hugepages.py -r8G

PF=ens2f0np0
VF=ens2f0v0
echo 0 > /sys/class/net/$PF/device/sriov_numvfs
echo switchdev > /sys/class/net/$PF/compat/devlink/mode
echo 1 > /sys/class/net/$PF/device/sriov_numvfs

sleep 1 # wait for VF to come up
ifconfig $PF up mtu 9216
ifconfig $VF up mtu 9216

lshw -c network -businfo
