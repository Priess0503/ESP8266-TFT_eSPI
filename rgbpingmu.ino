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
bool dataUpdated = false;  // 标记数据更新，避免跳回第一页

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
void drawMemGauge(float mem);  // 内存环图
void fillArcSegment(int16_t cx, int16_t cy, int16_t rOuter, int16_t thickness, float startDeg, float endDeg, uint16_t color);
void drawDiskProgressBar(float usage);  // 磁盘使用率进度条
void showBootAnimation();  // 显示开机动画

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

  // 优化一：先显示开机动画
  showBootAnimation();

  // 节能优化：启用WiFi低功耗模式
  WiFi.setSleepMode(WIFI_LIGHT_SLEEP);  // 启用WiFi轻睡眠模式，降低功耗

  connectWiFi();
  fetchData();
  showServer(0);
  lastRotate = millis();
}

void loop() {
  uint32_t now = millis();
  bool hasWork = false;
  
  if (now - lastFetch >= FETCH_INTERVAL || serverCount == 0) {
    if (fetchData()) {
      dataUpdated = true;
    }
    hasWork = true;
  }
  if (serverCount > 0 && now - lastRotate >= ROTATE_INTERVAL) {
    // 如果刚更新数据，保持当前页不跳回第一页
    if (dataUpdated) {
      if (currentIdx >= serverCount) currentIdx = 0;
      showServer(currentIdx);
      lastRotate = now;
      dataUpdated = false;
      hasWork = true;
    } else {
      currentIdx = (currentIdx + 1) % serverCount;
      showServer(currentIdx);
      lastRotate = now;
      hasWork = true;
    }
  }
  
  // 节能优化：空闲时让出CPU时间，降低功耗
  if (!hasWork) {
    yield();  // 让出CPU时间，允许WiFi和系统任务运行
    delay(10);  // 短暂延迟，进一步降低CPU占用
  }
}

// WiFi 连接函数（节能优化）
void connectWiFi() {
  // 如果已连接，直接返回，避免重复连接
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  #ifdef DEBUG_MODE
  Serial.printf("Connecting to %s", WIFI_SSID);
  #endif
  
  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    yield();  // 让出CPU时间
    #ifdef DEBUG_MODE
    Serial.print(".");
    #endif
    retries++;
  }
  
  #ifdef DEBUG_MODE
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
  } else {
    Serial.println("\nWiFi connection failed");
  }
  #endif
}

// 数据获取函数（优化二：优化API读取方式，提高速度）
bool fetchData() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  WiFiClient client;
  HTTPClient http;
  
  // 优化：设置更短的超时时间，加快失败响应
  http.setTimeout(3000);  // 从5秒减少到3秒
  
  // 优化：使用更高效的连接方式（直接使用IP和端口，避免DNS解析）
  if (!http.begin(client, API_HOST, API_PORT, API_PATH)) {
    #ifdef DEBUG_MODE
    Serial.println("HTTP begin 失败");
    #endif
    return false;
  }
  
  // 优化：设置HTTP头，减少数据传输
  http.setUserAgent("ESP8266");
  http.addHeader("Connection", "close");
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    #ifdef DEBUG_MODE
    Serial.printf("HTTP error: %d\n", httpCode);
    #endif
    http.end();
    return false;
  }
  
  // 优化：使用更高效的字符串读取方式
  String payload = http.getString();
  http.end();
  
  yield();  // 让出CPU时间

  // 注意：原代码 JSON 缓冲区太小（StaticJsonDocument<10>），会解析失败！
  // 修改：扩大 JSON 缓冲区（根据 API 返回数据大小调整，建议至少 512）
  StaticJsonDocument<512> doc;  
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    #ifdef DEBUG_MODE
    Serial.printf("JSON parse error: %s\n", err.c_str());
    #endif
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
  #ifdef DEBUG_MODE
  Serial.printf("Fetched %u servers\n", (unsigned)serverCount);
  #endif
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

