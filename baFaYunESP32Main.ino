#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h"   // 引入独立存放的网页

// ====== 热点配置 ======
const char* AP_SSID = "5532";      // 手机搜到的 WiFi 名
const char* AP_PASS = "123456780";       // 热点密码，至少8位；想免密就改成 ""

WebServer server(80);
String receivedText = "";

// ====== 路由处理 ======
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);  // 注意用 send_P 配合 PROGMEM
}

void handleSubmit() {
  if (server.hasArg("text")) {
    receivedText = server.arg("text");
    Serial.println("收到: " + receivedText);

    String statusMsg = "已收到：" + receivedText +
                       "<br>长度：" + String(receivedText.length());
    server.send(200, "text/html", statusMsg);
  } else {
    server.send(200, "text/html", "错误：没有收到数据");
  }
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
  server.on("/submit", handleSubmit);

  server.begin();
  Serial.println("服务器已启动");
}

void loop() {
  server.handleClient();
}