#!/usr/bin/env bash
# Set the basic-auth password of one or more sessions, or all of them.
#
#   ./deploy/session/set-password.sh acecombat            # a new random password
#   ./deploy/session/set-password.sh crash dino           # several sessions, one password each
#   ./deploy/session/set-password.sh all                  # every session
#   ./deploy/session/set-password.sh --prompt acecombat   # type the password instead
#   ./deploy/session/set-password.sh --dry-run all        # show what would happen, change nothing
#
# Options:  --length N     length of a generated password (default 20, minimum 12)
#           --user NAME    login to put in the secret (default zs1, or $USER_NAME)
#           --prompt       read the password from the terminal (asked twice, not echoed)
#
# Each session has its own secret, zs1-auth-<session>, which the Ingress reads
# through a Traefik basic-auth middleware, so one credential that leaks costs one
# session. Only an apr1 hash goes into the cluster: a password can be replaced but
# never read back, and this script is the only place it is shown, once, on the
# terminal. It is not written to a file, to the shell history, or to the process
# list. Put it in your password manager straight away.
#
# Traefik reads the secret again by itself; no pod is restarted and a viewer that
# is already connected keeps its stream until it reloads the page.
set -euo pipefail

CLUSTER=cluster-zs1
NS=zs1
SESSIONS=(acecombat crash dino)
USER_NAME=${USER_NAME:-zs1}
LENGTH=20
PROMPT=0
DRY=0
TARGETS=()

usage() { sed -n '2,21p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)   usage; exit 0 ;;
        --prompt)    PROMPT=1 ;;
        --dry-run)   DRY=1 ;;
        --length)    shift; LENGTH=${1:?--length needs a number} ;;
        --user)      shift; USER_NAME=${1:?--user needs a name} ;;
        all)         TARGETS=("${SESSIONS[@]}") ;;
        -*)          echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
        *)           TARGETS+=("$1") ;;
    esac
    shift
done

[ ${#TARGETS[@]} -gt 0 ] || { usage >&2; exit 2; }
case "$LENGTH" in ''|*[!0-9]*) echo "--length must be a number" >&2; exit 2 ;; esac
[ "$LENGTH" -ge 12 ] || { echo "--length below 12 is refused" >&2; exit 2; }
case "$USER_NAME" in *[!A-Za-z0-9._-]*|'') echo "--user may only use letters, digits . _ -" >&2; exit 2 ;; esac

for g in "${TARGETS[@]}"; do
    ok=0
    for s in "${SESSIONS[@]}"; do [ "$g" = "$s" ] && ok=1; done
    [ $ok -eq 1 ] || { echo "unknown session '$g' (known: ${SESSIONS[*]})" >&2; exit 2; }
done

command -v openssl >/dev/null || { echo "openssl is required" >&2; exit 1; }
command -v kubectl >/dev/null || { echo "kubectl is required" >&2; exit 1; }
KC=(kubectl --context "k3d-$CLUSTER" -n "$NS")

if [ $DRY -eq 0 ]; then
    "${KC[@]}" get namespace "$NS" >/dev/null 2>&1 \
        || { echo "cannot reach cluster $CLUSTER (is it running? ./deploy/session/start.sh)" >&2; exit 1; }
fi

# Letters and digits only: nothing here needs quoting in a URL, a shell or a
# htpasswd line. Reads from the kernel's random source, never from $RANDOM.
generate() {
    local out=""
    while [ ${#out} -lt "$LENGTH" ]; do
        out+=$(openssl rand -base64 48 | tr -dc 'A-Za-z0-9')
    done
    printf '%s' "${out:0:$LENGTH}"
}

read_password() {
    local a b
    read -rsp "password for $1: " a; echo >&2
    read -rsp "again: " b; echo >&2
    [ "$a" = "$b" ] || { echo "the two entries differ" >&2; return 1; }
    [ ${#a} -ge 12 ] || { echo "a password under 12 characters is refused" >&2; return 1; }
    printf '%s' "$a"
}

declare -a SHOWN=()
for g in "${TARGETS[@]}"; do
    if [ $PROMPT -eq 1 ]; then pass=$(read_password "$g"); else pass=$(generate); fi

    # The hash is what reaches the cluster. `openssl passwd -apr1 -stdin` keeps the
    # plaintext out of the process list, which `openssl passwd -apr1 "$pass"` would not.
    hash=$(printf '%s' "$pass" | openssl passwd -apr1 -stdin)
    secret=$(kubectl create secret generic "zs1-auth-$g" -n "$NS" \
                --from-literal=users="$USER_NAME:$hash" --dry-run=client -o yaml)

    if [ $DRY -eq 1 ]; then
        echo "dry run: would replace secret zs1-auth-$g (login $USER_NAME)"
    else
        printf '%s\n' "$secret" | "${KC[@]}" apply -f - >/dev/null
        echo "secret zs1-auth-$g updated"
    fi
    [ $PROMPT -eq 1 ] || SHOWN+=("$g|$pass")
    unset pass hash secret
done

if [ ${#SHOWN[@]} -gt 0 ]; then
    echo
    [ $DRY -eq 1 ] && echo "(dry run: these were generated and NOT applied)"
    echo "Shown once. Login is '$USER_NAME'. Copy it to your password manager now."
    printf '  %-10s %s\n' session password
    for row in "${SHOWN[@]}"; do printf '  %-10s %s\n' "${row%%|*}" "${row#*|}"; done
fi