// 填充弧段（优化：平衡速度与光滑度）
void fillArcSegment(int16_t cx, int16_t cy, int16_t rOuter, int16_t thickness, float startDeg, float endDeg, uint16_t color) {
  float rInner = rOuter - thickness;
  // 优化：使用0.4度步进，在保持光滑度的同时提高速度
  const float step = 0.4f;
  
  // 优化：预先计算角度范围
  float totalAngle = endDeg - startDeg;
  if (totalAngle <= 0) return;
  
  // 计算需要绘制的片段数
  int segments = (int)(totalAngle / step) + 1;
  uint16_t drawCount = 0;
  
  // 优化：减少边缘线绘制频率（每8个片段绘制一次，提高速度）
  const int edgeDrawInterval = 8;
  
  // 存储上一个片段的点，用于绘制边缘线
  int16_t prevXo = 0, prevYo = 0, prevXi = 0, prevYi = 0;
  bool firstSegment = true;
  
  // 优化：使用更高效的绘制方法
  for (int i = 0; i < segments; i++) {
    float a1 = startDeg + i * step;
    float a2 = (i == segments - 1) ? endDeg : (startDeg + (i + 1) * step);
    
    // 转换为弧度
    float a1Rad = a1 * DEG_TO_RAD;
    float a2Rad = a2 * DEG_TO_RAD;
    
    // 计算外圆和内圆的四个点
    float cos1 = cos(a1Rad);
    float sin1 = sin(a1Rad);
    float cos2 = cos(a2Rad);
    float sin2 = sin(a2Rad);
    
    // 使用四舍五入提高精度
    int16_t x1o = (int16_t)(cx + rOuter * cos1 + 0.5f);
    int16_t y1o = (int16_t)(cy + rOuter * sin1 + 0.5f);
    int16_t x2o = (int16_t)(cx + rOuter * cos2 + 0.5f);
    int16_t y2o = (int16_t)(cy + rOuter * sin2 + 0.5f);
    int16_t x1i = (int16_t)(cx + rInner * cos1 + 0.5f);
    int16_t y1i = (int16_t)(cy + rInner * sin1 + 0.5f);
    int16_t x2i = (int16_t)(cx + rInner * cos2 + 0.5f);
    int16_t y2i = (int16_t)(cy + rInner * sin2 + 0.5f);
    
    // 使用两个三角形填充每个弧段
    tft.fillTriangle(x1o, y1o, x2o, y2o, x1i, y1i, color);
    tft.fillTriangle(x1i, y1i, x2o, y2o, x2i, y2i, color);
    
    // 优化：减少边缘线绘制频率，提高速度
    // 只在关键位置绘制边缘线（每8个片段或最后一段）
    if ((i % edgeDrawInterval == 0 || i == segments - 1) && !firstSegment) {
      // 连接外圆边缘
      if (abs(x1o - prevXo) > 1 || abs(y1o - prevYo) > 1) {
        tft.drawLine(prevXo, prevYo, x1o, y1o, color);
      }
      // 连接内圆边缘
      if (abs(x1i - prevXi) > 1 || abs(y1i - prevYi) > 1) {
        tft.drawLine(prevXi, prevYi, x1i, y1i, color);
      }
    }
    
    // 更新上一个点
    prevXo = x2o;
    prevYo = y2o;
    prevXi = x2i;
    prevYi = y2i;
    firstSegment = false;
    
    // 优化：减少yield频率，提高绘制速度
    drawCount++;
    if (drawCount % 100 == 0) {
      yield();
    }
  }
  
  // 绘制起始和结束的径向线，确保边界完整
  float startRad = startDeg * DEG_TO_RAD;
  float endRad = endDeg * DEG_TO_RAD;
  int16_t x1o = (int16_t)(cx + rOuter * cos(startRad) + 0.5f);
  int16_t y1o = (int16_t)(cy + rOuter * sin(startRad) + 0.5f);
  int16_t x1i = (int16_t)(cx + rInner * cos(startRad) + 0.5f);
  int16_t y1i = (int16_t)(cy + rInner * sin(startRad) + 0.5f);
  tft.drawLine(x1o, y1o, x1i, y1i, color);
  
  int16_t x2o = (int16_t)(cx + rOuter * cos(endRad) + 0.5f);
  int16_t y2o = (int16_t)(cy + rOuter * sin(endRad) + 0.5f);
  int16_t x2i = (int16_t)(cx + rInner * cos(endRad) + 0.5f);
  int16_t y2i = (int16_t)(cy + rInner * sin(endRad) + 0.5f);
  tft.drawLine(x2o, y2o, x2i, y2i, color);
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

// 优化一：显示开机动画（jiIcon图标 + "启动中…"文字）
void showBootAnimation() {
  tft.fillScreen(TFT_BLACK);
  
  // 获取屏幕尺寸（TFT_eSPI库提供的方法）
  const int16_t screenWidth = tft.width();
  const int16_t screenHeight = tft.height();
  
  // 居中显示100x100图标
  const int16_t iconX = (screenWidth - JI_ICON_W) / 2;   // 图标X坐标（居中）
  const int16_t iconY = (screenHeight - JI_ICON_H - 40) / 2;  // 图标Y坐标（稍微上移，为文字留空间）
  
  // 显示开机图标（100x100）
  tft.pushImage(iconX, iconY, JI_ICON_W, JI_ICON_H, jiIcon);
  
  // 显示"启动中…"文字（图标下方）
  const int16_t textY = iconY + JI_ICON_H + 10;  // 图标下方10像素
  // "启动中…"共4个字符，每个16像素宽，居中显示
  const int16_t textX = (screenWidth - 16 * 4) / 2;
  drawChineseText(textX, textY, "启动中…", 16);
  
  yield();  // 让出CPU时间，确保显示完成
}
