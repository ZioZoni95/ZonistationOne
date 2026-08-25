#!/usr/bin/env python3
"""WebRTC transport for one emulator session: video, audio and keyboard.

Why this exists beside the VNC path rather than on top of it: VNC polls the X
framebuffer on the CPU and carries no sound, so the picture and the audio ended
up on two transports with no shared clock and no way to synchronise them. A
single webrtcbin carries both with RTP timestamps that do relate, encodes H.264
on the NVENC block of a GPU that is otherwise idle, and is built for real time
rather than for buffering.

Deliberately single-peer: a session has one player. A second connection replaces
the first rather than being multiplexed, which keeps the pipeline and the
signalling state trivial.
"""
import asyncio
import json
import os
import subprocess
import sys
import threading

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstWebRTC", "1.0")
gi.require_version("GstSdp", "1.0")
from gi.repository import Gst, GstWebRTC, GstSdp, GLib  # noqa: E402

import websockets  # noqa: E402

FPS      = int(os.environ.get("ZS1_WEBRTC_FPS", "50"))
BITRATE  = int(os.environ.get("ZS1_WEBRTC_BITRATE_KBPS", "12000"))
PORT     = int(os.environ.get("ZS1_WEBRTC_PORT", "6082"))
DISPLAY  = os.environ.get("DISPLAY", ":0")
SINK_MON = os.environ.get("ZS1_PULSE_MONITOR", "zs1.monitor")

# nvcudah264enc, not nvh264enc.
#
# Both are present and both claim the GPU at registration time, but the older
# element drives the NVENC preset API that NVIDIA removed from the encoder SDK,
# so against a 610-series driver it registers, accepts the pipeline, and then
# fails to configure a session with "Selected preset not supported" — the stream
# never negotiates and the browser sits on "connecting". The newer element uses
# the current tune/rate-control API and works.
#
# openh264enc is the escape hatch for a machine with no usable NVENC; it costs
# CPU and latency, and is not the default for that reason.
ENCODER = os.environ.get("ZS1_WEBRTC_ENCODER", "nvcudah264enc")
if ENCODER == "nvcudah264enc":
    ENC = (f"nvcudah264enc name=venc bitrate={{BITRATE}} gop-size={{FPS}} "
           f"rate-control=cbr tune=ultra-low-latency zero-reorder-delay=true b-frames=0")
elif ENCODER == "openh264enc":
    ENC = "openh264enc name=venc bitrate={BITRATE}000 complexity=low"
else:
    ENC = ENCODER + " name=venc"

# 50 fps is not a throughput choice, it is the machine's own cadence: a PAL field
# is 20 ms, so anything above it transmits duplicate frames and anything below it
# drops real ones. gop-size follows at one keyframe a second.
#
# The queues are one buffer deep and leaky on the capture side. A deeper queue
# would smooth a stall by adding delay, which is the opposite of what this is
# for: when the encoder falls behind, the right answer is to drop the frame that
# is already stale, not to show it late.
PIPELINE = f"""
webrtcbin name=sendrecv bundle-policy=max-bundle latency=0

ximagesrc display-name={DISPLAY} use-damage=0 show-pointer=false
  ! video/x-raw,framerate={FPS}/1
  ! queue max-size-buffers=1 leaky=downstream
  ! videoconvert
  ! {ENC.format(BITRATE=BITRATE, FPS=FPS)}
  ! h264parse config-interval=-1
  ! rtph264pay pt=96 config-interval=-1 aggregate-mode=zero-latency
  ! queue max-size-buffers=1 max-size-time=0 max-size-bytes=0
  ! sendrecv.

pulsesrc device={SINK_MON} provide-clock=false
  ! audio/x-raw,channels=2,rate=48000
  ! queue max-size-buffers=2 leaky=downstream
  ! audioconvert ! audioresample
  ! opusenc bitrate=96000 frame-size=10 audio-type=restricted-lowdelay
  ! rtpopuspay pt=97
  ! queue max-size-buffers=2
  ! sendrecv.
"""


def log(*a):
    print("[webrtc]", *a, file=sys.stderr, flush=True)


