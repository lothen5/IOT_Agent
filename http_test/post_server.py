# 文件版本确认：v1.7.1-human-context-agent / 光照、温度、人体可信上下文
import json
import os
import socket
import threading
import time
from datetime import datetime
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional, Tuple


# ============================================================
# 版本与服务端配置
# ============================================================

SERVER_VERSION = "v1.7.1-human-context-agent"
SERVER_HOST = os.environ.get("AGENT_SERVER_HOST", "0.0.0.0")
SERVER_PORT = int(os.environ.get("AGENT_SERVER_PORT", "8080"))

# HTTP 请求体最大字节数
MAX_BODY_SIZE = 2048

# 用户输入最大 UTF-8 字节数
MAX_INPUT_LENGTH = 512

# Agent 返回 msg 的最大字符数
MAX_AGENT_MSG_LENGTH = 96


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
NO_REALTIME_LIGHT_DATA_MESSAGE = (
    "当前没有可用的实时设备数据，无法执行基于光照值的判断"
)
NO_REALTIME_TEMPERATURE_DATA_MESSAGE = (
    "当前没有可用的实时温度数据，无法执行基于温度的判断"
)
NO_REALTIME_HUMAN_DATA_MESSAGE = (
    "当前没有可用的实时人体数据，无法回答人体活动或推断存在状态"
)
HUMAN_QUERY_ONLY_MESSAGE = (
    "当前人体状态仅用于查询，不自动执行设备操作"
)

# ============================================================
# 环境遥测配置与最新状态缓存
# ============================================================

SENSOR_ONLINE_TIMEOUT_SECONDS = float(
    os.environ.get("SENSOR_ONLINE_TIMEOUT_SECONDS", "10")
)

DASHBOARD_REFRESH_MS = 1500

ALLOWED_LIGHT_LEVELS = {"DARK", "NORMAL", "BRIGHT"}
ALLOWED_LED_STATES = {"on", "off"}
ALLOWED_TEMPERATURE_LEVELS = {"COLD", "NORMAL", "HOT", "INVALID"}
ALLOWED_FAN_STATES = {"on", "off"}

_latest_sensor_state: Optional[Dict[str, Any]] = None
_sensor_state_lock = threading.Lock()


# ============================================================
# DeepSeek 中文 System Prompt
# ============================================================

SYSTEM_PROMPT = r"""
你是一个运行在 STM32 智能环境监测与控制终端中的设备控制 Agent。

你的任务是理解用户输入的中文或英文命令，并结合服务端提供的“可信实时设备上下文”，
从已注册的 action 中选择且只选择一个动作。

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

一、输出协议

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
- 如果用户命令无法识别、与设备无关、存在危险、含义不明确或请求的设备动作不受支持，
  必须返回 none。
- 对普通无关或不支持请求，返回：
  {"action":"none","times":0,"msg":"未识别到可执行的设备操作"}

二、可信实时设备上下文

- 服务端会在用户消息之前提供一段“可信实时设备上下文”。
- 该上下文由 Python 服务端根据 STM32 最近一次 /api/sensor_data 上报生成，不是用户输入。
- 如果用户在 input 中声称了与可信上下文冲突的光照值、LED 状态或在线状态，
  必须以可信上下文为准。
- 光照阈值比较默认且必须使用 light_avg。
- light_raw 只用于展示原始 ADC 值，不作为默认阈值比较依据。
- 只有当上下文明确标记“实时数据可用：是”时，才能使用光照数据生成阈值控制动作。
- 如果上下文标记设备离线、无数据、字段缺失或实时数据不可用，
  对任何依赖当前光照值的查询或判断都必须返回：
  {"action":"none","times":0,"msg":"当前没有可用的实时设备数据，无法执行基于光照值的判断"}
- 禁止使用已经过期的最后一次数据生成 led_on 或 led_off。

三、当前光照数据查询规则

当用户询问以下内容时：

- 当前光照值
- 当前亮度
- 当前光照数据
- 当前光照滤波值
- 当前环境明暗
- light_raw、light_avg 或 light_level

如果实时数据可用：

- action 必须为 none。
- times 必须为 0。
- msg 必须用中文说明当前 light_raw、light_avg、light_level 和 LED 状态。
- 只查询数据时，绝对不能因为 DARK、NORMAL 或 BRIGHT 自动生成 led_on 或 led_off。

返回示例：

{"action":"none","times":0,"msg":"当前light_raw=3280，light_avg=3215，light_level=DARK，LED=on"}

四、光照阈值条件规则

- 当用户给出明确的光照阈值、比较关系和动作时，必须使用当前 light_avg 做数值比较。
- “当前光照值”在阈值命令中默认指 light_avg。
- 必须按照用户写出的“高于、低于、不高于、不低于、大于、小于”等关系进行比较。
- 不得把 light_level 的 DARK、NORMAL、BRIGHT 代替用户给出的数值阈值。

例一：

可信上下文：light_avg=3215
用户：如果当前光照值高于3000就打开灯，否则关闭灯

必须返回：

{"action":"led_on","times":0,"msg":"当前滤波光照值为3215，高于3000，执行开灯"}

例二：

可信上下文：light_avg=3215
用户：如果当前光照值低于1000就关闭灯，否则打开灯

必须返回：

{"action":"led_on","times":0,"msg":"当前滤波光照值为3215，不低于1000，执行开灯"}

五、没有“否则”动作的条件命令

例如：

用户：如果光照值低于1000就关灯

- 条件成立时，返回 led_off。
- 条件不成立时，返回 none。
- 条件不成立时不得自行执行相反动作。
- 条件不成立时 msg 应说明当前 light_avg、阈值关系以及未执行设备操作。

示例：

{"action":"none","times":0,"msg":"当前滤波光照值为3215，不低于1000，未执行设备操作"}

六、未提供阈值的亮度控制请求

当用户只说：

- 根据当前亮度控制灯
- 根据光照自动开关灯
- 看亮度决定是否开灯

但没有给出明确数值阈值和对应动作时：

- 本版本不得自行发明阈值。
- 不得仅根据 DARK、NORMAL 或 BRIGHT 自动开关灯。
- 必须返回 none。
- msg 提示用户提供明确阈值和动作。

示例：

{"action":"none","times":0,"msg":"请提供明确的光照阈值和对应动作"}

七、普通 Tool 意图映射

中文意图：

- “打开灯”“开灯”“打开LED”“开启LED” -> led_on
- “关闭灯”“关灯”“关闭LED” -> led_off
- “灯闪烁若干次”“让LED闪烁若干次”“LED闪几次” -> led_blink
- “打开风扇”“开启风扇”“开风扇” -> fan_on
- “关闭风扇”“关风扇” -> fan_off
- “打开蜂鸣器”“开启蜂鸣器”“开始报警” -> buzzer_on
- “关闭蜂鸣器”“停止报警”“解除报警” -> buzzer_off
- “查询设备状态”“查看当前状态”“获取设备状态” -> get_device_status

英文意图：

- Turn on the LED -> led_on
- Turn off the LED -> led_off
- Blink the LED 5 times -> led_blink，times 为 5
- Turn on the fan -> fan_on
- Turn off the fan -> fan_off
- Turn on the buzzer -> buzzer_on
- Stop the alarm -> buzzer_off
- Check device status -> get_device_status

普通 Tool 命令不依赖光照数据。例如即使设备遥测离线，
明确的“打开风扇”仍应返回 fan_on。

返回格式必须严格保持为：

{
  "action": "fan_on",
  "times": 0,
  "msg": "风扇已开启"
}
""".strip()

