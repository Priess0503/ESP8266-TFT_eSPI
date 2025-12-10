# ESP8266-制作桌面监控
ESP8266 配合哪吒探针用TFT_eSPI屏幕显示服务器状态。
<table><thead><tr><th>ESP8266 引脚</th><th>屏幕引脚</th><th>功能说明</th></tr></thead><tbody><tr><td>3V3</td><td>VCC</td><td>电源正极</td></tr><tr><td>GND</td><td>GND</td><td>电源负极</td></tr><tr><td>D5 (GPIO14)</td><td>SCL/CLK</td><td>SPI 时钟</td></tr><tr><td>D7 (GPIO13)</td><td>SDA/MOSI</td><td>SPI 数据</td></tr><tr><td>D8 (GPIO15)</td><td>CS</td><td>片选</td></tr><tr><td>D3 (GPIO0)</td><td>DC</td><td>数据 / 命令</td></tr><tr><td>D4 (GPIO2)</td><td>RES</td><td>复位</td></tr><tr><td>可选</td><td>BLK</td><td>背光控制（可接 3V3 常亮）</td></tr></tbody></table>
