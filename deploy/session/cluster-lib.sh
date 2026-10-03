#!/usr/bin/env bash
# Shared by start.sh and stop.sh. Source it, do not run it.
#
# The problem it exists for. A k3s agent registers with the Kubernetes API under
# the container's IP address, and its kubelet looks for an interface carrying that
# address every time it starts. Docker gives a container the lowest free address
# of the network *at the moment it starts*, so the addresses depend on the order
# the node containers are started in, and `k3d cluster start` starts the agents
# in parallel, in no stable order. When they come back swapped the agent cannot
# find its interface and dies with
#     Failed to start networking: unable to initialize network policy controller:
#     error getting node subnet: failed to find interface with specified node ip
# the node stays NotReady, and its pods sit in Terminating for ever. Nothing is
# wrong with the cluster; it only has to be started in the right order.
#
# The right order is whatever the nodes registered with. The server answers that
# from inside its own container (`docker exec ... kubectl`), before anything else
# has been started, so it needs no state file and survives the cluster being
# recreated.

CLUSTER=${CLUSTER:-cluster-zs1}
NS=${NS:-zs1}
NET="k3d-$CLUSTER"
SERVER="k3d-$CLUSTER-server-0"

log() { printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }

is_running() { [ "$(docker inspect -f '{{.State.Running}}' "$1" 2>/dev/null)" = true ]; }

# Address Docker gave a container on the cluster network.
actual_ip() {
    docker inspect -f "{{(index .NetworkSettings.Networks \"$NET\").IPAddress}}" "$1" 2>/dev/null
}

# "<node name> <address>" per node, as registered with the API.
registered_ips() {
    docker exec "$SERVER" kubectl get nodes \
        -o jsonpath='{range .items[*]}{.metadata.name}{" "}{.status.addresses[?(@.type=="InternalIP")].address}{"\n"}{end}'
}

# Names of the agent containers, lowest registered address first. An agent that
# never registered (a new one) has no address and goes last.
agents_in_order() {
    local reg all
    reg=$(registered_ips 2>/dev/null | grep -v "^$SERVER " | sort -k2,2V | awk '{print $1}')
    all=$(docker ps -a --filter "label=k3d.cluster=$CLUSTER" --filter label=k3d.role=agent \
              --format '{{.Names}}' | sort)
    { printf '%s\n' "$reg"; printf '%s\n' "$all"; } | awk 'NF && !seen[$0]++'
}

# Is any running agent on an address other than the one it registered with?
agents_mismatched() {
    local name ip want bad=1
    while read -r name want; do
        [ "$name" = "$SERVER" ] && continue
        is_running "$name" || continue
        ip=$(actual_ip "$name")
        if [ -n "$want" ] && [ "$ip" != "$want" ]; then
            warn "  $name is on $ip, registered as $want"
            bad=0
        fi
    done < <(registered_ips 2>/dev/null)
    return $bad
}

# Start the cluster so that every node comes back on the address it registered.
start_cluster_ordered() {
    if ! is_running "$SERVER"; then
        log "starting $SERVER"
        k3d node start "$SERVER" >/dev/null
    fi
    local tries=0
    until docker exec "$SERVER" kubectl get nodes >/dev/null 2>&1; do
        tries=$((tries + 1))
        [ $tries -le 60 ] || { warn "the API in $SERVER did not answer in 2 minutes"; return 1; }
        sleep 2
    done

    # Agents already running on the wrong addresses (an earlier plain
    # `k3d cluster start`) are stopped first, so their addresses are free again.
    if agents_mismatched; then
        warn "agents are on the wrong addresses; stopping them to start in order"
        local a
        while read -r a; do
            is_running "$a" && docker stop "$a" >/dev/null
        done < <(agents_in_order)
    fi

    local want ip name
    while read -r name; do
        want=$(registered_ips | awk -v n="$name" '$1==n{print $2}')
        if ! is_running "$name"; then
            log "starting $name${want:+ (registered as $want)}"
            k3d node start "$name" >/dev/null
        fi
        ip=$(actual_ip "$name")
        if [ -n "$want" ] && [ "$ip" != "$want" ]; then
            warn "$name came up on $ip, not the $want it registered with."
            warn "Something else holds $want. Check: docker network inspect $NET"
        fi
    done < <(agents_in_order)

    # Load balancer and registry carry no registered address, so any order does.
    local role n
    for role in loadbalancer registry; do
        while read -r n; do
            is_running "$n" || { log "starting $n"; k3d node start "$n" >/dev/null; }
        done < <(docker ps -a --filter "label=k3d.cluster=$CLUSTER" --filter "label=k3d.role=$role" \
                     --format '{{.Names}}')
    done
}

# Pods left in Terminating by an earlier stop (their node died before the delete
# finished) never go away on their own. Their data is hostPath, so forcing them
# loses nothing.
unstick_pods() {
    local ns p n=0
    while read -r ns p; do
        [ -n "$p" ] || continue
        kubectl delete pod -n "$ns" "$p" --force --grace-period=0 >/dev/null 2>&1 && n=$((n + 1))
    done < <(kubectl get pods -A --no-headers 2>/dev/null | awk '$4=="Terminating"{print $1" "$2}')
    [ $n -eq 0 ] || log "force-deleted $n pod(s) stuck in Terminating"
}

# Wait for every node to be Ready, or say why not.
wait_nodes_ready() {
    local want=${1:-4} waited=0 ready
    printf 'waiting for nodes '
    while :; do
        ready=$(kubectl get nodes --no-headers 2>/dev/null | grep -c ' Ready' || true)
        [ "$ready" -ge "$want" ] && { echo " ok"; return 0; }
        waited=$((waited + 3))
        if [ $waited -ge 180 ]; then
            echo
            warn "only $ready of $want nodes are Ready after 3 minutes."
            warn "registered addresses against the containers' own:"
            registered_ips 2>/dev/null | while read -r n a; do
                warn "  $n registered $a, container is on $(actual_ip "$n")"
            done
            warn "A mismatch means the order problem described in cluster-lib.sh;"
            warn "  stop.sh, then start.sh again, normally fixes it."
            return 1
        fi
        printf .; sleep 3
    done
}