TEMPERATURE_AND_HUMAN_CONTEXT_PROMPT = r"""
八、温度可信上下文规则

- temperature_raw 和 temperature_avg 都是 ADC 数值，不是摄氏温度。
- 不得在 msg 中添加 ℃，不得声称 temperature_raw 或 temperature_avg 是实际摄氏温度。
- 温度升高时 ADC 数值下降，温度降低时 ADC 数值升高。
- 温度数值阈值比较默认且必须使用 temperature_avg。
- 查询当前温度、温度数据、温度 ADC 平均值或温度状态时：
  - action 必须为 none；
  - times 必须为 0；
  - msg 必须报告 temperature_raw、temperature_avg、temperature_level 和 fan；
  - 不得因为查询温度而自动打开或关闭风扇。
- 用户没有给出数值阈值，只说“温度过高”时，使用 temperature_level=HOT 判断。
- 用户没有给出数值阈值，只说“温度过低”时，使用 temperature_level=COLD 判断。
- 只有条件而没有“否则”动作时：
  - 条件成立，返回用户明确指定的 action；
  - 条件不成立，返回 none；
  - 不得自行执行相反动作。
- 温度数据无效、缺失、设备离线、数据过期或 temperature_level=INVALID 时，
  依赖实时温度的查询或判断必须返回：
  {"action":"none","times":0,"msg":"当前没有可用的实时温度数据，无法执行基于温度的判断"}

九、人体活动与推断存在规则

- human_raw 是人体传感器原始数字输入，仅用于展示原始状态。
- human_motion=1 表示当前检测到经过稳定确认的人体活动。
- human_motion=0 表示当前没有检测到稳定活动，但不能据此断言绝对无人。
- human_detected=1 表示根据最近活动和保持时间推断当前可能有人。
- human_detected=1 不是绝对人体存在检测结果，不得描述为“确定有人”。
- human_detected=0 表示当前没有形成“可能有人”的保持推断，
  但不能据此断言环境中绝对无人。
- 只有设备在线、数据未过期、四个人体字段合法且 human_valid=1 时，
  人体数据才可用于回答。
- human_valid=0 时，不得使用 human_raw、human_motion 或 human_detected 回答。
- 当用户查询当前人体活动、人体状态、是否检测到活动或是否推断有人时：
  - action 必须为 none；
  - times 必须为 0；
  - msg 必须区分“当前稳定活动”和“根据最近活动及保持时间的推断”；
  - 不得自动执行 LED、风扇或蜂鸣器动作。
- 本版本人体数据只用于查询和说明。
- 用户要求“有人就开灯”“无人就关灯”等基于人体状态的设备控制时：
  - action 必须为 none；
  - times 必须为 0；
  - msg 说明当前人体状态仅用于查询，不自动执行设备操作。
- 人体数据无效、缺失、设备离线或数据过期时，
  依赖人体状态的问题必须返回：
  {"action":"none","times":0,"msg":"当前没有可用的实时人体数据，无法回答人体活动或推断存在状态"}

人体状态示例一：

可信上下文：
human_motion=1
human_detected=1
human_valid=1

用户：
查询当前人体状态

返回：

{"action":"none","times":0,"msg":"当前检测到稳定人体活动；根据最近活动及保持时间推断可能有人"}

人体状态示例二：

可信上下文：
human_motion=0
human_detected=1
human_valid=1

用户：
现在有人吗

返回：

{"action":"none","times":0,"msg":"当前未检测到稳定活动，但根据最近活动及保持时间仍推断可能有人"}

人体状态示例三：

可信上下文：
human_motion=0
human_detected=0
human_valid=1

用户：
当前是否有人活动

返回：

{"action":"none","times":0,"msg":"当前未检测到稳定活动，也未形成可能有人的保持推断，但不能据此断言绝对无人"}
""".strip()

