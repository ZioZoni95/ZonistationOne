#!/usr/bin/env bash
# Create cluster-zs1: 1 server + 3 agents, all with GPU access.
#
# Runs against the SYSTEM docker daemon, not Docker Desktop: Desktop's daemon
# lives in a LinuxKit VM with no NVIDIA passthrough, so --gpus is silently
# useless there. The context switch is the difference between a working GPU
# node and one that reports no devices.
set -euo pipefail

CLUSTER=cluster-zs1
IMAGE=zs1/k3s-cuda:v1.33.4-k3s1
REPO=$(cd "$(dirname "$0")/../.." && pwd)

docker context use default

# Flannel is left ON deliberately. cluster-svil was created with
# --flannel-backend=none and no --disable-network-policy, and its server has
# been crashlooping ever since on:
#   "unable to initialize network policy controller: error getting node subnet"
# Do not disable one without the other.
# The kubelet's default eviction threshold is 10% free on nodefs. All four node
# containers sit on the host's single filesystem, which runs near full, so the
# default taints every node with node.kubernetes.io/disk-pressure and nothing
# schedules at all. A session writes only memory cards and savestates — a few
# MB — so the threshold is lowered rather than the workload shrunk.
#
# eviction-minimum-reclaim has to move with it. k3s defaults it to 10%, which
# means the kubelet keeps DiskPressure set and keeps evicting until free space
# reaches threshold + reclaim = 12% — so lowering eviction-hard alone changes
# nothing, and the node stays tainted with the disk visibly above the threshold.
# That combination is what made the taint look stuck. This does
# not create free space: if the host disk genuinely fills, the nodes will
# misbehave instead of refusing work early.

# Host disks reach the pods in two hops: k3d bind-mounts them into every node
# container here, and a pod then takes a hostPath on /mnt/zs1/... . Discs and
# BIOS are read-only; /mnt/zs1/data is where memory cards and savestates are
# written, and it must NOT be read-only or every boot's card write test fails.
#
# Volumes can only be declared at cluster creation. Adding one later means
# recreating the cluster.

# Non-default CIDRs, deliberately.
#
# A native k3s runs on this host (k3s.service, /usr/local/bin/k3s server) and
# uses the k3s defaults 10.42.0.0/16 (pods) and 10.43.0.0/16 (services). A k3d
# cluster on those same ranges collides: the host's iptables catch 10.43.0.1
# for traffic leaving the node containers and send it to the host apiserver,
# which answers with a certificate signed by a CA the k3d pods do not trust.
# The symptom is every system pod failing with
#   x509: certificate signed by unknown authority
# cluster-dns must sit inside service-cidr, so it moves too.
k3d cluster create "$CLUSTER" \
  --image "$IMAGE" \
  --servers 1 \
  --agents 3 \
  --gpus all \
  --volume "$REPO/roms:/mnt/zs1/roms:ro@all" \
  --volume "$REPO/games:/mnt/zs1/games:ro@all" \
  --volume "$REPO/cluster-data:/mnt/zs1/data@all" \
  --k3s-arg "--cluster-cidr=10.44.0.0/16@server:*" \
  --k3s-arg "--service-cidr=10.45.0.0/16@server:*" \
  --k3s-arg "--cluster-dns=10.45.0.10@server:*" \
  --k3s-arg "--kubelet-arg=eviction-hard=nodefs.available<2%,imagefs.available<2%,nodefs.inodesFree<2%@all" \
  --k3s-arg "--kubelet-arg=eviction-minimum-reclaim=nodefs.available=1%,imagefs.available=1%@all" \
  --port "8081:80@loadbalancer" \
  --port "8444:443@loadbalancer" \
  --wait

kubectl config use-context "k3d-$CLUSTER"

# The server already carries control-plane/master from k3s itself. The worker
# role has to be applied here rather than via --k3s-node-label: NodeRestriction
# forbids the kubelet from setting any node-role.kubernetes.io/ label on itself.
for n in $(kubectl get nodes -o name | grep -- '-agent-'); do
  kubectl label "$n" node-role.kubernetes.io/worker=worker --overwrite
done

kubectl apply -f "$(dirname "$0")/nvidia-device-plugin.yaml"

kubectl get nodes -o wide
