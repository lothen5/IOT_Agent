from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
import urllib.request
import urllib.error

DEEPSEEK_URL = "https://api.deepseek.com/chat/completions"

def call_deepseek(user_input):
    api_key = os.environ.get("DEEPSEEK_API_KEY")

    if not api_key:
        return {
            "action": "none",
            "times": 0,
            "msg": "DEEPSEEK_API_KEY not set"
        }

    payload = {
        "model": "deepseek-v4-flash",
        "messages": [
            {
                "role": "system",
                "content": (
                    "你是一个嵌入式设备控制Agent。"
                    "你只能返回JSON，不要返回解释。"
                    "JSON格式必须是："
                    "{\"action\":\"led_blink 或 none\",\"times\":数字,\"msg\":\"简短说明\"}。"
                    "如果用户要求LED闪烁，就返回action为led_blink，并从用户话语中提取闪烁次数。"
                    "如果无法判断，就返回action为none，times为0。"
                )
            },
            {
                "role": "user",
                "content": user_input
            }
        ],
        "response_format": {
            "type": "json_object"
        },
        "thinking": {
            "type": "disabled"
        },
        "stream": False
    }

    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")

    req = urllib.request.Request(
        DEEPSEEK_URL,
        data=data,
        headers={
            "Content-Type": "application/json",
            "Authorization": "Bearer " + api_key
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            resp_text = resp.read().decode("utf-8", errors="ignore")

        obj = json.loads(resp_text)
        content = obj["choices"][0]["message"]["content"]

        action_obj = json.loads(content)

        return {
            "action": str(action_obj.get("action", "none")),
            "times": int(action_obj.get("times", 0)),
            "msg": str(action_obj.get("msg", ""))
        }

    except Exception as e:
        return {
            "action": "none",
            "times": 0,
            "msg": "deepseek call failed: " + str(e)
        }


class PostHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"<h1>GET OK</h1><p>DeepSeek proxy server is running.</p>"

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
        print("Body:")
        print(body_text)
        print("===================================")

        try:
            req_obj = json.loads(body_text)
            user_input = req_obj.get("input", "")
        except Exception:
            user_input = ""

        response_obj = call_deepseek(user_input)

        print("========== Agent response ==========")
        print(response_obj)
        print("====================================")

        response_body = json.dumps(response_obj, ensure_ascii=False).encode("utf-8")

        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)


if __name__ == "__main__":
    server_ip = "192.168.1.100"
    server_port = 8080

    httpd = HTTPServer((server_ip, server_port), PostHandler)
    print(f"Serving DeepSeek proxy server on {server_ip}:{server_port} ...")
    httpd.serve_forever()