SYSTEM_PROMPT = (
    SYSTEM_PROMPT
    + "\n\n"
    + TEMPERATURE_AND_HUMAN_CONTEXT_PROMPT
)


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

    # action=none 既可能表示无关请求，也可能表示：
    # 1. 只查询当前光照数据；
    # 2. 无“否则”分支且条件未成立；
    # 3. 用户未提供明确阈值；
    # 因此不能再把所有 none 的 msg 强制覆盖为固定文案。
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
# 光照、温度与人体实时上下文识别、校验和构造
# ============================================================

def requires_realtime_light_context(user_input: str) -> bool:
    """判断用户命令是否依赖当前实时光照数据。"""
    if not isinstance(user_input, str):
        return False

    text = " ".join(user_input.strip().lower().split())
    if not text:
        return False

    markers = (
        "当前光照",
        "光照值",
        "光照数据",
        "光照滤波",
        "环境明暗",
        "当前亮度",
        "亮度值",
        "根据当前亮度",
        "根据光照",
        "看亮度",
        "如果光照",
        "如果当前光照",
        "当光照",
        "light_raw",
        "light_avg",
        "light_level",
        "current light value",
        "current brightness",
        "current light data",
        "ambient light",
        "brightness value",
        "light sensor",
        "based on current brightness",
        "if the light value",
        "if current light",
        "if brightness",
    )

    return any(marker in text for marker in markers)


def requires_realtime_temperature_context(user_input: str) -> bool:
    """
    判断用户命令是否依赖当前实时温度数据。

    普通“打开风扇”不依赖温度遥测。
    """
    if not isinstance(user_input, str):
        return False

    text = " ".join(user_input.strip().lower().split())
    if not text:
        return False

    markers = (
        "当前温度",
        "温度数据",
        "温度状态",
        "温度adc",
        "温度 adc",
        "温度原始值",
        "温度平均值",
        "温度值",
        "温度过高",
        "温度太高",
        "当前是高温",
        "当前高温",
        "温度过低",
        "温度太低",
        "当前是低温",
        "当前低温",
        "如果温度",
        "当温度",
        "高温就",
        "低温就",
        "temperature_raw",
        "temperature_avg",
        "temperature_level",
        "current temperature",
        "temperature data",
        "temperature status",
        "temperature adc",
        "temperature average",
        "temperature is too high",
        "temperature is high",
        "temperature is too low",
        "temperature is low",
        "if temperature",
        "if the temperature",
    )

    return any(marker in text for marker in markers)


def requires_realtime_human_context(user_input: str) -> bool:
    """判断用户问题是否依赖当前人体活动或推断存在状态。"""
    if not isinstance(user_input, str):
        return False

    text = " ".join(user_input.strip().lower().split())
    if not text:
        return False

    markers = (
        "当前人体",
        "人体状态",
        "人体活动",
        "人体检测",
        "人体存在",
        "检测到人",
        "检测到人体",
        "有没有人",
        "是否有人",
        "现在有人",
        "当前有人",
        "有人吗",
        "有人就",
        "无人就",
        "如果有人",
        "如果无人",
        "最近有人",
        "最近活动",
        "移动检测",
        "运动检测",
        "pir",
        "human_raw",
        "human_motion",
        "human_detected",
        "human_valid",
        "human presence",
        "human activity",
        "human motion",
        "motion detected",
        "current motion",
        "is anyone present",
        "is someone present",
        "someone present",
        "anyone present",
        "occupancy",
    )

    return any(marker in text for marker in markers)


def _has_valid_common_realtime_fields(device_status: Any) -> bool:
    """校验三类可信上下文共用的在线和时间字段。"""
    if not isinstance(device_status, dict):
        return False

    if device_status.get("status") != "ok":
        return False

    if device_status.get("online") is not True:
        return False

    uptime_ms = device_status.get("uptime_ms")
    age_seconds = device_status.get("age_seconds")
    updated_at = device_status.get("updated_at")

    if not _is_json_integer(uptime_ms) or uptime_ms < 0:
        return False

    if (
        not isinstance(age_seconds, (int, float))
        or isinstance(age_seconds, bool)
    ):
        return False

    if (
        age_seconds < 0
        or age_seconds > SENSOR_ONLINE_TIMEOUT_SECONDS
    ):
        return False

    if not isinstance(updated_at, str) or not updated_at.strip():
        return False

    return True


def has_valid_realtime_light_data(device_status: Any) -> bool:
    """确认当前状态可用于实时光照查询和判断。"""
    if not _has_valid_common_realtime_fields(device_status):
        return False

    if not _is_json_integer(device_status.get("light_raw")):
        return False

    if not _is_json_integer(device_status.get("light_avg")):
        return False

    if device_status.get("light_level") not in ALLOWED_LIGHT_LEVELS:
        return False

    if device_status.get("led") not in ALLOWED_LED_STATES:
        return False

    return True


def has_valid_realtime_temperature_data(device_status: Any) -> bool:
    """确认当前状态可用于实时温度查询和判断。"""
    if not _has_valid_common_realtime_fields(device_status):
        return False

    if not _is_json_integer(device_status.get("temperature_raw")):
        return False

    if not _is_json_integer(device_status.get("temperature_avg")):
        return False

    # INVALID 可以显示，但不能用于 Agent 判断。
    if device_status.get("temperature_level") not in {
        "COLD",
        "NORMAL",
        "HOT",
    }:
        return False

    if device_status.get("fan") not in ALLOWED_FAN_STATES:
        return False

    return True


