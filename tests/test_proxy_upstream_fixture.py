import http.server

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"real-upstream-ok")
    def log_message(self, fmt, *args):
        pass

s = http.server.HTTPServer(("127.0.0.1", 18099), H)
s.serve_forever()
