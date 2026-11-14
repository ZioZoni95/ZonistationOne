#!/usr/bin/env bash
# Bring up the single-session setup on cilium-ha-cluster (Ace Combat 2 only).
#
# Idempotent. Unlike start.sh it applies no manifests: they live in the cluster
# already, and only the zs1-acecombat Deployment exists here, so scaling it back
# to one is all that is left to do.
#
# cilium-ha-cluster has THREE servers. start_cluster_ordered (cluster-lib.sh)
# starts server-0 and the agents only, which is right for cluster-zs1's single
# server and wrong here: one server of three has no etcd quorum. All the
# servers are started first, then the library orders the agents.
set -euo pipefail
export CLUSTER=${CLUSTER:-cilium-ha-cluster}
export NS=zs1
HERE=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=cluster-lib.sh
. "$HERE/cluster-lib.sh"

docker context use default >/dev/null

# The NVIDIA device plugin opens an inotify watch per node; at the default of 128
# it crashloops and no node advertises a GPU, so the session stays Pending.
inst=$(sysctl -n fs.inotify.max_user_instances)
if [ "$inst" -lt 1024 ]; then
    warn "fs.inotify.max_user_instances is $inst; the GPU device plugin will crash."
    warn "  sudo sysctl -w fs.inotify.max_user_instances=1024 fs.inotify.max_user_watches=524288"
fi

echo "== cluster =="
k3d cluster list "$CLUSTER" --no-headers 2>/dev/null | grep -q "$CLUSTER" \
    || { echo "cluster $CLUSTER does not exist" >&2; exit 1; }
for s in $(docker ps -a --filter "label=k3d.cluster=$CLUSTER" --filter label=k3d.role=server \
               --format '{{.Names}}' | sort); do
    is_running "$s" || { log "starting $s"; k3d node start "$s" >/dev/null; }
done
start_cluster_ordered
kubectl config use-context "k3d-$CLUSTER" >/dev/null
wait_nodes_ready 6 || exit 1
unstick_pods

echo "== session =="
kubectl scale deployment zs1-acecombat -n "$NS" --replicas=1 >/dev/null
echo -n "waiting for the session "
tries=0
until kubectl get pods -n "$NS" --no-headers 2>/dev/null | grep -q '1/1.*Running'; do
    tries=$((tries + 1))
    [ $tries -le 60 ] || { echo; warn "not Running after 3 minutes: kubectl get pods -n $NS"; exit 1; }
    echo -n .; sleep 3
done; echo " ok"

echo "== reachable at =="
echo "  http://acecombat.localhost:8082/webrtc.html"
ip -4 -o addr show scope global 2>/dev/null | awk '{split($4,a,"/"); print a[1]}' \
    | grep -vE '^(172\.|10\.4[2-5]\.|192\.168\.49\.)' \
    | while read -r a; do echo "  http://acecombat.$a.nip.io:8082/webrtc.html"; done