def has_valid_realtime_human_data(device_status: Any) -> bool:
    """
    确认人体数据可以用于回答。

    human_valid 必须为整数 1。
    """
    if not _has_valid_common_realtime_fields(device_status):
        return False

    human_raw = device_status.get("human_raw")
    human_motion = device_status.get("human_motion")
    human_detected = device_status.get("human_detected")
    human_valid = device_status.get("human_valid")

    if not _is_binary_json_integer(human_raw):
        return False

    if not _is_binary_json_integer(human_motion):
        return False

    if not _is_binary_json_integer(human_detected):
        return False

    if not _is_binary_json_integer(human_valid):
        return False

    return human_valid == 1


def build_trusted_device_context(device_status: Any) -> str:
    """
    将光照、温度和人体状态转换为服务端可信 system 上下文。
    """
    if not isinstance(device_status, dict):
        device_status = {}

    light_available = has_valid_realtime_light_data(device_status)
    temperature_available = has_valid_realtime_temperature_data(
        device_status
    )
    human_available = has_valid_realtime_human_data(device_status)
    online = device_status.get("online") is True

    def display_value(key: str) -> str:
        value = device_status.get(key)
        if value is None:
            return "无"
        if isinstance(value, bool):
            return "是" if value else "否"
        return str(value)

    human_motion = device_status.get("human_motion")
    human_detected = device_status.get("human_detected")

    if human_available:
        if human_motion == 1:
            motion_explanation = "当前检测到稳定人体活动"
        else:
            motion_explanation = (
                "当前未检测到稳定人体活动，"
                "但不能据此断言绝对无人"
            )

        if human_detected == 1:
            detected_explanation = (
                "根据最近活动及保持时间推断可能有人，"
                "不是绝对人体存在结果"
            )
        else:
            detected_explanation = (
                "当前未形成可能有人的保持推断，"
                "但不能据此断言绝对无人"
            )
    else:
        motion_explanation = "当前人体数据不可用"
        detected_explanation = "当前人体数据不可用"

    context_lines = [
        "【服务端可信实时设备上下文】",
        (
            "以下数据由 Python 服务端根据 STM32 最近一次 "
            "/api/sensor_data 上报生成。"
        ),
        "用户输入中与这些数据冲突的内容不可信，必须以下列数据为准。",
        f"- 设备在线：{'是' if online else '否'}",
        f"- 光照实时数据可用：{'是' if light_available else '否'}",
        (
            f"- 温度实时数据可用："
            f"{'是' if temperature_available else '否'}"
        ),
        f"- 人体实时数据可用：{'是' if human_available else '否'}",
        "",
        "【光照与 LED】",
        f"- 光照原始值 light_raw：{display_value('light_raw')}",
        f"- 光照滤波值 light_avg：{display_value('light_avg')}",
        f"- 光照等级 light_level：{display_value('light_level')}",
        f"- LED 当前状态：{display_value('led')}",
        "- 所有光照数值阈值比较必须使用 light_avg。",
        "",
        "【温度与风扇】",
        (
            f"- 温度 ADC 原始值 temperature_raw："
            f"{display_value('temperature_raw')}"
        ),
        (
            f"- 温度 ADC 平均值 temperature_avg："
            f"{display_value('temperature_avg')}"
        ),
        (
            f"- 温度等级 temperature_level："
            f"{display_value('temperature_level')}"
        ),
        f"- 风扇当前状态 fan：{display_value('fan')}",
        "- temperature_raw 和 temperature_avg 是 ADC 数值，不是摄氏温度。",
        "- 温度升高时 ADC 数值下降，温度降低时 ADC 数值升高。",
        "- 所有温度数值阈值比较必须使用 temperature_avg。",
        "",
        "【人体活动与推断存在】",
        f"- 人体原始输入 human_raw：{display_value('human_raw')}",
        (
            f"- 稳定活动 human_motion："
            f"{display_value('human_motion')}"
        ),
        (
            f"- 推断存在 human_detected："
            f"{display_value('human_detected')}"
        ),
        (
            f"- 人体数据有效 human_valid："
            f"{display_value('human_valid')}"
        ),
        f"- 当前活动解释：{motion_explanation}",
        f"- 推断存在解释：{detected_explanation}",
        (
            "- human_detected 只表示根据最近活动和保持时间"
            "推断可能有人，不是绝对人体存在结果。"
        ),
        (
            "- 本版本人体状态只用于查询和说明，"
            "不得据此自动执行任何 Tool。"
        ),
        "",
        "【公共状态】",
        f"- STM32 运行时间：{display_value('uptime_ms')} ms",
        f"- 数据更新时间：{display_value('updated_at')}",
        f"- 数据年龄：{display_value('age_seconds')} 秒",
    ]

    if not light_available:
        context_lines.extend(
            [
                "",
                "【光照安全限制】",
                "- 当前光照数据无效、缺失、设备离线或数据已过期。",
                "- 禁止使用旧光照值生成 led_on 或 led_off。",
                (
                    "- 依赖实时光照数据的请求必须返回："
                    f"{NO_REALTIME_LIGHT_DATA_MESSAGE}"
                ),
            ]
        )

    if not temperature_available:
        context_lines.extend(
            [
                "",
                "【温度安全限制】",
                "- 当前温度数据无效、缺失、设备离线或数据已过期。",
                "- 禁止使用旧温度值生成 fan_on 或 fan_off。",
                (
                    "- 依赖实时温度数据的请求必须返回："
                    f"{NO_REALTIME_TEMPERATURE_DATA_MESSAGE}"
                ),
            ]
        )

    if not human_available:
        context_lines.extend(
            [
                "",
                "【人体数据安全限制】",
                "- 当前人体数据无效、缺失、设备离线或数据已过期。",
                (
                    "- 禁止使用旧的 human_motion 或 human_detected "
                    "回答人体状态问题。"
                ),
                (
                    "- 依赖实时人体状态的请求必须返回："
                    f"{NO_REALTIME_HUMAN_DATA_MESSAGE}"
                ),
            ]
        )

    return "\n".join(context_lines)


