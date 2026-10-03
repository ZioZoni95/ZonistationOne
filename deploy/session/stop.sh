#!/usr/bin/env bash
# Put it away. Stops, never deletes: the node containers, their volumes, the
# secrets and every memory card under cluster-data/ are all still there
# afterwards, and start.sh picks up where this left off.
#
# Use `k3d cluster delete cluster-zs1` for the destructive version — that one
# loses the cluster's secrets, including the basic-auth and TURN ones.
#
# The sessions are scaled to zero and given time to finish before the nodes go.
# Stopping a node under a running pod kills the kubelet mid-delete, and the pod
# then sits in Terminating until someone forces it; start.sh scales them back to
# one. See cluster-lib.sh for the other half of the story, the node addresses.
set -euo pipefail
CLUSTER=cluster-zs1
NS=zs1
KC=(kubectl --context "k3d-$CLUSTER")

docker context use default >/dev/null

if docker ps --format '{{.Names}}' | grep -qx "k3d-$CLUSTER-server-0" \
   && "${KC[@]}" get namespace "$NS" >/dev/null 2>&1; then
    echo "scaling the sessions to zero"
    "${KC[@]}" scale deployment --all -n "$NS" --replicas=0 >/dev/null
    # Up to 90 s: a pod has 30 s of grace by default, and the GPU one needs it.
    for _ in $(seq 1 45); do
        [ -z "$("${KC[@]}" get pods -n "$NS" --no-headers 2>/dev/null)" ] && break
        sleep 2
    done
    left=$("${KC[@]}" get pods -n "$NS" --no-headers 2>/dev/null | wc -l)
    [ "$left" -eq 0 ] || echo "$left pod(s) still ending; start.sh will force them" >&2
fi

if docker ps --format '{{.Names}}' | grep -qx zs1-turn; then
    docker stop zs1-turn >/dev/null
    echo "zs1-turn stopped"
fi

if k3d cluster list "$CLUSTER" --no-headers 2>/dev/null | grep -q "$CLUSTER"; then
    k3d cluster stop "$CLUSTER" >/dev/null
    echo "cluster $CLUSTER stopped"
fi

# tailscale serve keeps its configuration across this and across reboots; it
# simply has nothing to proxy to until start.sh runs again.
echo "tailscale serve left configured — nothing listens behind it until start.sh"
