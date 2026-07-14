import json
import os
import socket
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any, Dict, Optional, Tuple


# ============================================================
# 版本与服务端配置
# ============================================================

SERVER_VERSION = "v1.3-serial-utf8-command"
SERVER_HOST = os.environ.get("AGENT_SERVER_HOST", "0.0.0.0")
SERVER_PORT = int(os.environ.get("AGENT_SERVER_PORT", "8080"))

# HTTP 请求体最大字节数
MAX_BODY_SIZE = 2048

# 用户输入最大 UTF-8 字节数
MAX_INPUT_LENGTH = 512

# Agent 返回 msg 的最大字符数
MAX_AGENT_MSG_LENGTH = 64


# ============================================================
# DeepSeek 配置
# ============================================================

DEEPSEEK_API_URL = os.environ.get(
    "DEEPSEEK_API_URL",
    "https://api.deepseek.com/chat/completions",
)
DEEPSEEK_MODEL = os.environ.get("DEEPSEEK_MODEL", "deepseek-chat")
DEEPSEEK_TIMEOUT = float(os.environ.get("DEEPSEEK_TIMEOUT", "30"))


# ============================================================
# Tool 定义
# ============================================================

ALLOWED_ACTIONS = {
    "led_on",
    "led_off",
    "led_blink",
    "fan_on",
    "fan_off",
    "buzzer_on",
    "buzzer_off",
    "get_device_status",
    "none",
}

LED_BLINK_MIN_TIMES = 1
LED_BLINK_MAX_TIMES = 20

NONE_MESSAGE = "未识别到可执行的设备操作"


# ============================================================
# DeepSeek 中文 System Prompt
# ============================================================

SYSTEM_PROMPT = r"""
你是一个运行在 STM32 智能环境监测与控制终端中的设备控制 Agent。

你的任务是理解用户输入的中文或英文命令，并从已注册的 action 中选择且只选择一个动作。
你必须只返回一个合法的 JSON 对象，不能返回 Markdown、代码块、解释文字、标题、注释或任何前后缀。

允许使用的 action 只有：

1. led_on
   打开 LED 灯。

2. led_off
   关闭 LED 灯。

3. led_blink
   让 LED 闪烁指定次数。
   times 必须是 1 到 20 之间的整数。

4. fan_on
   打开风扇。

5. fan_off
   关闭风扇。

6. buzzer_on
   打开蜂鸣器或开始报警。

7. buzzer_off
   关闭蜂鸣器、停止报警或解除报警。

8. get_device_status
   查询 LED、风扇和蜂鸣器的当前状态。

9. none
   不执行任何设备动作。

必须遵守以下规则：

- action 必须继续使用上述英文名称，绝对不能返回中文 action。
- 每次请求只能选择一个 action。
- 不得创建、组合或猜测未注册的 action。
- 返回内容必须是一个纯 JSON 对象。
- 不得使用 Markdown。
- 不得使用 ```json 或其他代码块。
- JSON 前后不得包含任何解释、提示或附加文字。
- 对 led_blink，times 必须是 1 到 20 之间的整数。
- 对除 led_blink 以外的所有 action，times 必须为 0。
- msg 使用简短、清晰的中文说明。
- 如果用户命令无法识别、与设备无关、存在危险、含义不明确或请求的设备动作不受支持，必须返回 none。
- 返回 none 时，msg 必须为“未识别到可执行的设备操作”。

中文意图映射：

- “打开灯”“开灯”“打开LED”“开启LED”；或者按你自己的理解，比如“我觉得有点黑” -> led_on
- “关闭灯”“关灯”“关闭LED”；或者按你自己的理解，比如“我觉得有点亮” -> led_off
- “灯闪烁若干次”“让LED闪烁若干次”“LED闪几次” -> led_blink
- “打开风扇”“开启风扇”“开风扇”；或者按你自己的理解，比如“我觉得有点热”  -> fan_on
- “关闭风扇”“关风扇”；或者按你自己的理解，比如“我觉得有点冷”  -> fan_off
- “打开蜂鸣器”“开启蜂鸣器”“开始报警” -> buzzer_on
- “关闭蜂鸣器”“停止报警”“解除报警” -> buzzer_off
- “查询设备状态”“查看当前状态”“获取设备状态” -> get_device_status

英文意图也要正确识别，例如：

- Turn on the LED -> led_on
- Turn off the LED -> led_off
- Blink the LED 5 times -> led_blink，times 为 5
- Turn on the fan -> fan_on
- Turn off the fan -> fan_off
- Turn on the buzzer -> buzzer_on
- Stop the alarm -> buzzer_off
- Check device status -> get_device_status

返回格式必须严格保持为：

{
  "action": "fan_on",
  "times": 0,
  "msg": "风扇已开启"
}

示例：

用户：打开风扇
返回：{"action":"fan_on","times":0,"msg":"风扇已开启"}

用户：关闭LED
返回：{"action":"led_off","times":0,"msg":"LED已关闭"}

用户：让LED闪烁5次
返回：{"action":"led_blink","times":5,"msg":"LED将闪烁5次"}

用户：打开蜂鸣器
返回：{"action":"buzzer_on","times":0,"msg":"蜂鸣器已开启"}

用户：查询设备状态
返回：{"action":"get_device_status","times":0,"msg":"正在查询设备状态"}

用户：今天天气怎么样
返回：{"action":"none","times":0,"msg":"未识别到可执行的设备操作"}
""".strip()


