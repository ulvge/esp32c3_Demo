#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "index_html.h"      // 网页 HTML
#include "style_css.h"       // 网页样式
#include "proximity_wifi.h"  // WiFi RSSI 检测
#include "proximity_BLE.h"   // BLE RSSI 检测

#define RSSI_FROM_WIFI 1
#define RSSI_FROM_BLE 2
static const int g_rssiSource = RSSI_FROM_BLE;  // 默认使用 BLE

// ====== 热点配置 ======
const char *AP_SSID = "5532";       // 手机搜到的 WiFi 名
const char *AP_PASS = "123456780";  // 至少 8 位；想免密就改成 ""

WebServer server(80);

String g_lastReceivedText1 = "";
String g_lastReceivedText2 = "";
unsigned long bootMillis = 0;

// ====== 页面 ======
void handleRoot() {
    // 原来这里发的是 500，浏览器会当成“服务器错误”处理，页面直接打不开/显示空白
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html", g_indexHTML);
}

void handleStyle() {
    server.sendHeader("Cache-Control", "max-age=3600");
    server.send_P(200, "text/css", g_styleCSS);
}

void handleSubmit() {
    String box = server.arg("box");
    String text = server.arg("text");

    // 注意：必须带 charset=utf-8，否则中文在手机上会乱码
    if (box == "1") {
        g_lastReceivedText1 = text;
        Serial.println("text1 Received: " + text);
        server.send(200, "text/plain; charset=utf-8", "框1 已收到：" + text);
    } else if (box == "2") {
        g_lastReceivedText2 = text;
        Serial.println("text2 Received: " + text);
        server.send(200, "text/plain; charset=utf-8", "框2 已收到：" + text);
    } else {
        server.send(200, "text/plain; charset=utf-8", "错误：缺少 box 参数");
    }
}

// 拼 JSON 时的字符串转义（设备名里可能有引号/反斜杠）
static String jsonEsc(const char *s) {
    String o;
    if (!s) return o;
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((unsigned char)c < 0x20) {
            o += ' ';
        } else {
            o += c;
        }
    }
    return o;
}

