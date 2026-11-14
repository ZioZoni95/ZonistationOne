#!/usr/bin/env bash
# Put away the single-session setup on cilium-ha-cluster (Ace Combat 2 only).
# Stops, never deletes: node containers, the BIOS/disc copies under /mnt/zs1 in
# the agents, the secrets and the Cilium/Traefik install are all still there
# afterwards, and start-ac2.sh picks up where this left off.
#
# stop.sh is for cluster-zs1 and would find nothing to stop here. The order is
# the same: the session is scaled to zero first and given time to finish,
# because stopping a node under a running GPU pod leaves it in Terminating.
set -euo pipefail
CLUSTER=${CLUSTER:-cilium-ha-cluster}
NS=zs1
KC=(kubectl --context "k3d-$CLUSTER")

docker context use default >/dev/null

if docker ps --format '{{.Names}}' | grep -qx "k3d-$CLUSTER-server-0" \
   && "${KC[@]}" get namespace "$NS" >/dev/null 2>&1; then
    echo "scaling the sessions to zero"
    "${KC[@]}" scale deployment --all -n "$NS" --replicas=0 >/dev/null
    # Up to 90 s: the pod has 30 s of grace by default, and the GPU one needs it.
    for _ in $(seq 1 45); do
        [ -z "$("${KC[@]}" get pods -n "$NS" --no-headers 2>/dev/null)" ] && break
        sleep 2
    done
    left=$("${KC[@]}" get pods -n "$NS" --no-headers 2>/dev/null | wc -l)
    [ "$left" -eq 0 ] || echo "$left pod(s) still ending; start-ac2.sh will force them" >&2
fi

if k3d cluster list "$CLUSTER" --no-headers 2>/dev/null | grep -q "$CLUSTER"; then
    k3d cluster stop "$CLUSTER" >/dev/null
    echo "cluster $CLUSTER stopped"
else
    echo "cluster $CLUSTER does not exist" >&2
fi