# ============================================================
# Agent 响应校验与安全归一化
# ============================================================

def _clean_text(value: Any, fallback: str) -> str:
    """保留 UTF-8 中文文本，并限制返回消息长度。"""
    if not isinstance(value, str):
        value = ""

    # Python str 已是 Unicode；这里只移除首尾空白和控制字符。
    text = "".join(
        char for char in value.strip()
        if char in "\t\n\r" or ord(char) >= 32
    )
    text = text.replace("\r", " ").replace("\n", " ").replace("\t", " ")
    text = " ".join(text.split())

    if not text:
        text = fallback

    return text[:MAX_AGENT_MSG_LENGTH]


def make_safe_action(msg: str = "Agent 服务暂不可用") -> Dict[str, Any]:
    """返回不会触发任何硬件动作的安全响应。"""
    return {
        "action": "none",
        "times": 0,
        "msg": _clean_text(msg, "Agent 服务暂不可用"),
    }


def make_unrecognized_action() -> Dict[str, Any]:
    """返回统一的未识别或不支持动作响应。"""
    return {
        "action": "none",
        "times": 0,
        "msg": NONE_MESSAGE,
    }


def get_default_message(action: str, times: int) -> str:
    """在模型未提供有效 msg 时生成中文默认消息。"""
    messages = {
        "led_on": "LED已开启",
        "led_off": "LED已关闭",
        "fan_on": "风扇已开启",
        "fan_off": "风扇已关闭",
        "buzzer_on": "蜂鸣器已开启",
        "buzzer_off": "蜂鸣器已关闭",
        "get_device_status": "正在查询设备状态",
        "none": NONE_MESSAGE,
    }

    if action == "led_blink":
        return f"LED将闪烁{times}次"

    return messages.get(action, NONE_MESSAGE)


