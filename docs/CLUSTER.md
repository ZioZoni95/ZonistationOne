# Running on a Kubernetes cluster

Moved out of `README.md`. The commands are unchanged; the last section collects traps found while bringing the cluster back up on 2026-10-03.

`deploy/` runs the emulator as a cluster workload: one pod per session, each with a GPU, its own
display and its own memory cards. It was built and verified against a local **k3d** cluster
(`cluster-zs1`, 1 server + 3 workers) on an RTX 4060.

```sh
./deploy/k3d-cuda/create-cluster.sh                 # cluster + GPU device plugin
kubectl apply -f deploy/k3d-cuda/zs1-storage.yaml   # namespace, disks, GPU budget
docker build -f deploy/session/Dockerfile -t zs1/session:dev .
k3d image import zs1/session:dev -c cluster-zs1     # node containerd cannot see the host's images
kubectl apply -f deploy/session/sessions.yaml       # three sessions, three different discs
kubectl port-forward -n zs1 svc/zs1-acecombat 6080:6080
```

## HTTPS, and reaching it from outside

The sessions are published on the tailnet with `tailscale serve`, which terminates TLS with a real
Let's Encrypt certificate and is reachable by tailnet members only — no port forwarding, no public
exposure, and nothing to accept in the browser:

```sh
sudo tailscale serve --bg --https=443   http://acecombat.localhost:8081
sudo tailscale serve --bg --https=8443  http://crash.localhost:8081
sudo tailscale serve --bg --https=10000 http://dino.localhost:8081
```

Three ports because `tailscale serve` offers exactly three for HTTPS, and each proxies to
`<game>.localhost:8081` so Traefik still routes by host and the rate-limit, headers and per-game auth
middlewares stay in the path.

**Enable HTTPS certificates in the tailnet first** (admin console, DNS page). Enabling them after
`tailscaled` has started is not enough on its own: it keeps serving a self-signed certificate from
before the change, and the proxy falls back to it silently rather than reporting anything. Run
`sudo tailscale cert <machine>.<tailnet>.ts.net` once — that path provisions explicitly and prints
the real error if something is wrong.

Another person does not need your account: share the machine from the admin console and they join
with their own free one, seeing that machine and nothing else in your tailnet.

## Starting and stopping

```sh
./deploy/session/start.sh    # cluster, relay, sessions, and the URLs to open
./deploy/session/stop.sh     # stops everything; deletes nothing
```

`stop.sh` stops, it does not delete. The node containers, their volumes, the
cluster's secrets and every memory card under `cluster-data/` survive it. `k3d cluster delete
cluster-zs1` is the destructive form, and it loses the basic-auth and TURN secrets with the cluster.

`start.sh` re-runs `expose.sh` each time rather than trusting the host rules already in the Ingress:
addresses change between sessions, and a rule naming yesterday's DHCP lease resolves to whatever
holds it today.

## Watching a session

Each session publishes three ports. Forward them together — the player page derives the audio port
from the one it is served on (`N` -> `N+1`), so mapping them as a block is what lets several sessions run
side by side:

```sh
kubectl port-forward -n zs1 svc/zs1-acecombat 6080:6080 6081:6081 6082:6082
kubectl port-forward -n zs1 svc/zs1-crash     6090:6080 6091:6081 6092:6082   # a second session
```

| Port | What |
|---|---|
| 6080 | noVNC (picture, and the pages below are served from here) |
| 6081 | audio, WebM/Opus over HTTP |
| 6082 | WebRTC signalling (WebSocket) |

Two ways in, and they are not equivalent:

- **`http://localhost:6080/webrtc.html`** — picture and sound in **one** WebRTC transport, H.264
  encoded on the GPU's NVENC block, keyboard forwarded. This is the one to use. Click *Connect*, then click the picture to
  give it focus before using the keys. The bar shows measured round-trip time, frame rate, bitrate
  and jitter, so latency is read rather than guessed. Add `?sig=6092` for the second session.
- **`http://localhost:6080/play.html`** — the older VNC path: noVNC for the picture, a separate HTTP
  audio stream, an *Enable audio* button. Kept as the fallback, because it depends on nothing but
  x11vnc.

## Through the Ingress

`deploy/session/ingress.yaml` publishes the sessions on one port with Traefik, one hostname each,
behind a rate limit, security headers and basic auth. Each session has its own credential, so one
that leaks costs one session rather than all of them — create them before applying:

