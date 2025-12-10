#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <math.h>
// 新增：引入中文点阵和渲染库（关键）
#include "OpenFontRender.h"
#include "MyChineseFont.h"
#include "icons.h"  // 平台图标（20x20）

TFT_eSPI tft = TFT_eSPI();
OpenFontRender render;  // 创建中文渲染对象

// WiFi 参数（保持不变）
const char *WIFI_SSID = "Xiaoyaozi";
const char *WIFI_PASS = "zhanglu742206@@";
// API 参数（保持不变）
const char *API_URL = "http://192.168.50.48:8080/";
const char *API_HOST = "192.168.50.48";
const uint16_t API_PORT = 8080;
const char *API_PATH = "/";
// 定时参数（保持不变）
const uint32_t FETCH_INTERVAL = 30000;
const uint32_t ROTATE_INTERVAL = 8000;
uint32_t lastFetch = 0;
uint32_t lastRotate = 0;

// 数据结构
struct ServerInfo {
  char name[32];
  char platform[16];
  float cpu;
  float mem;
  float disk;
  float disk_usage;  // 磁盘使用率（新增）
  float up;
  float down;
};
const size_t MAX_SERVERS = 5;
ServerInfo servers[MAX_SERVERS];
size_t serverCount = 0;
size_t currentIdx = 0;

// 函数声明（保持不变）
void connectWiFi();
bool fetchData();
void showServer(size_t idx);
// 修改：中文显示函数（替换原 drawTextLine）
void drawChineseText(int16_t x, int16_t y, const String &text, uint8_t fontSize = 16);
const uint16_t *pickIcon(const char *platform);
void drawCpuGauge(float cpu);
void fillArcSegment(int16_t cx, int16_t cy, int16_t rOuter, int16_t thickness, float startDeg, float endDeg, uint16_t color);
void drawDiskProgressBar(float usage);  // 磁盘使用率进度条

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(TFT_BL, HIGH);
  tft.setTextWrap(false);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // 新增：初始化中文渲染（关键）
  render.setTFT(&tft);  // 绑定 TFT 屏幕
  render.setColor(TFT_WHITE);  // 中文颜色
  render.setBackColor(TFT_BLACK);  // 背景色（与屏幕背景一致）

  connectWiFi();
  fetchData();
  showServer(0);
  lastRotate = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastFetch >= FETCH_INTERVAL || serverCount == 0) {
    fetchData();
    currentIdx = 0;
  }
  if (serverCount > 0 && now - lastRotate >= ROTATE_INTERVAL) {
    currentIdx = (currentIdx + 1) % serverCount;
    showServer(currentIdx);
    lastRotate = now;
  }
}

// WiFi 连接函数（保持不变）
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// 数据获取函数（保持不变，注意 JSON 缓冲区大小）
bool fetchData() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, API_URL)) {
    Serial.println("HTTP begin 失败");
    return false;
  }
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  // 注意：原代码 JSON 缓冲区太小（StaticJsonDocument<10>），会解析失败！
  // 修改：扩大 JSON 缓冲区（根据 API 返回数据大小调整，建议至少 512）
  StaticJsonDocument<512> doc;  
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonArray arr = doc["servers"].as<JsonArray>();
  serverCount = 0;
  for (JsonObject obj : arr) {
    if (serverCount >= MAX_SERVERS) break;
    ServerInfo &s = servers[serverCount];
    strlcpy(s.name, obj["name"] | "未知", sizeof(s.name));
    strlcpy(s.platform, obj["platform"] | "unknown", sizeof(s.platform));
    s.cpu = obj["cpu_usage"] | 0.0;
    s.mem = obj["memory_usage"] | 0.0;
    s.disk = obj["disk_total_gb"] | 0.0;
    s.disk_usage = obj["disk_usage"] | 0.0;  // 解析磁盘使用率
    s.up = obj["upload_speed_kb"] | 0.0;
    s.down = obj["download_speed_kb"] | 0.0;
    serverCount++;
  }
  lastFetch = millis();
  Serial.printf("Fetched %u servers\n", (unsigned)serverCount);
  if (serverCount > 0) {
    showServer(0);
    lastRotate = millis();
  }
  return serverCount > 0;
}

