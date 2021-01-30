# Systemy Czasu Rzeczywistego
#
# Authors: 
#    Piotr Sawicki (https://github.com/psawicki0)
#    Marcin Białek (https://github.com/marcin-bialek)


import os 
import sys
import time
import subprocess
import eventlet
import socketio
from ipcqueue import posixmq
from ipcqueue.serializers import RawSerializer


sio = socketio.Server(async_mode = "eventlet")
app = socketio.WSGIApp(sio, static_files = {
    "/": sys.path[0] + "/public/index.html",
    "/index.html": sys.path[0] + "/public/index.html",
    "/res": sys.path[0] + "/public/res"
})

FREQUENCIES = [
    20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 
]
sliders_ids = { freq: i for i, freq in enumerate(FREQUENCIES) }
sliders_values = { freq: 0 for freq in FREQUENCIES }


@sio.event
def connect(sid, env):
    sio.emit("sliders_init", data = sliders_values, to = sid)


@sio.event
def freq_change(sid, data):
    for freq in data.keys():
        id = sliders_ids[int(freq)]
        val = int(data[freq])
        sliders_values[int(freq)] = val
        mq_settings.put(id.to_bytes(4, 'little', signed=True) + val.to_bytes(4, 'little', signed=True))

    sio.emit("freq_change", data = data, skip_sid = sid)


def try_rm(path):
    try: 
        os.remove(path)
    except OSError:
        ...


if __name__ == "__main__":
    try_rm("/tmp/log-capture")
    try_rm("/tmp/log-filter")
    try_rm("/tmp/log-playback")
    mq_settings = posixmq.Queue("/settings", maxsize = 10, maxmsgsize = 8, serializer = RawSerializer)
    proc_capture = subprocess.Popen(["build/capture"], stdout = sys.stdout, stderr = sys.stderr)
    proc_filter = subprocess.Popen(["build/filter"], stdout = sys.stdout, stderr = sys.stderr)
    proc_playback = subprocess.Popen(["build/playback"], stdout = sys.stdout, stderr = sys.stderr)

    time.sleep(1)

    if proc_capture.poll() is None and proc_filter.poll() is None and proc_playback.poll() is None:
        print("[web] running")
        eventlet.wsgi.server(eventlet.listen(("", 8080)), app, debug=False, log_output=False)

    proc_capture.kill()
    proc_filter.kill()
    proc_playback.kill()
    try_rm("/dev/mqueue/input")
    try_rm("/dev/mqueue/output")
    try_rm("/dev/mqueue/settings")