```sh
for game in acecombat crash dino; do
  read -rsp "password for $game: " pass; echo
  kubectl create secret generic "zs1-auth-$game" -n zs1 \
    --from-literal=users="$USER_NAME:$(openssl passwd -apr1 "$pass")"
done
kubectl apply -f deploy/session/ingress.yaml
```

Set `USER_NAME` to whatever login you want; it is the same for every session and appears only in the
secret. No credential is stored in this repository — a committed htpasswd hash is a committed
credential — so they live in the cluster and in your password manager, nowhere else. `kubectl delete
cluster` takes them with it and they have to be recreated.

| | |
|---|---|
| `http://acecombat.localhost:8081/webrtc.html` | Ace Combat |
| `http://crash.localhost:8081/webrtc.html` | Crash |

`8081` is where `create-cluster.sh` maps the cluster's port 80; `*.localhost` resolves to 127.0.0.1
without touching `/etc/hosts`. `play.html` works on the same hosts.

**Basic auth is not optional here.** Until this point a session was ClusterIP-only, so reaching one
needed cluster credentials and the read-only BIOS and disc mounts were unreachable from any network.
An Ingress removes that: without the middleware, anyone who can route to the host can drive the
emulated machine. The credential is deliberately *not* in the manifest — a committed htpasswd hash
is a committed credential — so the secret is created separately, as above.

Routing is by host rather than by path because noVNC loads its assets from absolute paths, and a
`/acecombat/` prefix would break every one of them. Each host carries three backends: `/ws` for the
WebRTC signalling socket, `/audio` for the HTTP audio stream, and `/` for noVNC and the pages. The
player pages detect which way they were reached — a path behind the Ingress, a port under
`port-forward` — so both routes work from the same file.

The stream runs at **50 fps**, matching a PAL field. That is a cadence, not a throughput target:
above it the encoder sends duplicate frames, below it real ones are dropped. `ZS1_WEBRTC_FPS` and
`ZS1_WEBRTC_BITRATE_KBPS` override it, `ZS1_WEBRTC=0` and `ZS1_VNC=0` switch either path off.

The pipeline is only built once a viewer connects, so an idle session costs nothing. One viewer at a
time: a second connection replaces the first rather than being multiplexed.

**On the VNC path, audio and picture are not synchronised** — that is what WebRTC is for. VNC carries no sound, so the SPU's output is encoded off
a PulseAudio null sink as WebM/Opus and served on a second port — two transports with nothing tying
their clocks together. What dominates the gap is the browser's own media buffer, which grows without
bound on a progressive stream, so `play.html` chases the live edge: small drift is absorbed by
playing 5% fast, a large one by seeking, and the page shows the measured lag. It lands in the low
hundreds of milliseconds rather than the second-plus it settles at untouched. Real synchronisation
means one transport carrying both, which is the WebRTC work.

---

## Traps when bringing the cluster back up

Each of these cost time on 2026-10-03; none is a code bug.

- **Build the image from a clean tree.** `docker build` runs `COPY . /src`, which carries the host's
  `.o` files into the image, and the container's gcc differs from the host's. The link fails with
  `bytecode stream in file '...' generated with LTO version 13.1 instead of the expected 14.0`. Run
  `make clean` before `docker build`.
- **Import the image only when every node is `Ready`.** `k3d image import` reported success while the
  agents were `NotReady`, and the pods kept running the previous binary. Check what a pod actually
  runs: `kubectl -n zs1 exec deploy/zs1-acecombat -c emulator -- grep -c ZS1_DMA_STALL /opt/zs1/ZoniStation_One`
  prints `0` on a binary older than the 2026-10-02 work. Re-import and
  `kubectl -n zs1 rollout restart deployment/<name>`.
- **Agents stay `NotReady` after `k3d cluster start`, with the old pods stuck `Terminating`.** The
  agent log says `Failed to start networking: ... failed to find interface with specified node ip`.
  Docker hands out the lowest free address in start order, so the agents come back on different IPs
  than the ones they registered with. Compare `kubectl get nodes -o wide` with
  `docker inspect` on each `k3d-cluster-zs1-agent-N`, stop the agents, start them one at a time in the
  order that restores the registered addresses, then force-delete the stale pods
  (`kubectl delete pod --force --grace-period=0`). Their data is hostPath, so nothing is lost.
  `start.sh` waits for four `Ready` nodes and looks like a hang until this is fixed.
- **`port-forward` is not enough for WebRTC.** The signalling and the page load, but the media path
  stays on "connecting". Use the Ingress (or the tailnet) with the session's basic-auth credential.
- **The system Docker daemon must be running.** Docker Desktop's daemon has no NVIDIA passthrough;
  the scripts run `docker context use default` for that reason.