def normalize_agent_response(raw_result: Any) -> Dict[str, Any]:
    """校验模型输出，并归一化为 STM32 可解析的扁平 JSON。"""
    if not isinstance(raw_result, dict):
        print("[AGENT][ERROR] 模型结果不是 JSON 对象")
        return make_safe_action("模型返回格式无效")

    action = raw_result.get("action", "none")
    if not isinstance(action, str):
        print("[AGENT][ERROR] action 不是字符串")
        return make_safe_action("模型返回的 action 无效")

    action = action.strip().lower()
    if action not in ALLOWED_ACTIONS:
        print("[AGENT][ERROR] 不支持的 action:", action)
        return make_unrecognized_action()

    if action == "led_blink":
        raw_times = raw_result.get("times", LED_BLINK_MIN_TIMES)

        try:
            if isinstance(raw_times, bool):
                raise ValueError("bool 不能作为闪烁次数")
            times = int(raw_times)
        except (TypeError, ValueError):
            print("[AGENT][ERROR] 无效的闪烁次数:", repr(raw_times))
            times = LED_BLINK_MIN_TIMES

        times = max(LED_BLINK_MIN_TIMES, min(times, LED_BLINK_MAX_TIMES))
    else:
        # 除 led_blink 外，其他动作的 times 一律强制为 0。
        times = 0

    # 所有 none 响应统一使用固定文案，避免模型返回无关说明。
    if action == "none":
        msg = NONE_MESSAGE
    else:
        default_msg = get_default_message(action, times)
        msg = _clean_text(raw_result.get("msg", ""), default_msg)

    return {
        "action": action,
        "times": times,
        "msg": msg,
    }


def strip_markdown_code_fence(content: str) -> str:
    """兼容模型偶尔错误返回的 Markdown JSON 代码块。"""
    content = content.strip()
    if not content.startswith("```"):
        return content

    lines = content.splitlines()
    if lines and lines[0].strip().lower() in {"```", "```json"}:
        lines = lines[1:]
    if lines and lines[-1].strip() == "```":
        lines = lines[:-1]

    return "\n".join(lines).strip()


# ============================================================
# DeepSeek API 调用
# ============================================================

def call_deepseek(user_input: str) -> Dict[str, Any]:
    """调用 DeepSeek，将中英文自然语言转换为一个合法 action。"""
    if not isinstance(user_input, str):
        return make_safe_action("输入内容格式无效")

    user_input = user_input.strip()
    if not user_input:
        return make_safe_action("输入内容不能为空")

    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("[AGENT][ERROR] DEEPSEEK_API_KEY 未设置")
        return make_safe_action("API Key 未设置")

    request_data = {
        "model": DEEPSEEK_MODEL,
        "messages": [
            {
                "role": "system",
                "content": SYSTEM_PROMPT,
            },
            {
                "role": "user",
                "content": user_input,
            },
        ],
        "response_format": {
            "type": "json_object",
        },
        "stream": False,
    }

    # 向 DeepSeek 发送请求时保留真实 UTF-8 中文。
    request_body = json.dumps(
        request_data,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")

    request = urllib.request.Request(
        url=DEEPSEEK_API_URL,
        data=request_body,
        method="POST",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json; charset=utf-8",
            "Accept": "application/json",
        },
    )

    try:
        print("========== DeepSeek request ==========")
        print("Model:", DEEPSEEK_MODEL)
        print("User input:", user_input)
        print("======================================")

        with urllib.request.urlopen(request, timeout=DEEPSEEK_TIMEOUT) as response:
            http_status = response.getcode()
            response_bytes = response.read()

        if http_status != 200:
            print("[AGENT][ERROR] DeepSeek HTTP 状态码异常:", http_status)
            return make_safe_action("Agent 服务暂不可用")

        if not response_bytes:
            print("[AGENT][ERROR] DeepSeek 返回空响应")
            return make_safe_action("模型返回内容为空")

        response_text = response_bytes.decode("utf-8")
        response_json = json.loads(response_text)

        if not isinstance(response_json, dict):
            return make_safe_action("模型返回格式无效")

        choices = response_json.get("choices")
        if not isinstance(choices, list) or not choices:
            print("[AGENT][ERROR] choices 缺失或为空")
            return make_safe_action("模型返回格式无效")

        first_choice = choices[0]
        if not isinstance(first_choice, dict):
            return make_safe_action("模型返回格式无效")

        message = first_choice.get("message")
        if not isinstance(message, dict):
            print("[AGENT][ERROR] message 字段缺失")
            return make_safe_action("模型返回格式无效")

        content = message.get("content")
        if not isinstance(content, str) or not content.strip():
            print("[AGENT][ERROR] message.content 缺失或为空")
            return make_safe_action("模型返回内容为空")

        content = strip_markdown_code_fence(content)

        print("========== DeepSeek content ==========")
        print(content)
        print("======================================")

        # DeepSeek 外层响应中的 content 本身仍是 JSON 字符串，需要再次解析。
        raw_agent_result = json.loads(content)
        agent_result = normalize_agent_response(raw_agent_result)

        print("========== Agent response ==========")
        print(agent_result)
        print("====================================")

        return agent_result

    except urllib.error.HTTPError as exc:
        try:
            error_body = exc.read().decode("utf-8", errors="replace")
        except Exception:
            error_body = ""

        print("========== DeepSeek HTTP error ==========")
        print("Status:", exc.code)
        print("Reason:", exc.reason)
        if error_body:
            print("Body:", error_body[:500])
        print("=========================================")
        return make_safe_action("Agent 服务暂不可用")

    except (socket.timeout, TimeoutError) as exc:
        print("[AGENT][TIMEOUT]", repr(exc))
        return make_safe_action("Agent 服务请求超时")

    except urllib.error.URLError as exc:
        print("[AGENT][NETWORK ERROR]", repr(exc.reason))
        return make_safe_action("Agent 服务暂不可用")

    except UnicodeDecodeError as exc:
        print("[AGENT][UTF-8 ERROR]", repr(exc))
        return make_safe_action("模型响应不是有效的 UTF-8")

    except json.JSONDecodeError as exc:
        print("========== DeepSeek JSON error ==========")
        print("Message:", exc.msg)
        print("Line:", exc.lineno)
        print("Column:", exc.colno)
        print("========================================")
        return make_safe_action("模型返回的 JSON 无效")

    except Exception as exc:
        print("[AGENT][ERROR]", type(exc).__name__, repr(exc))
        return make_safe_action("Agent 服务暂不可用")


