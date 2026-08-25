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

# The container's xkeyboard-config is older than the keysyms the default rules
# name (XF86CameraAccess*, XF86NavChart, ...), so xkbcomp prints a warning per
# unknown keysym every time a keymap is compiled — twice per start, once for
# Xvfb and once when x11vnc attaches. The X server says itself that they are not
# fatal. They are dropped by name so anything else Xvfb has to say still shows.
drop_xkb_noise() {
    grep --line-buffered -vE \
        '^(> Warning:[[:space:]]+Could not resolve keysym|The XKEYBOARD keymap compiler \(xkbcomp\) reports:|Errors from xkbcomp are not fatal)'
}

# -nolisten tcp: the X server is for this pod only and must not be reachable.
Xvfb :0 -screen 0 "${ZS1_SCREEN_W}x${ZS1_SCREEN_H}x24" -nolisten tcp \
     +extension GLX +extension RANDR +extension RENDER 2>&1 | drop_xkb_noise &

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

# Audio to the browser, on its own port.
#
# VNC carries no sound, so the SPU's output is encoded straight off the null
# sink's monitor and served as WebM/Opus. ffmpeg's http muxer with -listen 1
# serves exactly one client and exits when it disconnects, which is why this
# sits in a loop — a session has one player, and reconnecting should just work.
#
# This is NOT in sync with the picture: progressive HTTP is buffered by the
# browser, so expect the audio to trail the video by roughly a second. Putting
# both in one transport is the WebRTC work, not this.
if [ "${ZS1_AUDIO_STREAM:-1}" = "1" ]; then
    (
        while true; do
            # Every setting here trades bitrate or robustness for delay:
            # a 480-frame pulse fragment is 10 ms of capture, lowdelay plus a
            # 10 ms Opus frame keeps the encoder from looking ahead, and a
            # 40 ms cluster is the smallest that still muxes cleanly.
            ffmpeg -hide_banner -loglevel error -fflags nobuffer \
                   -f pulse -fragment_size 480 -i zs1.monitor \
                   -c:a libopus -b:a 96k -application lowdelay -frame_duration 10 \
                   -flush_packets 1 -cluster_time_limit 40 -max_delay 0 \
                   -content_type audio/webm -listen 1 -f webm \
                   "http://0.0.0.0:6081" || true
            sleep 1
        done
    ) &
    echo "[entrypoint] audio stream on :6081 (WebM/Opus, one listener)"
fi

echo "[entrypoint] bios=${ZS1_BIOS} game=${ZS1_GAME:-<none>} session=${ZS1_SESSION}"

if [ -n "$ZS1_GAME" ]; then
    exec /opt/zs1/ZoniStation_One "$ZS1_BIOS" --game="$ZS1_GAME" "$@"
else
    exec /opt/zs1/ZoniStation_One "$ZS1_BIOS" "$@"
fi