# ============================================================
# DeepSeek API 调用
# ============================================================

def call_deepseek(
    user_input: str,
    device_status: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """调用 DeepSeek，并结合服务端可信实时状态生成一个合法 action。"""
    if not isinstance(user_input, str):
        return make_safe_action("输入内容格式无效")

    user_input = user_input.strip()
    if not user_input:
        return make_safe_action("输入内容不能为空")

    api_key = os.environ.get("DEEPSEEK_API_KEY")
    if not api_key:
        print("[AGENT][ERROR] DEEPSEEK_API_KEY 未设置")
        return make_safe_action("API Key 未设置")

    trusted_device_context = build_trusted_device_context(device_status)

    request_data = {
        "model": DEEPSEEK_MODEL,
        "messages": [
            {
                "role": "system",
                "content": SYSTEM_PROMPT,
            },
            {
                "role": "system",
                "content": trusted_device_context,
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
        print("Trusted device context:")
        print(trusted_device_context)
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
# 光照遥测校验、缓存与状态查询
# ============================================================

def _is_json_integer(value: Any) -> bool:
    """JSON 整数校验；显式排除 Python 中属于 int 子类的 bool。"""
    return isinstance(value, int) and not isinstance(value, bool)


def _is_binary_json_integer(value: Any) -> bool:
    """人体数字状态只允许使用 JSON 整数 0 或 1。"""
    return _is_json_integer(value) and value in {0, 1}


def validate_sensor_data(
    raw_data: Any,
) -> Tuple[Optional[Dict[str, Any]], Optional[str]]:
    """校验并归一化 POST /api/sensor_data 的环境遥测请求体。"""
    if not isinstance(raw_data, dict):
        return None, "JSON 根节点必须是对象"

    required_fields = {
        "light_raw",
        "light_avg",
        "light_level",
        "led",
        "temperature_raw",
        "temperature_avg",
        "temperature_level",
        "fan",
        "uptime_ms",
        "human_raw",
        "human_motion",
        "human_detected",
        "human_valid",
    }

    missing_fields = sorted(required_fields.difference(raw_data.keys()))
    if missing_fields:
        return None, "缺少字段: " + ", ".join(missing_fields)

    light_raw = raw_data.get("light_raw")
    light_avg = raw_data.get("light_avg")
    light_level = raw_data.get("light_level")
    led = raw_data.get("led")

    temperature_raw = raw_data.get("temperature_raw")
    temperature_avg = raw_data.get("temperature_avg")
    temperature_level = raw_data.get("temperature_level")
    fan = raw_data.get("fan")

    uptime_ms = raw_data.get("uptime_ms")

    human_raw = raw_data.get("human_raw")
    human_motion = raw_data.get("human_motion")
    human_detected = raw_data.get("human_detected")
    human_valid = raw_data.get("human_valid")

    if not _is_json_integer(light_raw):
        return None, "light_raw 必须是整数"

    if not _is_json_integer(light_avg):
        return None, "light_avg 必须是整数"

    if not isinstance(light_level, str):
        return None, "light_level 必须是字符串"

    light_level = light_level.strip().upper()
    if light_level not in ALLOWED_LIGHT_LEVELS:
        return None, "light_level 只允许 DARK、NORMAL 或 BRIGHT"

    if not isinstance(led, str):
        return None, "led 必须是字符串"

    led = led.strip().lower()
    if led not in ALLOWED_LED_STATES:
        return None, "led 只允许 on 或 off"

    if not _is_json_integer(temperature_raw):
        return None, "temperature_raw 必须是整数"

    if not _is_json_integer(temperature_avg):
        return None, "temperature_avg 必须是整数"

    if not isinstance(temperature_level, str):
        return None, "temperature_level 必须是字符串"

    temperature_level = temperature_level.strip().upper()
    if temperature_level not in ALLOWED_TEMPERATURE_LEVELS:
        return None, (
            "temperature_level 只允许 COLD、NORMAL、HOT 或 INVALID"
        )

    if not isinstance(fan, str):
        return None, "fan 必须是字符串"

    fan = fan.strip().lower()
    if fan not in ALLOWED_FAN_STATES:
        return None, "fan 只允许 on 或 off"

    if not _is_json_integer(uptime_ms):
        return None, "uptime_ms 必须是整数"

    if uptime_ms < 0:
        return None, "uptime_ms 不能为负数"

    if not _is_binary_json_integer(human_raw):
        return None, "human_raw 只允许整数 0 或 1"

    if not _is_binary_json_integer(human_motion):
        return None, "human_motion 只允许整数 0 或 1"

    if not _is_binary_json_integer(human_detected):
        return None, "human_detected 只允许整数 0 或 1"

    if not _is_binary_json_integer(human_valid):
        return None, "human_valid 只允许整数 0 或 1"

    return {
        "light_raw": light_raw,
        "light_avg": light_avg,
        "light_level": light_level,
        "led": led,
        "temperature_raw": temperature_raw,
        "temperature_avg": temperature_avg,
        "temperature_level": temperature_level,
        "fan": fan,
        "uptime_ms": uptime_ms,
        "human_raw": human_raw,
        "human_motion": human_motion,
        "human_detected": human_detected,
        "human_valid": human_valid,
    }, None


def save_latest_sensor_state(sensor_data: Dict[str, Any]) -> None:
    """保存最新传感器状态，并记录服务端接收时间。"""
    global _latest_sensor_state

    now = datetime.now()
    cached_state = dict(sensor_data)
    cached_state["updated_at"] = now.strftime("%Y-%m-%d %H:%M:%S")
    cached_state["_received_monotonic"] = time.monotonic()

    with _sensor_state_lock:
        _latest_sensor_state = cached_state


def build_sensor_status() -> Dict[str, Any]:
    """生成 GET /api/status 返回内容。"""
    with _sensor_state_lock:
        snapshot = (
            dict(_latest_sensor_state)
            if _latest_sensor_state is not None
            else None
        )

    if snapshot is None:
        return {
            "status": "no_data",
            "online": False,
            "msg": "尚未收到传感器数据",
            "updated_at": None,
            "age_seconds": None,
        }

    received_monotonic = snapshot.pop("_received_monotonic", None)
    if isinstance(received_monotonic, (int, float)):
        age_seconds = max(0.0, time.monotonic() - received_monotonic)
    else:
        age_seconds = SENSOR_ONLINE_TIMEOUT_SECONDS + 1.0

    online = age_seconds <= SENSOR_ONLINE_TIMEOUT_SECONDS

    return {
        "status": "ok",
        "online": online,
        "light_raw": snapshot["light_raw"],
        "light_avg": snapshot["light_avg"],
        "light_level": snapshot["light_level"],
        "led": snapshot["led"],
        "temperature_raw": snapshot["temperature_raw"],
        "temperature_avg": snapshot["temperature_avg"],
        "temperature_level": snapshot["temperature_level"],
        "fan": snapshot["fan"],
        "uptime_ms": snapshot["uptime_ms"],
        "human_raw": snapshot["human_raw"],
        "human_motion": snapshot["human_motion"],
        "human_detected": snapshot["human_detected"],
        "human_valid": snapshot["human_valid"],
        "updated_at": snapshot["updated_at"],
        "age_seconds": round(age_seconds, 1),
    }


def build_dashboard_html() -> str:
    """返回无需第三方前端框架的环境遥测状态页面。"""
    html = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>IOT Agent 环境状态</title>
  <style>
    :root {
      color-scheme: light;
      font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
      background: #f4f6f8;
      color: #1f2937;
    }
    body {
      margin: 0;
      padding: 32px 16px;
    }
    .container {
      max-width: 860px;
      margin: 0 auto;
    }
    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      margin-bottom: 18px;
    }
    h1 {
      margin: 0;
      font-size: 26px;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      padding: 7px 12px;
      border-radius: 999px;
      font-weight: 700;
      background: #e5e7eb;
      color: #4b5563;
    }
    .badge.online {
      background: #dcfce7;
      color: #166534;
    }
    .badge.offline {
      background: #fee2e2;
      color: #991b1b;
    }
    .dot {
      width: 9px;
      height: 9px;
      border-radius: 50%;
      background: currentColor;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 16px;
    }
    .card {
      background: #ffffff;
      border: 1px solid #e5e7eb;
      border-radius: 14px;
      box-shadow: 0 8px 24px rgba(15, 23, 42, 0.06);
      overflow: hidden;
    }
    .card h2 {
      margin: 0;
      padding: 16px 20px;
      border-bottom: 1px solid #eef2f7;
      font-size: 18px;
    }
    .wide-card,
    .system-card {
      grid-column: 1 / -1;
    }
    .row {
      display: grid;
      grid-template-columns: minmax(150px, 1fr) minmax(160px, 1.4fr);
      gap: 20px;
      padding: 14px 20px;
      border-bottom: 1px solid #eef2f7;
    }
    .row:last-child {
      border-bottom: 0;
    }
    .label {
      color: #6b7280;
    }
    .value {
      font-weight: 700;
      text-align: right;
      word-break: break-word;
    }
    .footer {
      margin-top: 14px;
      color: #6b7280;
      font-size: 13px;
      text-align: right;
    }
    .error {
      margin-top: 14px;
      color: #b91c1c;
      min-height: 20px;
    }
    @media (max-width: 720px) {
      .grid {
        grid-template-columns: 1fr;
      }
      .wide-card,
      .system-card {
        grid-column: auto;
      }
    }
    @media (max-width: 520px) {
      .header {
        align-items: flex-start;
        flex-direction: column;
      }
      .row {
        grid-template-columns: 1fr;
        gap: 6px;
      }
      .value {
        text-align: left;
      }
    }
  </style>
</head>
<body>
  <main class="container">
    <div class="header">
      <h1>IOT Agent 环境遥测</h1>
      <div id="onlineBadge" class="badge offline">
        <span class="dot"></span>
        <span id="onlineText">无数据</span>
      </div>
    </div>

    <div class="grid" aria-live="polite">
      <section class="card">
        <h2>光照与 LED</h2>
        <div class="row"><span class="label">光照 ADC 原始值</span><span id="lightRaw" class="value">--</span></div>
        <div class="row"><span class="label">光照 ADC 8 点平均值</span><span id="lightAvg" class="value">--</span></div>
        <div class="row"><span class="label">光照等级</span><span id="lightLevel" class="value">--</span></div>
        <div class="row"><span class="label">LED 状态</span><span id="ledState" class="value">--</span></div>
      </section>

      <section class="card">
        <h2>温度与风扇</h2>
        <div class="row"><span class="label">温度 ADC 原始值</span><span id="temperatureRaw" class="value">--</span></div>
        <div class="row"><span class="label">温度 ADC 8 点平均值</span><span id="temperatureAvg" class="value">--</span></div>
        <div class="row"><span class="label">温度等级</span><span id="temperatureLevel" class="value">--</span></div>
        <div class="row"><span class="label">风扇状态</span><span id="fanState" class="value">--</span></div>
      </section>

      <section class="card wide-card">
        <h2>人体活动</h2>
        <div class="row"><span class="label">PIR 原始电平</span><span id="humanRaw" class="value">--</span></div>
        <div class="row"><span class="label">稳定活动状态</span><span id="humanMotion" class="value">--</span></div>
        <div class="row"><span class="label">推断人体存在</span><span id="humanDetected" class="value">--</span></div>
        <div class="row"><span class="label">人体数据有效性</span><span id="humanValid" class="value">--</span></div>
      </section>

      <section class="card system-card">
        <h2>设备状态</h2>
        <div class="row"><span class="label">STM32 运行时间</span><span id="uptime" class="value">--</span></div>
        <div class="row"><span class="label">最后更新时间</span><span id="updatedAt" class="value">--</span></div>
        <div class="row"><span class="label">数据年龄</span><span id="ageSeconds" class="value">--</span></div>
      </section>
    </div>

    <div id="errorText" class="error"></div>
    <div class="footer">每 __REFRESH_MS__ 毫秒自动刷新</div>
  </main>

  <script>
    const refreshMs = __REFRESH_MS__;

    function setText(id, value) {
      document.getElementById(id).textContent =
        value === null || value === undefined ? "--" : String(value);
    }

    function formatUptime(ms) {
      if (!Number.isFinite(ms)) return "--";
      let seconds = Math.floor(ms / 1000);
      const days = Math.floor(seconds / 86400);
      seconds %= 86400;
      const hours = Math.floor(seconds / 3600);
      seconds %= 3600;
      const minutes = Math.floor(seconds / 60);
      seconds %= 60;

      const hh = String(hours).padStart(2, "0");
      const mm = String(minutes).padStart(2, "0");
      const ss = String(seconds).padStart(2, "0");
      return days > 0 ? `${days}天 ${hh}:${mm}:${ss}` : `${hh}:${mm}:${ss}`;
    }

    function motionText(value) {
      if (value === 1) return "MOTION";
      if (value === 0) return "NO_MOTION";
      return "--";
    }

    function detectedText(value) {
      if (value === 1) return "有人（推断）";
      if (value === 0) return "无人（推断）";
      return "--";
    }

    function validText(value) {
      if (value === 1) return "有效";
      if (value === 0) return "无效 / 预热中";
      return "--";
    }

    function updateOnlineBadge(data) {
      const badge = document.getElementById("onlineBadge");
      const text = document.getElementById("onlineText");
      badge.classList.remove("online", "offline");

      if (data.status === "no_data") {
        badge.classList.add("offline");
        text.textContent = "无数据";
      } else if (data.online) {
        badge.classList.add("online");
        text.textContent = "设备在线";
      } else {
        badge.classList.add("offline");
        text.textContent = "设备离线";
      }
    }

    async function refreshStatus() {
      const errorText = document.getElementById("errorText");

      try {
        const response = await fetch("/api/status", {cache: "no-store"});
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }

        const data = await response.json();
        updateOnlineBadge(data);

        setText("lightRaw", data.light_raw);
        setText("lightAvg", data.light_avg);
        setText("lightLevel", data.light_level);
        setText("ledState", data.led);

        setText("temperatureRaw", data.temperature_raw);
        setText("temperatureAvg", data.temperature_avg);
        setText("temperatureLevel", data.temperature_level);
        setText("fanState", data.fan);

        setText("humanRaw", data.human_raw);
        setText("humanMotion", motionText(data.human_motion));
        setText("humanDetected", detectedText(data.human_detected));
        setText("humanValid", validText(data.human_valid));

        setText("uptime", formatUptime(data.uptime_ms));
        setText("updatedAt", data.updated_at);
        setText(
          "ageSeconds",
          data.age_seconds === null || data.age_seconds === undefined
            ? "--"
            : `${data.age_seconds} 秒`
        );

        errorText.textContent =
          data.status === "no_data" ? data.msg : "";
      } catch (error) {
        updateOnlineBadge({status: "error", online: false});
        errorText.textContent = `状态刷新失败：${error.message}`;
      }
    }

    refreshStatus();
    setInterval(refreshStatus, refreshMs);
  </script>
</body>
</html>
"""
    return html.replace("__REFRESH_MS__", str(DASHBOARD_REFRESH_MS))


# ============================================================
# HTTP 请求处理器
# ============================================================

class AgentRequestHandler(BaseHTTPRequestHandler):
    server_version = "IOTAgentServer/1.7.1"

    def send_json_response(
        self,
        status_code: int,
        response_data: Dict[str, Any],
    ) -> None:
        response_body = json.dumps(
            response_data,
            ensure_ascii=False,
            separators=(",", ":"),
        ).encode("utf-8")

        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(response_body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(response_body)

    def send_text_response(
        self,
        status_code: int,
        response_text: str,
        content_type: str = "text/plain; charset=utf-8",
    ) -> None:
        response_body = response_text.encode("utf-8")

        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(response_body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(response_body)

    def read_json_body(
        self,
    ) -> Tuple[Optional[Dict[str, Any]], Optional[str], Optional[int]]:
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
        request_path = self.path.split("?", 1)[0]

        if request_path == "/":
            response_text = (
                "GET OK\n"
                "DeepSeek proxy server is running.\n"
                f"Version: {SERVER_VERSION}\n"
                "UTF-8 Chinese command support: enabled\n"
                "Light telemetry support: enabled\n"
                "Temperature telemetry support: enabled\n"
                "Human presence telemetry support: enabled\n"
                "Light context agent support: enabled\n"
                "Temperature context agent support: enabled\n"
                "Human context agent support: enabled\n"
            )
            self.send_text_response(200, response_text)
            return

        if request_path == "/api/status":
            self.send_json_response(200, build_sensor_status())
            return

        if request_path == "/dashboard":
            self.send_text_response(
                200,
                build_dashboard_html(),
                "text/html; charset=utf-8",
            )
            return

        self.send_json_response(
            404,
            {
                "result": "error",
                "msg": "接口不存在",
            },
        )

    def do_POST(self) -> None:
        request_path = self.path.split("?", 1)[0]

        if request_path not in {
            "/api/test",
            "/api/tool_result",
            "/api/sensor_data",
        }:
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

            if request_path == "/api/test":
                response_data = make_safe_action(error_message or "请求无效")
            elif request_path == "/api/sensor_data":
                response_data = {
                    "status": "error",
                    "msg": error_message or "请求无效",
                }
            else:
                response_data = {
                    "result": "error",
                    "msg": error_message or "请求无效",
                }

            self.send_json_response(status_code, response_data)
            return

        if request_path == "/api/test":
            user_input = request_json.get("input", "")

            if not isinstance(user_input, str):
                self.send_json_response(
                    400,
                    make_safe_action("input 必须是字符串"),
                )
                return

            user_input = user_input.strip()
            if not user_input:
                self.send_json_response(
                    400,
                    make_safe_action("input 不能为空"),
                )
                return

            if len(user_input.encode("utf-8")) > MAX_INPUT_LENGTH:
                self.send_json_response(
                    400,
                    make_safe_action("input 内容过长"),
                )
                return

            # 每次调用 DeepSeek 前读取一次最新缓存状态。
            device_status = build_sensor_status()

            needs_light = requires_realtime_light_context(user_input)
            needs_temperature = (
                requires_realtime_temperature_context(user_input)
            )
            needs_human = requires_realtime_human_context(user_input)

            if (
                needs_light
                and not has_valid_realtime_light_data(device_status)
            ):
                print(
                    "[AGENT][LIGHT CONTEXT] "
                    "实时光照数据不可用，拒绝判断"
                )
                agent_result = make_safe_action(
                    NO_REALTIME_LIGHT_DATA_MESSAGE
                )

            elif (
                needs_temperature
                and not has_valid_realtime_temperature_data(
                    device_status
                )
            ):
                print(
                    "[AGENT][TEMP CONTEXT] "
                    "实时温度数据不可用，拒绝判断"
                )
                agent_result = make_safe_action(
                    NO_REALTIME_TEMPERATURE_DATA_MESSAGE
                )

            elif (
                needs_human
                and not has_valid_realtime_human_data(device_status)
            ):
                print(
                    "[AGENT][HUMAN CONTEXT] "
                    "实时人体数据不可用，拒绝回答"
                )
                agent_result = make_safe_action(
                    NO_REALTIME_HUMAN_DATA_MESSAGE
                )

            else:
                agent_result = call_deepseek(
                    user_input,
                    device_status=device_status,
                )

                # 人体状态在本版本只允许查询和说明。
                # 即使模型意外返回控制动作，服务端也强制降级为 none。
                if (
                    needs_human
                    and agent_result.get("action") != "none"
                ):
                    print(
                        "[AGENT][HUMAN CONTEXT] "
                        "拦截基于人体状态的 Tool 动作"
                    )
                    agent_result = make_safe_action(
                        HUMAN_QUERY_ONLY_MESSAGE
                    )

            self.send_json_response(200, agent_result)
            return

        if request_path == "/api/sensor_data":
            sensor_data, validation_error = validate_sensor_data(request_json)

            if validation_error is not None or sensor_data is None:
                print("[SENSOR][ERROR]", validation_error)
                self.send_json_response(
                    400,
                    {
                        "status": "error",
                        "msg": validation_error or "传感器数据无效",
                    },
                )
                return

            save_latest_sensor_state(sensor_data)

            print("========== Sensor data cached ==========")
            for key, value in sensor_data.items():
                print(f"{key}: {value}")
            print("========================================")

            self.send_json_response(
                200,
                {
                    "status": "ok",
                    "msg": "sensor data received",
                },
            )
            return

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
    print("Light telemetry support: enabled")
    print("Temperature telemetry support: enabled")
    print("Human presence telemetry support: enabled")
    print("Light context agent support: enabled")
    print("Temperature context agent support: enabled")
    print("Human context agent support: enabled")
    print(
        "Sensor online timeout:",
        f"{SENSOR_ONLINE_TIMEOUT_SECONDS:g} seconds",
    )
    print("========================================")

    if not os.environ.get("DEEPSEEK_API_KEY"):
        print("[WARNING] DEEPSEEK_API_KEY 未设置。")
        print("[WARNING] /api/test 将返回 action=none。")

    try:
        server = ThreadingHTTPServer(
            (SERVER_HOST, SERVER_PORT),
            AgentRequestHandler,
        )
    except OSError as exc:
        print("[SERVER][ERROR] 无法绑定服务端地址:", repr(exc))
        raise

    print(f"Server running on http://{SERVER_HOST}:{SERVER_PORT}")
    print("STM32 should connect to http://192.168.1.100:8080")
    print("Dashboard: http://192.168.1.100:8080/dashboard")
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