# ============================================================
# HTTP 请求处理器
# ============================================================

class AgentRequestHandler(BaseHTTPRequestHandler):
    server_version = "IOTAgentServer/1.3"

    def send_json_response(
        self,
        status_code: int,
        response_data: Dict[str, Any],
    ) -> None:
        """以 UTF-8 JSON 返回响应，并按编码后的字节数设置 Content-Length。"""
        response_body = json.dumps(
            response_data,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")

        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(response_body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(response_body)

    def read_json_body(
        self,
    ) -> Tuple[Optional[Dict[str, Any]], Optional[str], Optional[int]]:
        """读取 STM32 请求体，按 UTF-8 解码并解析 JSON。"""
        content_length_text = self.headers.get("Content-Length")
        if content_length_text is None:
            return None, "缺少 Content-Length", 400

        try:
            content_length = int(content_length_text)
        except ValueError:
            return None, "Content-Length 无效", 400

        if content_length <= 0:
            return None, "请求体为空", 400

        if content_length > MAX_BODY_SIZE:
            return None, "请求体过大", 413

        body_bytes = self.rfile.read(content_length)

        try:
            body_text = body_bytes.decode("utf-8")
        except UnicodeDecodeError as exc:
            print("[HTTP][UTF-8 ERROR]", repr(exc))
            return None, "请求体不是有效的 UTF-8 编码", 400

        print("========== POST received ==========")
        print("Path:", self.path)
        print("Body:")
        print(body_text)
        print("===================================")

        try:
            request_json = json.loads(body_text)
        except json.JSONDecodeError as exc:
            print("[HTTP][JSON ERROR]", repr(exc))
            return None, "请求体不是合法 JSON", 400

        if not isinstance(request_json, dict):
            return None, "JSON 根节点必须是对象", 400

        return request_json, None, None

    def do_GET(self) -> None:
        """GET /：服务运行状态检查。"""
        if self.path == "/":
            response_text = (
                "GET OK\n"
                "DeepSeek proxy server is running.\n"
                f"Version: {SERVER_VERSION}\n"
                "UTF-8 Chinese command support: enabled\n"
            )
            response_body = response_text.encode("utf-8")

            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(response_body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(response_body)
            return

        self.send_json_response(
            404,
            {
                "result": "error",
                "msg": "接口不存在",
            },
        )

    def do_POST(self) -> None:
        """处理 /api/test 和 /api/tool_result。"""
        if self.path not in {"/api/test", "/api/tool_result"}:
            self.send_json_response(
                404,
                {
                    "result": "error",
                    "msg": "接口不存在",
                },
            )
            return

        request_json, error_message, error_status = self.read_json_body()
        if error_message is not None or request_json is None:
            status_code = error_status or 400

            if self.path == "/api/test":
                response_data = make_safe_action(error_message or "请求无效")
            else:
                response_data = {
                    "result": "error",
                    "msg": error_message or "请求无效",
                }

            self.send_json_response(status_code, response_data)
            return

        # ----------------------------------------------------
        # POST /api/test
        # ----------------------------------------------------
        if self.path == "/api/test":
            user_input = request_json.get("input", "")

            if not isinstance(user_input, str):
                self.send_json_response(400, make_safe_action("input 必须是字符串"))
                return

            user_input = user_input.strip()
            if not user_input:
                self.send_json_response(400, make_safe_action("input 不能为空"))
                return

            # 按 UTF-8 编码后的字节长度限制输入，而不是按字符数限制。
            if len(user_input.encode("utf-8")) > MAX_INPUT_LENGTH:
                self.send_json_response(400, make_safe_action("input 内容过长"))
                return

            agent_result = call_deepseek(user_input)
            self.send_json_response(200, agent_result)
            return

        # ----------------------------------------------------
        # POST /api/tool_result
        # ----------------------------------------------------
        # 保持宽松接收，兼容 get_device_status 回传的
        # led、fan、buzzer 以及后续附加字段。
        print("========== Tool result ==========")
        for key, value in request_json.items():
            print(f"{key}: {value}")
        print("=================================")

        self.send_json_response(
            200,
            {
                "result": "ok",
                "msg": "Tool 执行结果已接收",
            },
        )

    def do_PUT(self) -> None:
        self.send_json_response(
            405,
            {
                "result": "error",
                "msg": "请求方法不允许",
            },
        )

    def do_DELETE(self) -> None:
        self.send_json_response(
            405,
            {
                "result": "error",
                "msg": "请求方法不允许",
            },
        )

    def log_message(self, format_string: str, *args: Any) -> None:
        print("[HTTP]", self.address_string(), "-", format_string % args)


# ============================================================
# 程序入口
# ============================================================

def run_server() -> None:
    print("========================================")
    print("IOT Agent DeepSeek proxy server")
    print("Version:", SERVER_VERSION)
    print("Host:", SERVER_HOST)
    print("Port:", SERVER_PORT)
    print("Model:", DEEPSEEK_MODEL)
    print("UTF-8 Chinese command support: enabled")
    print("========================================")

    if not os.environ.get("DEEPSEEK_API_KEY"):
        print("[WARNING] DEEPSEEK_API_KEY 未设置。")
        print("[WARNING] /api/test 将返回 action=none。")

    try:
        server = HTTPServer((SERVER_HOST, SERVER_PORT), AgentRequestHandler)
    except OSError as exc:
        print("[SERVER][ERROR] 无法绑定服务端地址:", repr(exc))
        raise

    print(f"Server running on http://{SERVER_HOST}:{SERVER_PORT}")
    print("STM32 should connect to http://192.168.1.100:8080")
    print("Press Ctrl+C to stop.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
    finally:
        server.server_close()
        print("Server stopped.")


if __name__ == "__main__":
    run_server()
