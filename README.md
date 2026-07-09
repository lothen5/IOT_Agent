# IOT_Agent

STM32H743 + FreeRTOS + W5500 embedded AI Agent terminal.

## Local network test

- Device IP: `192.168.1.100`
- Subnet mask: `255.255.255.0`

Start the HTTP GET test server:

```powershell
cd http_test
python -m http.server 8080 --bind 192.168.1.100
```

Start the DeepSeek proxy server:

```powershell
cd http_test
$env:DEEPSEEK_API_KEY = "your_api_key_here"
python post_server.py
```

The API key is read from the `DEEPSEEK_API_KEY` environment variable and must
never be committed to the repository.
