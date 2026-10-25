# A small tile server for trying out sources on your own machine.
# It answers with a grey 256 x 256 picture and prints every request it gets.
# Run:  python3 tileserver.py        (port 8765; the example sources use 127.0.0.1:8765, so keep this port)
import datetime
import json
import struct
import sys
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


ROW = b"\x00" + b"\x80" * 256
PNG = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 0, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(ROW * 256))
       + chunk(b"IEND", b""))

# Weather frames for the snapshot examples, taken once when the server starts.
NOW = datetime.datetime.now(datetime.timezone.utc).replace(second=0, microsecond=0)
NEWEST = NOW - datetime.timedelta(minutes=NOW.minute % 10)
OLDER = NEWEST - datetime.timedelta(minutes=10)
HOUR = NOW.replace(minute=0)


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


FRAMES = {"frames": [{"time": iso(OLDER)}, {"time": iso(NEWEST)}]}

CAPABILITIES = """<?xml version="1.0" encoding="UTF-8"?>
<WMS_Capabilities version="1.3.0" xmlns="http://www.opengis.net/wms">
  <Service><Name>WMS</Name><Title>Demo sea temperature</Title></Service>
  <Capability>
    <Layer>
      <Title>Demo</Title>
      <Layer queryable="0">
        <Name>sea-temp</Name>
        <Title>Sea temperature</Title>
        <Dimension name="time" units="ISO8601" default="%(default)s">%(start)s/%(end)s/PT1H</Dimension>
      </Layer>
    </Layer>
  </Capability>
</WMS_Capabilities>
""" % {"default": iso(HOUR + datetime.timedelta(hours=24)),
       "start": iso(HOUR - datetime.timedelta(days=2)),
       "end": iso(HOUR + datetime.timedelta(hours=24))}


class Handler(BaseHTTPRequestHandler):
    def send(self, status, ctype, body):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        print(self.path, flush=True)
        path = self.path.split("?")[0]
        if path == "/frames.json":
            self.send(200, "application/json", json.dumps(FRAMES).encode())
        elif path == "/token":
            self.send(200, "text/plain", b"abc123")
        elif path == "/caps.xml":
            self.send(200, "text/xml", CAPABILITIES.encode())
        elif path.startswith("/protected/") and "Referer" not in self.headers:
            self.send(403, "text/plain", b"forbidden")
        elif path.startswith("/keyed/") and "key=KEY" in self.path:
            self.send(401, "text/plain", b"invalid key")
        elif path.startswith("/lag/") and path.split("/")[2] == iso(NEWEST):
            # The newest frame is listed but not served yet.
            self.send(404, "text/plain", b"not yet")
        else:
            self.send(200, "image/png", PNG)

    def log_message(self, *args):
        pass


print("frames: %s and %s" % (iso(OLDER), iso(NEWEST)), flush=True)
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