class Session:
    def __init__(self, loop):
        self.loop = loop
        self.ws = None
        self.pipe = None
        self.webrtc = None

    # -- signalling out: called from the GLib thread, delivered on the asyncio one
    def _send(self, obj):
        ws = self.ws
        if ws is None:
            return
        asyncio.run_coroutine_threadsafe(ws.send(json.dumps(obj)), self.loop)

    def start(self):
        self.stop()
        self.pipe = Gst.parse_launch(PIPELINE)
        self.webrtc = self.pipe.get_by_name("sendrecv")
        self.webrtc.connect("on-negotiation-needed", self._on_negotiation_needed)
        self.webrtc.connect("on-ice-candidate", self._on_ice_candidate)
        bus = self.pipe.get_bus()
        bus.add_signal_watch()
        bus.connect("message::error", self._on_error)
        bus.connect("message::warning",
                    lambda _b, m: log("warning:", m.parse_warning()[0].message))
        bus.connect("message::state-changed", self._on_state)
        rc = self.pipe.set_state(Gst.State.PLAYING)
        log(f"set_state(PLAYING) -> {rc.value_nick}; {FPS} fps, {BITRATE} kbps, {ENCODER}")

    def _on_state(self, _bus, msg):
        if msg.src is self.pipe:
            old, new, _ = msg.parse_state_changed()
            log(f"pipeline {old.value_nick} -> {new.value_nick}")

    def stop(self):
        if self.pipe is not None:
            self.pipe.set_state(Gst.State.NULL)
            self.pipe = None
            self.webrtc = None

    def _on_error(self, _bus, msg):
        err, dbg = msg.parse_error()
        log("pipeline error:", err.message, "|", dbg)

    def _on_negotiation_needed(self, element):
        log("negotiation needed")

        # The reply is handled in a closure with exactly one user-data slot.
        # Passing two — the pattern in the older GStreamer examples — is silently
        # rejected by this binding: negotiation fired, create-offer ran, and the
        # callback simply never executed, so no offer was ever sent and the
        # browser sat waiting until it gave up with "signalling closed".
        #
        # Everything here is wrapped, because an exception raised inside a
        # GStreamer callback does not reach the interpreter's handler and would
        # vanish the same way.
        def on_offer(promise, _user_data=None):
            try:
                promise.wait()
                reply = promise.get_reply()
                offer = reply.get_value("offer") if reply is not None else None
                if offer is None or offer.sdp is None:
                    log("create-offer produced no SDP")
                    return
                # Read the text before handing the description over:
                # set-local-description takes ownership of the GstSDPMessage and
                # leaves offer.sdp None behind it.
                sdp_text = offer.sdp.as_text()
                log("offer created:", len(sdp_text), "bytes,",
                    sum(1 for l in sdp_text.splitlines() if l.startswith("m=")), "media")
                element.emit("set-local-description", offer, Gst.Promise.new())
                self._send({"sdp": {"type": "offer", "sdp": sdp_text}})
            except Exception as e:
                log("offer failed:", type(e).__name__, e)

        element.emit("create-offer", None, Gst.Promise.new_with_change_func(on_offer, None))

    def _on_ice_candidate(self, _element, mline, candidate):
        self._send({"ice": {"candidate": candidate, "sdpMLineIndex": mline}})

    # -- signalling in
    def on_answer(self, sdp_text):
        _res, sdpmsg = GstSdp.SDPMessage.new_from_text(sdp_text)
        answer = GstWebRTC.WebRTCSessionDescription.new(
            GstWebRTC.WebRTCSDPType.ANSWER, sdpmsg)
        self.webrtc.emit("set-remote-description", answer, Gst.Promise.new())

    def on_ice(self, ice):
        self.webrtc.emit("add-ice-candidate", ice["sdpMLineIndex"], ice["candidate"])


# Keyboard goes to the X server through xdotool rather than through the
# emulator: SDL is already reading the X input queue, so a synthetic key is
# indistinguishable from a real one and nothing in the emulator has to change.
def send_key(action, keysym):
    if not keysym or len(keysym) > 32 or not keysym.replace("_", "").isalnum():
        return
    subprocess.run(["xdotool", "key" + ("down" if action == "down" else "up"), keysym],
                   env={**os.environ, "DISPLAY": DISPLAY},
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


async def handler(ws, session):
    if session.ws is not None:
        log("second viewer connected — replacing the first")
        try:
            await session.ws.close()
        except Exception:
            pass
    session.ws = ws
    session.start()
    try:
        async for raw in ws:
            msg = json.loads(raw)
            if "sdp" in msg:
                session.on_answer(msg["sdp"]["sdp"])
            elif "ice" in msg:
                session.on_ice(msg["ice"])
            elif "key" in msg:
                send_key(msg["key"]["action"], msg["key"]["sym"])
    except websockets.ConnectionClosed:
        pass
    finally:
        if session.ws is ws:
            session.ws = None
            session.stop()
            log("viewer gone — pipeline stopped")


async def main():
    Gst.init(None)

    # GStreamer's signals need a GLib main loop; websockets needs an asyncio one.
    # They run side by side, and the only crossing is _send(), which hands the
    # message to asyncio from the GLib thread.
    glib_loop = GLib.MainLoop()
    threading.Thread(target=glib_loop.run, daemon=True).start()

    session = Session(asyncio.get_running_loop())
    async with websockets.serve(lambda ws: handler(ws, session), "0.0.0.0", PORT,
                                ping_interval=20, ping_timeout=20):
        log(f"signalling on :{PORT}")
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
