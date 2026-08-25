#!/bin/sh
# Bring up a display and an audio sink, then hand over to the emulator.
set -eu

: "${ZS1_BIOS:=/mnt/zs1/roms/SCPH-7502.BIN}"
: "${ZS1_GAME:=}"
: "${ZS1_SESSION:=default}"
: "${ZS1_SCREEN_W:=1280}"
: "${ZS1_SCREEN_H:=720}"

# Per-session working directory. interconnect.c opens "memcard1.mcd" and
# "memcard2.mcd" by fixed name relative to the CWD, so two sessions started in
# the same directory would write each other's cards — and every boot rewrites
# the card as part of the driver's write test, so the damage is immediate.
WORK="/mnt/zs1/data/sessions/${ZS1_SESSION}"
mkdir -p "$WORK/savestates" "$WORK/logs"
cd "$WORK"

# -nolisten tcp: the X server is for this pod only and must not be reachable.
Xvfb :0 -screen 0 "${ZS1_SCREEN_W}x${ZS1_SCREEN_H}x24" -nolisten tcp \
     +extension GLX +extension RANDR +extension RENDER &

i=0
until xdpyinfo -display :0 >/dev/null 2>&1; do
    i=$((i + 1))
    if [ "$i" -gt 100 ]; then
        echo "[entrypoint] Xvfb did not come up on :0" >&2
        exit 1
    fi
    sleep 0.1
done
echo "[entrypoint] Xvfb ready: ${ZS1_SCREEN_W}x${ZS1_SCREEN_H}"

# A null sink is enough: nothing listens yet, but SDL needs a device to open or
# it warns and runs silent, and the SPU's pacing loop is tuned against a device
# that actually consumes samples. Failure here is not fatal.
if pulseaudio --daemonize=yes --exit-idle-time=-1 2>/dev/null; then
    pactl load-module module-null-sink sink_name=zs1 >/dev/null 2>&1 || true
    echo "[entrypoint] pulseaudio ready"
else
    echo "[entrypoint] pulseaudio unavailable — running silent" >&2
    export SDL_AUDIODRIVER=dummy
fi

# Browser view of the session.
#
# x11vnc binds to 127.0.0.1 only (-localhost): the RFB port is never reachable
# from outside the pod, and websockify — in the same pod, same loopback — is the
# only thing that talks to it. What is exposed is the HTTP port, and only as far
# as a Service. VNC carries no audio; that arrives with the WebRTC step.
if [ "${ZS1_VNC:-1}" = "1" ]; then
    x11vnc -display :0 -forever -shared -localhost -nopw -quiet \
           -rfbport 5900 -bg >/dev/null 2>&1 || echo "[entrypoint] x11vnc failed" >&2
    websockify --web=/usr/share/novnc 6080 127.0.0.1:5900 >/dev/null 2>&1 &
    echo "[entrypoint] noVNC on :6080 (vnc.html)"
fi

echo "[entrypoint] bios=${ZS1_BIOS} game=${ZS1_GAME:-<none>} session=${ZS1_SESSION}"

if [ -n "$ZS1_GAME" ]; then
    exec /opt/zs1/ZoniStation_One "$ZS1_BIOS" --game="$ZS1_GAME" "$@"
else
    exec /opt/zs1/ZoniStation_One "$ZS1_BIOS" "$@"
fi