// 关键修改：显示函数（用中文函数替换原英文字体）
void showServer(size_t idx) {
  if (idx >= serverCount) return;
  ServerInfo &s = servers[idx];
  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);  // 数字统一使用字体2，大小与CPU一致

  // 中文标题 + 设备名（设备名可能包含中文，统一用 drawChineseText）
  drawChineseText(4, 10, "设备：");
  drawChineseText(4 + 16*3, 10, s.name);  // 设备名用中文渲染，支持中文

  drawChineseText(4, 36, "系统：");
  {
    const uint16_t *icon = pickIcon(s.platform);
    int16_t iconX = 4 + 16 * 3;
    int16_t iconY = 35;          // 稍微上移，图标 20px 高
    if (icon) {
      tft.pushImage(iconX, iconY, ICON_W, ICON_H, icon);
      tft.setTextFont(2);
      //tft.drawString(s.platform, iconX + ICON_W + 4, 36);
    } else {
     // tft.drawString(s.platform, 4 + 16 * 3, 36);
    }
  }

  drawChineseText(4, 62, "CPU：");  // render.print 会自动处理 ASCII 的 "CPU" 和中文的 "："
  tft.drawString(String(s.cpu, 2) + "%", 4 + 16*3, 62);  // CPU：= 4个字符宽度

  drawChineseText(4, 88, "内存：");
  tft.drawString(String(s.mem, 2) + "%", 4 + 16*3, 88);

  drawChineseText(4, 114, "磁盘：");
  tft.drawString(String(s.disk, 1) + "GB", 4 + 16*3, 114);
  


  drawChineseText(4, 140, "上行：");
  tft.drawString(String(s.up, 2) + "KB/s", 4 + 16*3, 140);

  drawChineseText(4, 166, "下行：");
  tft.drawString(String(s.down, 2) + "KB/s", 4 + 16*3, 166);

  // 环图放在最后连续绘制，减少分步出现感
  drawCpuGauge(s.cpu);  // 右侧环形图（上方）
  drawMemGauge(s.mem);  // 右侧第二个环图（下方）
  tft.setTextFont(2);  // 环图内部会切换字体，这里重置为字体2供后续数值使用
  // 磁盘使用率进度条（在磁盘信息下方）
  drawDiskProgressBar(s.disk_usage);
  // 页脚序号（保持不变）
  tft.drawString(String(idx + 1) + "/" + String(serverCount), 4, 208);
}

// 新增：中文渲染函数（适配 16x16 点阵）
void drawChineseText(int16_t x, int16_t y, const String &text, uint8_t fontSize) {
  render.setCursor(x, y);  // 设置起始坐标
  render.setFontSize(fontSize);  // 字体大小（16=16x16 点阵）
  render.print(text);  // 渲染中文（自动解析 GB2312 编码）
}

// 根据平台名称选择图标
const uint16_t *pickIcon(const char *platform) {
  if (platform == nullptr) return nullptr;
  String p = String(platform);
  p.toLowerCase();
  if (p.indexOf("centos") != -1) return centosIcon;
  if (p.indexOf("debian") != -1) return debianIcon;
  if (p.indexOf("raspbian") != -1 || p.indexOf("raspi") != -1 || p.indexOf("pi") != -1) return raspiIcon;
  return nullptr;
}

// 通用环图（增加层次：外圈暗底 + 轨道 + 前景亮色）
void drawGauge(int16_t cx, int16_t cy, float value, const char *label, uint16_t fg, uint16_t track, uint16_t base) {
  const int16_t rOuter = 40;    // 外半径
  const int16_t thickness = 9;  // 环宽

  // 轨道（未激活部分）
  fillArcSegment(cx, cy, rOuter, thickness, 0, 360, track);
  // 前景亮色
  float angle = constrain(value, 0.0f, 100.0f) * 3.6f;  // 0~360
  fillArcSegment(cx, cy, rOuter, thickness, -90, -90 + angle, fg);
  // 中心文字
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(2);
  char buf[12];
  snprintf(buf, sizeof(buf), "%.0f%%", value);
  tft.drawString(buf, cx, cy - 4);
  tft.setTextFont(1);
  tft.drawString(label, cx, cy + 12);
  tft.setTextFont(2);        // 还原为字体2
  tft.setTextDatum(TL_DATUM); // 还原
}

