#!/usr/bin/env bash
# Add host rules for the addresses this machine actually has.
#
# The committed Ingress answers only on *.localhost, which is correct on the
# cluster host and useless from anywhere else: a phone resolving
# acecombat.localhost gets itself. Reaching a session from another device needs a
# name that carries this machine's address, and nip.io provides one without any
# DNS to run — <anything>.192.0.2.7.nip.io resolves to 192.0.2.7.
#
# Those addresses are read from the interfaces here and patched into the live
# Ingress rather than written into the manifest, so nobody's network layout ends
# up in the repository.
set -euo pipefail
NS=zs1

mapfile -t ADDRS < <(
  ip -4 -o addr show scope global 2>/dev/null \
    | awk '{split($4,a,"/"); print a[1]}' \
    | grep -vE '^(172\.1[6-9]\.|172\.2[0-9]\.|172\.3[01]\.|10\.4[23]\.|192\.168\.49\.)'
  tailscale ip -4 2>/dev/null || true
)
# The tailnet address is reported by both sources; dedupe, keeping order.
mapfile -t ADDRS < <(printf '%s\n' "${ADDRS[@]}" | awk '!seen[$0]++')

if [ "${#ADDRS[@]}" -eq 0 ]; then
  echo "no usable address found" >&2; exit 1
fi

for game in acecombat crash dino; do
  # Rebuild the rule list from the .localhost rule, which is the template: same
  # three backends, one host per address.
  base=$(kubectl get ingress -n "$NS" "zs1-$game" \
           -o jsonpath="{.spec.rules[?(@.host=='$game.localhost')]}")
  rules="[$base"
  for ip in "${ADDRS[@]}"; do
    rules+=",$(printf '%s' "$base" | sed "s/\"host\":\"$game.localhost\"/\"host\":\"$game.$ip.nip.io\"/")"
  done
  rules+="]"
  kubectl patch ingress -n "$NS" "zs1-$game" --type=json \
    -p "[{\"op\":\"replace\",\"path\":\"/spec/rules\",\"value\":$rules}]" >/dev/null
  echo "zs1-$game:"
  for ip in "${ADDRS[@]}"; do echo "  http://$game.$ip.nip.io:8081/webrtc.html"; done
done
