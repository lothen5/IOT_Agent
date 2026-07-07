以太网设置保持

IP 地址：192.168.1.100
子网掩码：255.255.255.0
网关：空
首选 DNS：空
备用 DNS：空


启动GET测试服务器

cd /d D:\STM32project\STM32H743\IOT_Agent\http_test
python -m http.server 8080 --bind 192.168.1.100


启动 POST 测试服务器

cd /d D:\STM32project\STM32H743\IOT_Agent\http_test
python post_server.py