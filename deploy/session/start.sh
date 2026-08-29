#!/usr/bin/env bash
# Bring the whole thing up: cluster, relay, sessions, reachable host names.
#
# Safe to run repeatedly — every step is idempotent, and the parts that hold
# state (secrets, ingress rules, memory cards) survive a stop because k3d keeps
# the node containers rather than deleting them.
set -euo pipefail
CLUSTER=cluster-zs1
NS=zs1
HERE=$(cd "$(dirname "$0")" && pwd)

# Docker Desktop's daemon has no NVIDIA passthrough and is not where this lives.
docker context use default >/dev/null

echo "== disk =="
# The kubelet taints every node with disk-pressure below its threshold, and the
# nodes share the host filesystem. Worth seeing before wondering why nothing
# schedules.
df -h / | tail -1

echo "== cluster =="
if k3d cluster list "$CLUSTER" --no-headers 2>/dev/null | grep -q "$CLUSTER"; then
    k3d cluster start "$CLUSTER" >/dev/null
else
    echo "cluster $CLUSTER does not exist — run $HERE/../k3d-cuda/create-cluster.sh first" >&2
    exit 1
fi
kubectl config use-context "k3d-$CLUSTER" >/dev/null

echo -n "waiting for nodes "
until [ "$(kubectl get nodes --no-headers 2>/dev/null | grep -c ' Ready ')" -ge 4 ]; do
    echo -n .; sleep 3
done; echo " ok"

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

echo -n "waiting for sessions "
until [ "$(kubectl get pods -n "$NS" --no-headers 2>/dev/null | grep -c '1/1.*Running')" -ge 3 ]; do
    echo -n .; sleep 3
done; echo " ok"

echo "== reachable at =="
# Addresses can change between sessions (DHCP, a new tailnet), so the host rules
# are rebuilt from the interfaces present now rather than trusted from last time.
"$HERE/expose.sh"
