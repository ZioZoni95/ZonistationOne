#!/usr/bin/env bash
# Bring the whole thing up: cluster, relay, sessions, reachable host names.
#
# Safe to run repeatedly — every step is idempotent, and the parts that hold
# state (secrets, ingress rules, memory cards) survive a stop because k3d keeps
# the node containers rather than deleting them.
#
# The nodes are started one at a time, in the order of the addresses they
# registered with, because a plain `k3d cluster start` starts the agents in
# parallel and Docker then hands the addresses out in a different order, which
# leaves agents NotReady. cluster-lib.sh explains it and holds the code.
set -euo pipefail
CLUSTER=cluster-zs1
NS=zs1
HERE=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=cluster-lib.sh
. "$HERE/cluster-lib.sh"

# Docker Desktop's daemon has no NVIDIA passthrough and is not where this lives.
docker context use default >/dev/null

echo "== disk =="
# The kubelet taints every node with disk-pressure below its threshold, and the
# nodes share the host filesystem. Worth seeing before wondering why nothing
# schedules.
df -h / | tail -1

echo "== cluster =="
if k3d cluster list "$CLUSTER" --no-headers 2>/dev/null | grep -q "$CLUSTER"; then
    start_cluster_ordered
else
    echo "cluster $CLUSTER does not exist — run $HERE/../k3d-cuda/create-cluster.sh first" >&2
    exit 1
fi
kubectl config use-context "k3d-$CLUSTER" >/dev/null

wait_nodes_ready 4 || exit 1
# Pods an earlier stop left in Terminating would otherwise keep the old replicas
# alive next to the new ones.
unstick_pods

echo "== turn relay =="
# coturn lives on the host, not in the cluster: a relay has to sit on an address
# the viewer can reach, and no address inside k3d is.
if docker ps -a --format '{{.Names}}' | grep -qx zs1-turn; then
    docker start zs1-turn >/dev/null
    echo "zs1-turn started"
else
    echo "zs1-turn missing — see the coturn invocation in README" >&2
fi

echo "== workloads =="
kubectl apply -f "$HERE/../k3d-cuda/zs1-storage.yaml" >/dev/null
kubectl apply -f "$HERE/sessions.yaml" >/dev/null
kubectl apply -f "$HERE/ingress.yaml"  >/dev/null
# stop.sh scales the sessions to zero so their pods end cleanly; this undoes it.
kubectl scale deployment --all -n "$NS" --replicas=1 >/dev/null

echo -n "waiting for sessions "
until [ "$(kubectl get pods -n "$NS" --no-headers 2>/dev/null | grep -c '1/1.*Running')" -ge 3 ]; do
    echo -n .; sleep 3
done; echo " ok"

echo "== reachable at =="
# Addresses can change between sessions (DHCP, a new tailnet), so the host rules
# are rebuilt from the interfaces present now rather than trusted from last time.
"$HERE/expose.sh"
