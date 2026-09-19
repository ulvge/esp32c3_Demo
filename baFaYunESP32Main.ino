#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h"   // 引入独立存放的网页
#include "style_css.h"   // 引入独立存放的网页

// ====== 热点配置 ======
const char* AP_SSID = "5532";      // 手机搜到的 WiFi 名
const char* AP_PASS = "123456780";       // 热点密码，至少8位；想免密就改成 ""

WebServer server(80);
String g_lastReceivedText1 = "";
String g_lastReceivedText2 = "";
unsigned long bootMillis = 0;   // 记录开机时刻，用于算运行时间

// ====== 路由处理 ======
void handleRoot() {
  server.send_P(200, "text/html", g_indexHTML);  // 注意用 send_P 配合 PROGMEM
}
void handleStyle() {
  server.send_P(200, "text/css", g_styleCSS);
}
void handleSubmit() {
  String box = server.arg("box");
  String text = server.arg("text");

  if (box == "1") {
    g_lastReceivedText1 = text; // 记录下来，方便后面查看
    Serial.println("框1收到: " + text);
    server.send(200, "text/html", "框1 已收到：" + text);
  } else if (box == "2") {
    g_lastReceivedText2 = text; // 记录下来，方便后面查看
    Serial.println("框2收到: " + text);
    server.send(200, "text/html", "框2 已收到：" + text);
  } else {
    server.send(200, "text/html", "错误：缺少 box 参数");
  }
}
// ---- 返回实时状态（JSON） ----
void handleStatus() {
  float temp = temperatureRead();                 // 芯片内部温度，核心自带
  unsigned long uptime = (millis() - bootMillis) / 1000;  // 秒

  String json = "{";
  json += "\"temp\":" + String(temp, 2) + ",";
  json += "\"uptime\":" + String(uptime) + ",";
  json += "\"lastText1\":\"" + g_lastReceivedText1 + "\",";
  json += "\"lastText2\":\"" + g_lastReceivedText2 + "\"";
  json += "}";

  server.send(200, "application/json", json);
}
void setup() {
  Serial.begin(115200);

  // ====== 开启 AP 热点 ======
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress ip = WiFi.softAPIP();
  Serial.print("热点已开启，SSID: ");
  Serial.println(AP_SSID);
  Serial.print("请在手机浏览器访问: http://");
  Serial.println(ip);   // 默认是 192.168.4.1

  // ====== 注册路由 ======
  server.on("/", handleRoot);
  server.on("/style.css", handleStyle);
  server.on("/submit", handleSubmit);
  server.on("/status", handleStatus);   // 新增

  server.begin();
  Serial.println("服务器已启动");
}

void loop() {
  server.handleClient();
}