// ---- 附近设备列表（供网页点选锁定）----
void handlePeers() {
    // 16 个结构体约 1.1KB，必须放静态区，别压在 HTTP 任务的栈上
    static BlePeer buf[BLE_MAX_SLOTS];
    const int n = BLE_copyPeers(buf, BLE_MAX_SLOTS);
    const unsigned long now = millis();
    const char *lockedMac = BLE_getLockedMac();
    const char *targetMac = BLE_getTargetMac();

    String json;
    json.reserve(1400);
    json += F("{\"lock\":\"");
    json += BLE_getLockMode();
    json += F("\",\"lockedMac\":\"");
    json += lockedMac;
    json += F("\",\"keyword\":\"");
    json += jsonEsc(BLE_getTargetKeyword());
    json += F("\",\"reacq\":");
    json += (BLE_wasReacquired() ? "true" : "false");
    json += F(",\"peers\":[");

    bool first = true;
    for (int i = 0; i < n; i++) {
        const unsigned long age = (unsigned long)(now - buf[i].lastSeen);
        if (age > 15000) continue;  // 太久没出现的设备不显示，避免列表刷屏
        if (!first) json += ',';
        first = false;

        json += F("{\"m\":\"");
        json += buf[i].mac;
        json += F("\",\"n\":\"");
        json += jsonEsc(buf[i].name);
        json += F("\",\"r\":");
        json += buf[i].rssi;
        json += F(",\"t\":");
        json += (int)buf[i].addrType;
        json += F(",\"a\":");
        json += age;
        json += F(",\"p\":");
        json += (unsigned long)buf[i].pkt;
        json += F(",\"id\":\"");
        if (buf[i].idIdx >= 0) json += jsonEsc(BLE_getIdName(buf[i].idIdx));
        json += '\"';
        json += F(",\"tg\":");
        json += (ble_macEquals(buf[i].mac, targetMac) ? 1 : 0);
        json += '}';
    }
    json += F("]}");

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

// ---- 网页上点一下设备就锁定它（写进 NVS，掉电不丢）----
void handleSelect() {
    String mac = server.arg("mac");
    mac.trim();
    if (mac.length() > 0 && mac.length() != 17) {
        server.send(400, "text/plain; charset=utf-8", "MAC 格式应为 AA:BB:CC:DD:EE:FF");
        return;
    }
    BLE_setLock(mac.c_str());

    String json = "{\"ok\":true,\"lock\":\"";
    json += BLE_getLockMode();
    json += "\",\"lockedMac\":\"";
    json += BLE_getLockedMac();
    json += "\"}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

// ---- 白名单（IRK / MAC）：认出“我的手机” ----
static String hexOnly(const String &in) {
    String o;
    for (unsigned i = 0; i < in.length(); i++) {
        char c = in[i];
        if (isxdigit((unsigned char)c)) o += (char)tolower((unsigned char)c);
    }
    return o;
}

static String normMac(const String &in) {
    String h = hexOnly(in);
    if (h.length() != 12) return "";
    String o;
    for (int i = 0; i < 6; i++) {
        if (i) o += ':';
        o += h.substring(i * 2, i * 2 + 2);
    }
    return o;
}

void handleIds() {
    BleId d;
    String json;
    json.reserve(700);
    json += F("{\"max\":");
    json += BLE_MAX_IDS;
    json += F(",\"items\":[");
    bool first = true;
    for (int i = 0; i < BLE_MAX_IDS; i++) {
        if (!BLE_getId(i, &d)) continue;
        if (!first) json += ',';
        first = false;
        json += F("{\"i\":");
        json += i;
        json += F(",\"n\":\"");
        json += jsonEsc(d.name);
        json += F("\",\"m\":\"");
        json += d.mac;
        json += F("\",\"irk\":\"");
        if (d.hasIrk) {
            char hex[9];
            for (int k = 0; k < 4; k++) snprintf(&hex[k * 2], 3, "%02x", d.irk[k]);
            hex[8] = 0;
            json += hex;
            json += F("...");
        }
        json += F("\"}");
    }
    json += F("]}");

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

void handleIdAdd() {
    String name = server.arg("name");
    name.trim();
    if (name.length() == 0) name = "device";
    if (name.length() > 19) name = name.substring(0, 19);

    String mac = normMac(server.arg("mac"));
    String irk = hexOnly(server.arg("irk"));

    if (irk.length() > 0 && irk.length() != 32) {
        server.send(400, "text/plain; charset=utf-8", "IRK 必须是 32 个十六进制字符（16 字节）");
        return;
    }
    if (mac.length() != 17 && irk.length() != 32) {
        server.send(400, "text/plain; charset=utf-8", "IRK 和 MAC 至少填一个");
        return;
    }

    int slot = BLE_addId(name.c_str(),
                         mac.length() == 17 ? mac.c_str() : nullptr,
                         irk.length() == 32 ? irk.c_str() : nullptr);
    if (slot < 0) {
        server.send(409, "text/plain; charset=utf-8", "白名单已满（最多 8 条）或参数不合法");
        return;
    }
    String json = "{\"ok\":true,\"idx\":" + String(slot) + "}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

void handleIdDel() {
    int i = server.arg("i").toInt();
    bool ok = BLE_removeId(i);
    server.sendHeader("Cache-Control", "no-store");
    server.send(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- 抓取 IRK：让手机配对一次，自动把它分发的 IRK 存进白名单 ----
void handleCapStart() {
    String name = server.arg("name");
    name.trim();
    if (name.length() == 0) name = "phone";
    if (name.length() > 19) name = name.substring(0, 19);

    bool ok = BLE_captureStart(name.c_str());
    server.sendHeader("Cache-Control", "no-store");
    server.send(ok ? 200 : 500, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"初始化失败\"}");
}

void handleCapStop() {
    BLE_captureStop();
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleCapState() {
    String json = "{\"st\":";
    json += (int)BLE_captureState();
    json += ",\"msg\":\"";
    json += jsonEsc(BLE_captureMsg());
    json += "\",\"irk\":\"";
    json += BLE_captureIrkHex();
    json += "\",\"mac\":\"";
    json += BLE_capturePeerMac();
    json += "\",\"id\":\"";
    json += BLE_capturePeerId();
    json += "\",\"name\":\"";
    json += jsonEsc(BLE_captureLabel());
    json += "\",\"slot\":";
    json += (int)BLE_captureSlot();
    json += "}";

    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

// 把秒数转成 "X天X小时X分X秒"
String formatUptime(unsigned long seconds) {
    unsigned long days = seconds / 86400;
    unsigned long hours = (seconds % 86400) / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    unsigned long secs = seconds % 60;

    String result = "";
    if (days > 0) result += String(days) + "天";
    if (days > 0 || hours > 0) result += String(hours) + "小时";
    if (days > 0 || hours > 0 || minutes > 0) result += String(minutes) + "分";
    result += String(secs) + "秒";
    return result;
}

// ---- 返回实时状态（JSON）----
void handleStatus() {
    unsigned long uptimeSec = (millis() - bootMillis) / 1000;

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(1024);
#endif

    if (g_rssiSource == RSSI_FROM_WIFI) {
        doc["rssiSource"] = "WiFi";
        doc["isDevNear"] = wifi_isDevsProximity();
        doc["currentRSSI"] = wifi_getAverageRSSI();
        doc["rawRSSI"] = wifi_getAverageRSSI();
        doc["clientCount"] = wifi_getClientCount();
        doc["devName"] = "";
        doc["devMac"] = "";
        doc["lastSeenMs"] = -1;
        doc["packets"] = 0;
        doc["nameMatched"] = true;
    } else {
        doc["rssiSource"] = "BLE";
        doc["isDevNear"] = BLE_isDevsProximity();
        doc["currentRSSI"] = BLE_getAverageRSSI();
        doc["rawRSSI"] = BLE_getRawRSSI();
        doc["clientCount"] = 0;
        doc["devName"] = BLE_getTargetName();
        doc["devMac"] = BLE_getTargetMac();
        doc["lastSeenMs"] = BLE_getLastSeenAgeMs();
        doc["packets"] = BLE_getPacketCount();
        doc["nameMatched"] = BLE_isTargetMatched();
        doc["lockMode"] = BLE_getLockMode();
        doc["lockedMac"] = BLE_getLockedMac();
        doc["addrType"] = (int)BLE_getTargetAddrType();
        doc["keyword"] = BLE_getTargetKeyword();
        doc["reacquired"] = BLE_wasReacquired();
        doc["whitelisted"] = BLE_isWhitelisted();
        doc["capturing"] = BLE_isCapturing();
    }

    // ESP32-C3 有内部温度传感器，但读不到时会是 NaN，这里按 null 下发
    float temp = temperatureRead();
    if (isnan(temp)) {
        doc["temp"] = nullptr;
    } else {
        doc["temp"] = temp;
    }

    doc["uptime"] = formatUptime(uptimeSec);
    doc["heap"] = ESP.getFreeHeap();
    doc["heapMin"] = ESP.getMinFreeHeap();
    doc["lastText1"] = g_lastReceivedText1;
    doc["lastText2"] = g_lastReceivedText2;

    String json;
    serializeJson(doc, json);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== ESP32-C3 BLE RSSI Monitor ===");

    // 先初始化 BLE，再开 AP：蓝牙协议栈需要大块连续内存，
    // 放在 WiFi 之后申请容易失败（尤其是开了 HTTP 服务之后）。
    if (g_rssiSource == RSSI_FROM_BLE) {
        BLE_initProximity();
        BLE_update();  // 先跑一次，避免第一次查询时数据全是初始值
    }

    // ====== 开启 AP 热点 ======
    // 用信道 11：BLE 的三个广播信道在 2402/2426/2480MHz，
    // AP 在 1 或 6 信道会和 37/38 信道正面重叠，11 信道离得最远，互相干扰最小。
    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP(AP_SSID, AP_PASS, 11);
    Serial.printf("[WiFi] softAP(%s) = %s\n", AP_SSID, apOk ? "OK" : "FAILED");
    Serial.print("[WiFi] open browser: http://");
    Serial.println(WiFi.softAPIP());
    Serial.printf("[MEM] free heap = %u bytes\n", (unsigned)ESP.getFreeHeap());

    // ====== 注册路由 ======
    server.on("/", handleRoot);
    server.on("/style.css", handleStyle);
    server.on("/submit", handleSubmit);
    server.on("/peers", handlePeers);
    server.on("/select", handleSelect);
    server.on("/ids", handleIds);
    server.on("/id_add", handleIdAdd);
    server.on("/id_del", handleIdDel);
    server.on("/cap_start", handleCapStart);
    server.on("/cap_stop", handleCapStop);
    server.on("/cap_state", handleCapState);
    server.on("/status", handleStatus);
    server.onNotFound([]() {
        server.send(404, "text/plain; charset=utf-8", "404 Not Found");
    });

    server.begin();
    bootMillis = millis();
    Serial.println("[HTTP] server started on port 80");
}

bool isDevsNearLast = false;
unsigned long lastPrint = 0;

void loop() {
    // ★ 这里必须一直能被调用到，网页才不卡。
    //   蓝牙扫描已经是非阻塞的（见 proximity_BLE.h 里的说明），不会卡住 loop。
    server.handleClient();

    if (g_rssiSource == RSSI_FROM_WIFI) {
        wifi_update();
    } else {
        BLE_update();  // 内部 150ms 节流：采样 + 平滑 + 迟滞 + 超时判定
    }

    // 抓取 IRK 的状态机（非阻塞，扫描此时是暂停的）
    if (g_rssiSource == RSSI_FROM_BLE) BLE_capturePoll();

    // 状态变化时打印（每 200ms 判断一次足够了）
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 200) {
        lastCheck = millis();

        bool isDevsNearCurrent = (g_rssiSource == RSSI_FROM_WIFI) ? wifi_isDevsProximity()
                                                                 : BLE_isDevsProximity();
        if (isDevsNearCurrent && !isDevsNearLast) {
            Serial.println("Device is entered near range");
        } else if (!isDevsNearCurrent && isDevsNearLast) {
            Serial.println("Device is left near range");
        }
        isDevsNearLast = isDevsNearCurrent;
    }

    // 每秒打一行调试信息，方便看串口判断到底有没有收到包、内存有没有泄漏
    if (millis() - lastPrint >= 1000) {
        lastPrint = millis();
        if (g_rssiSource == RSSI_FROM_BLE && BLE_isCapturing()) {
            // 抓取期间扫描是停的，RSSI 行没意义，改打状态机
            static const char *CAPST[] = {"idle", "adv", "conn", "pairing", "done", "fail"};
            int cs = (int)BLE_captureState();
            Serial.printf("[CAP] st=%s heap=%u msg=%s\n",
                          CAPST[(cs >= 0 && cs <= 5) ? cs : 0],
                          (unsigned)ESP.getFreeHeap(), BLE_captureMsg());
        } else if (g_rssiSource == RSSI_FROM_BLE) {
            const char *nm = BLE_getTargetName();
            static const char *ATYPE[] = {"pub", "sta", "rpa", "nrpa"};
            int at = (int)BLE_getTargetAddrType();
            Serial.printf("[BLE] rssi=%d raw=%d near=%d wl=%d name=%s mac=%s(%s) lock=%s age=%ldms pkts=%u heap=%u/%u\n",
                          BLE_getAverageRSSI(), BLE_getRawRSSI(), (int)BLE_isDevsProximity(),
                          (int)BLE_isWhitelisted(),
                          (nm[0] ? nm : "(no name)"), BLE_getTargetMac(),
                          ATYPE[(at >= 0 && at <= 3) ? at : 0],
                          BLE_getLockMode(), BLE_getLastSeenAgeMs(),
                          (unsigned)BLE_getPacketCount(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
        } else {
            Serial.printf("[WiFi] rssi=%d near=%d clients=%d heap=%u/%u\n",
                          wifi_getAverageRSSI(), (int)wifi_isDevsProximity(),
                          wifi_getClientCount(),
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
        }
    }

    delay(1);  // 让出 CPU 给 WiFi/蓝牙任务，避免把 loop 任务跑满
}
