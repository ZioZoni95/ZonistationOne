#!/usr/bin/env bash
# Put it away. Stops, never deletes: the node containers, their volumes, the
# secrets and every memory card under cluster-data/ are all still there
# afterwards, and start.sh picks up where this left off.
#
# Use `k3d cluster delete cluster-zs1` for the destructive version — that one
# loses the cluster's secrets, including the basic-auth and TURN ones.
set -euo pipefail
CLUSTER=cluster-zs1

docker context use default >/dev/null

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
