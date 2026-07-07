from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import re

class PostHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"<h1>GET OK</h1><p>Local AI Agent mock server is running.</p>"

        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length)
        body_text = post_data.decode("utf-8", errors="ignore")

        print("========== POST received ==========")
        print("Path:", self.path)
        print("Headers:")
        print(self.headers)
        print("Body:")
        print(body_text)
        print("===================================")

        try:
            req = json.loads(body_text)
            user_input = req.get("input", "")
        except Exception:
            user_input = ""

        action = "none"
        times = 0
        msg = "no valid action"

        if "LED" in user_input or "led" in user_input:
            action = "led_blink"
            times = 3
            msg = "agent decide to blink led 3 times"

            match = re.search(r"(\d+)", user_input)
            if match:
                times = int(match.group(1))

        response_obj = {
            "action": action,
            "times": times,
            "msg": msg
        }

        response_body = json.dumps(response_obj).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)

if __name__ == "__main__":
    server_ip = "192.168.1.100"
    server_port = 8080

    httpd = HTTPServer((server_ip, server_port), PostHandler)
    print(f"Serving local AI Agent mock server on {server_ip}:{server_port} ...")
    httpd.serve_forever()
    