// 右侧 CPU 环图（上方）——蓝色系，参考附件效果
void drawCpuGauge(float cpu) {
  // 动态颜色：<50% 绿，50-80 橙，>=80 红
  uint16_t fg = (cpu < 50.0f) ? 0x07E0 /*绿*/ : (cpu < 80.0f ? 0xFD20 /*橙*/ : 0xF800 /*红*/);
  // 灰色底/轨道
  const uint16_t track = 0x3186;       // 较暗浅灰
  const uint16_t base  = 0x3186;       // 深灰底
  drawGauge(190, 55, cpu, "CPU", fg, track, base);
}

// 右侧内存环图（下方）——绿色系，参考附件效果
void drawMemGauge(float mem) {
  // 动态颜色：<50% 蓝，50-80 橙，>=80 红
  uint16_t fg = (mem < 50.0f) ? 0x4C9F /*蓝*/ : (mem < 80.0f ? 0xFD20 /*橙*/ : 0xF800 /*红*/);

  const uint16_t track = 0x3186;        // 更暗浅灰轨道
  const uint16_t base  = 0x3186;        // 深灰底
  drawGauge(190, 145, mem, "MEM", fg, track, base);
}

// 填充弧段（多边形近似）
void fillArcSegment(int16_t cx, int16_t cy, int16_t rOuter, int16_t thickness, float startDeg, float endDeg, uint16_t color) {
  float rInner = rOuter - thickness;
  float step = 1.0f; // 适中步进，平滑与速度平衡
  for (float a = startDeg; a < endDeg; a += step) {
    float a1 = a * DEG_TO_RAD;
    float a2 = min(a + step, endDeg) * DEG_TO_RAD;
    int16_t x1o = cx + rOuter * cos(a1);
    int16_t y1o = cy + rOuter * sin(a1);
    int16_t x2o = cx + rOuter * cos(a2);
    int16_t y2o = cy + rOuter * sin(a2);
    int16_t x1i = cx + rInner * cos(a1);
    int16_t y1i = cy + rInner * sin(a1);
    int16_t x2i = cx + rInner * cos(a2);
    int16_t y2i = cy + rInner * sin(a2);
    tft.fillTriangle(x1o, y1o, x2o, y2o, x1i, y1i, color);
    tft.fillTriangle(x1i, y1i, x2o, y2o, x2i, y2i, color);
  }
}

// 绘制磁盘使用率进度条（参考附件2样式：深色轨道+绿色填充）
void drawDiskProgressBar(float usage) {
  // 进度条位置和尺寸（在"磁盘："行下方，黄色位置）
  const int16_t x = 4;                    // 起始X坐标
  const int16_t y = 190;                  // Y坐标（磁盘信息下方）
  const int16_t width = 160;              // 进度条宽度
  const int16_t height = 8;               // 进度条高度
  const int16_t radius = 4;               // 圆角半径
  
  // 限制使用率范围
  float percent = constrain(usage, 0.0f, 100.0f);
  int16_t fillWidth = (int16_t)(width * percent / 100.0f);
  
  // 绘制背景轨道（深色，类似附件2的深色轨道）
  uint16_t trackColor = 0x3186;  // 深色轨道（深蓝灰色）
  tft.fillRoundRect(x, y, width, height, radius, trackColor);
  
  // 绘制填充部分（绿色，参考附件2）
  uint16_t fillColor = 0x07E0;  // 绿色
  if (fillWidth > 0) {
    // 只绘制填充部分，使用圆角矩形
    if (fillWidth >= radius * 2) {
      // 填充宽度足够，绘制完整圆角矩形
      tft.fillRoundRect(x, y, fillWidth, height, radius, fillColor);
    } else {
      // 填充宽度较小，绘制普通矩形（避免圆角问题）
      tft.fillRect(x, y, fillWidth, height, fillColor);
    }
  }
}