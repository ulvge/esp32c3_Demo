#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h" // 引入独立存放的网页
#include "style_css.h"  // 引入独立存放的网页
#include "proximity_wifi.h"  // 检测距离的库
#include <ArduinoJson.h>

// ====== 热点配置 ======
const char *AP_SSID = "5532";      // 手机搜到的 WiFi 名
const char *AP_PASS = "123456780"; // 热点密码，至少8位；想免密就改成 ""

WebServer server(80);
String g_lastReceivedText1 = "";
String g_lastReceivedText2 = "";
unsigned long bootMillis = 0; // 记录开机时刻，用于算运行时间

// ====== 路由处理 ======
void handleRoot()
{
    server.send_P(500, "text/html", g_indexHTML); // 注意用 send_P 配合 PROGMEM
}
void handleStyle()
{
    server.send_P(200, "text/css", g_styleCSS);
}
void handleSubmit()
{
    String box = server.arg("box");
    String text = server.arg("text");

    if (box == "1")
    {
        g_lastReceivedText1 = text; // 记录下来，方便后面查看
        Serial.println("text1 Received: " + text);
        server.send(200, "text/html", "框1 已收到：" + text);
    }
    else if (box == "2")
    {
        g_lastReceivedText2 = text; // 记录下来，方便后面查看
        Serial.println("text2 Received: " + text);
        server.send(200, "text/html", "框2 已收到：" + text);
    }
    else
    {
        server.send(200, "text/html", "错误：缺少 box 参数");
    }
}
// 把秒数转成 "X天X小时X分X秒" 格式
String formatUptime(unsigned long seconds) {
  unsigned long days    = seconds / 86400;
  unsigned long hours   = (seconds % 86400) / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long secs    = seconds % 60;

  String result = "";
  if (days > 0)    result += String(days)    + "天";
  if (days > 0 || hours > 0)   result += String(hours)   + "小时";
  if (days > 0 || hours > 0 || minutes > 0) result += String(minutes) + "分";
  result += String(secs) + "秒";

  return result;
}

// ---- 返回实时状态（JSON） ----
void handleStatus() {
  float temp = temperatureRead();
  unsigned long uptimeSec = (millis() - bootMillis) / 1000;

  StaticJsonDocument<512> doc;
  doc["isDevNear"]   = wifi_isDevsProximity();
  doc["currentRSSI"] = wifi_getAverageRSSI();
  doc["temp"]        = temp;
  doc["uptime"]      = formatUptime(uptimeSec);
  doc["lastText1"]   = g_lastReceivedText1;
  doc["lastText2"]   = g_lastReceivedText2;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}
void setup()
{
    Serial.begin(115200);

    // ====== 开启 AP 热点 ======
    WiFi.softAP(AP_SSID, AP_PASS);

    IPAddress ip = WiFi.softAPIP();
    Serial.print("wifi AP launched with SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Please visit http://");
    Serial.println(ip); // 默认是 192.168.4.1

    // ====== 注册路由 ======
    server.on("/", handleRoot);
    server.on("/style.css", handleStyle);
    server.on("/submit", handleSubmit);
    server.on("/status", handleStatus); // 新增

    server.begin();
    Serial.println("AP server started successfully.");
}

bool isDevsNearLast = false;
unsigned long lastCheck = 0;
void loop()
{
    server.handleClient();

    // 每 500ms 判断一次，和网页刷新节奏一致
    if (millis() - lastCheck >= 500)
    {
        lastCheck = millis();
        bool isDevsNearCurrent = wifi_isDevsProximity();

        if (isDevsNearCurrent == true && isDevsNearLast == false)
        { // 由远变近
            // ===== 手机靠近时执行的动作 =====
            // 例如：点亮 LED、启动某个功能、发串口通知
            Serial.println("Device is entered near range");
        }
        else if (isDevsNearCurrent == false && isDevsNearLast == true)
        { // 由近变远
            Serial.println("Device is left near range");
        }
        else
        {
            // 状态未变化，不做处理
        }
        isDevsNearLast = isDevsNearCurrent;
    }
}