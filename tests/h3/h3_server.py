#!/usr/bin/env python3
"""Minimal HTTP/3 test server on aioquic (independent QUIC/TLS/H3 implementation) for
tests/test_quic_client.c.  usage: h3_server.py <port> <cert.pem> <key.pem>
Serves: /          -> 200 text/plain "hello from aioquic h3"
        /big       -> 200, 200000 bytes of a repeating pattern (multi-packet, out-of-order capable)
        /echo-ua   -> 200, body = the request's user-agent header
        /missing   -> 404
"""
import asyncio, sys, os, logging
if os.environ.get('H3_DEBUG'): logging.basicConfig(level=logging.DEBUG)
from aioquic.asyncio import serve, QuicConnectionProtocol
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import HeadersReceived
from aioquic.quic.configuration import QuicConfiguration

class Srv(QuicConnectionProtocol):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.h3 = None
    def quic_event_received(self, event):
        if self.h3 is None:
            self.h3 = H3Connection(self._quic)
        for e in self.h3.handle_event(event):
            if isinstance(e, HeadersReceived):
                h = dict(e.headers)
                path = h.get(b":path", b"/").decode()
                ua = h.get(b"user-agent", b"")
                if path == "/big":
                    body = (b"0123456789abcdef" * 12500)
                elif path == "/echo-ua":
                    body = ua
                elif path == "/missing":
                    self.h3.send_headers(e.stream_id, [(b":status", b"404")])
                    self.h3.send_data(e.stream_id, b"nope", end_stream=True)
                    self.transmit(); return
                else:
                    body = b"hello from aioquic h3"
                self.h3.send_headers(e.stream_id, [(b":status", b"200"), (b"content-type", b"text/plain"),
                                                   (b"server", b"aioquic-test")])
                self.h3.send_data(e.stream_id, body, end_stream=True)
                self.transmit()

async def main(port, cert, key):
    cfg = QuicConfiguration(is_client=False, alpn_protocols=H3_ALPN)
    cfg.load_cert_chain(cert, key)
    await serve("127.0.0.1", port, configuration=cfg, create_protocol=Srv)
    print("READY", flush=True)
    await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main(int(sys.argv[1]), sys.argv[2], sys.argv[3]))
