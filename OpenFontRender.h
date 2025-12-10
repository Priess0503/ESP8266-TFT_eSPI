#ifndef OpenFontRender_h
#define OpenFontRender_h

#include <TFT_eSPI.h>
#include <pgmspace.h>  // 用于读取 PROGMEM 数据

// 声明外部字模数组（定义在 MyChineseFont.h 中，存储在 PROGMEM）
extern const unsigned char chineseFont[][32];

class OpenFontRender {
  private:
    TFT_eSPI* _tft;          // 绑定TFT屏幕对象
    uint16_t _textColor;     // 文字颜色
    uint16_t _bgColor;       // 背景颜色
    uint8_t _fontSize;       // 字体大小（固定16，适配你的16x16点阵）
    int16_t _cursorX;        // 显示起始X坐标
    int16_t _cursorY;        // 显示起始Y坐标

    // 中文与字模数组的映射（顺序必须和 MyChineseFont.h 中的字模完全一致！）
    // 注意：UTF-8 中文字符每个占 3 字节
    // 顺序必须与 MyChineseFont.h 中数组一致：系 统 设 备 内 存 磁 盘 上 行 下 ： 服 务 器
    const char* _chineseList = "系统设备内存磁盘上行下：服务器树莓派腾讯";  // 18 个字符
    const uint8_t _chineseCount = 20;                     // 更新为 15（含：服 务 器）
    const uint8_t _utf8CharBytes = 3;                     // UTF-8 中文字符字节数
    const uint8_t _dotWidth = 16;                         // 点阵宽度（16x16）
    const uint8_t _dotHeight = 16;                        // 点阵高度（16x16）

  public:
    // 构造函数（初始化默认值）
    OpenFontRender() {
      _tft = NULL;
      _textColor = TFT_WHITE;
      _bgColor = TFT_BLACK;
      _fontSize = 16;
      _cursorX = 0;
      _cursorY = 0;
    }

    // 1. 绑定TFT屏幕（主程序调用：render.setTFT(&tft)）
    void setTFT(TFT_eSPI* tft) {
      _tft = tft;
    }

    // 2. 设置文字颜色（主程序调用：render.setColor(TFT_WHITE)）
    void setColor(uint16_t color) {
      _textColor = color;
    }

    // 3. 设置背景颜色（主程序调用：render.setBackColor(TFT_BLACK)）
    void setBackColor(uint16_t bgColor) {
      _bgColor = bgColor;
    }

    // 4. 设置字体大小（主程序调用：render.setFontSize(fontSize)）
    void setFontSize(uint8_t size) {
      _fontSize = size;  // 强制16，适配你的字模
    }

    // 5. 设置显示起始坐标（主程序调用：render.setCursor(x, y)）
    void setCursor(int16_t x, int16_t y) {
      _cursorX = x;
      _cursorY = y;
    }

    // 6. 核心：渲染中文（主程序调用：render.print(text)）
    void print(const String& text) {
      // 检查屏幕是否绑定、字体大小是否适配
      if (_tft == NULL || _fontSize != 16) return;

      int16_t currentX = _cursorX;  // 当前绘制X坐标
      int16_t currentY = _cursorY;  // 当前绘制Y坐标

      // 遍历要显示的文本，逐个解析中文（UTF-8 编码）和 ASCII 字符
      for (int i = 0; i < text.length(); ) {
        int dotIndex = -1;  // 字模数组中的索引（默认未找到）
        bool isASCII = false;  // 标记是否为 ASCII 字符
        
        // 检查是否为 ASCII 字符（0x00-0x7F，单字节）
        if ((text[i] & 0x80) == 0) {
          // ASCII 字符（包括冒号、空格等）
          isASCII = true;
          // 使用 TFT 内置字体显示 ASCII 字符
          _tft->setTextFont(2);  // 使用字体 2（约 16 像素高，与中文字符对齐）
          _tft->setTextColor(_textColor, _bgColor);
          _tft->setCursor(currentX, currentY);
          _tft->print((char)text[i]);
          // ASCII 字符宽度约为 8 像素（字体 2），但为了对齐，我们使用 10 像素
          currentX += 10;
          i++;  // 跳过 1 字节
          continue;
        }
        
        // UTF-8 中文字符占 3 字节，检查是否匹配
        if (i + 2 < text.length() && (text[i] & 0xE0) == 0xE0) {
          // 可能是 UTF-8 中文字符（首字节 0xE0-0xEF）
          // 直接比较 3 个字节
          for (int j = 0; j < _chineseCount; j++) {
            int listOffset = j * _utf8CharBytes;  // 每个中文字符在列表中的偏移
            // 比较 3 个字节是否完全匹配
            if (text[i] == _chineseList[listOffset] &&
                text[i + 1] == _chineseList[listOffset + 1] &&
                text[i + 2] == _chineseList[listOffset + 2]) {
              dotIndex = j;
              i += _utf8CharBytes;  // 跳过 3 字节
              break;
            }
          }
        }
        
        // 如果没找到匹配的中文，跳过当前字节（可能是其他 UTF-8 字符）
        if (dotIndex == -1 && !isASCII) {
          i++;  // 跳过当前字节
          continue;
        }

        // 找到中文字模，绘制16x16点阵
        if (dotIndex != -1) {
          // 注意：chineseFont 存储在 PROGMEM 中，必须使用 pgm_read_byte 读取
          // 逐行绘制（16行）
          for (int row = 0; row < _dotHeight; row++) {
            // 每行2字节（16列 = 8位×2字节）
            // 从 PROGMEM 读取数据
            uint8_t byteHigh = pgm_read_byte(&chineseFont[dotIndex][row * 2]);     // 高8位（前8列）
            uint8_t byteLow = pgm_read_byte(&chineseFont[dotIndex][row * 2 + 1]); // 低8位（后8列）

            // 逐列绘制（16列）
            for (int col = 0; col < _dotWidth; col++) {
              uint16_t pixelColor = _bgColor;  // 默认背景色

              // 解析当前像素是否需要显示文字颜色
              if (col < 8) {
                // 前8列：取byteHigh的对应位（从高位到低位）
                if (byteHigh & (0x80 >> col)) {
                  pixelColor = _textColor;
                }
              } else {
                // 后8列：取byteLow的对应位（从高位到低位）
                if (byteLow & (0x80 >> (col - 8))) {
                  pixelColor = _textColor;
                }
              }

              // 在屏幕上绘制单个像素
              _tft->drawPixel(currentX + col, currentY + row, pixelColor);
            }
          }

          // 绘制完一个中文，X坐标偏移16像素（16x16点阵宽度）
          currentX += _dotWidth;
        }
      }
    }
};

#endif