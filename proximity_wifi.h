#ifndef __PROXIMITY_WIFI_H
#define __PROXIMITY_WIFI_H

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include <esp_wifi.h>

/* =====================================================================
 * WiFi 模式下的“距离”检测（读 AP 下已连接客户端的 RSSI）
 * ---------------------------------------------------------------------
 * 和 BLE 版本一样，把“采样”和“读取”分开：
 *   wifi_update() 负责采样/平滑/判定（主循环里周期性调用）
 *   wifi_getAverageRSSI() / wifi_isDevsProximity() 只读缓存，没有副作用
 * 之前的实现在“读平均值”的函数里写采样缓冲，被 /status 和 loop 各读一次，
 * 采样频率就变成了实际的两倍，而且没有客户端时把 -100 写进缓冲拉低平均值。
 * ===================================================================== */

// ============ 可调参数（实测后修改）============
static const int WIFI_RSSI_NEAR_THRESHOLD = -55;  // ≥ 此值 -> 近
static const int WIFI_RSSI_FAR_THRESHOLD = -62;   // < 此值 -> 远
static const int WIFI_RSSI_NONE = -100;           // 无客户端
static const unsigned long WIFI_STALE_MS = 4000;  // 多久没数据认为断开
static const float WIFI_EMA_ALPHA = 0.35f;

// ============ 内部状态 ============
static int WIFI_outRSSI = WIFI_RSSI_NONE;
static bool WIFI_outNear = false;
static int WIFI_clientCount = 0;

static float WIFI_ema = 0.0f;
static bool WIFI_emaValid = false;
static unsigned long WIFI_lastSeen = 0;
static unsigned long WIFI_lastUpdate = 0;

// 读取当前连接客户端里信号最强的那个（AP 模式下）
static int wifi_readClientRSSI(int *count) {
    wifi_sta_list_t staList;
    memset(&staList, 0, sizeof(staList));

    if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK || staList.num == 0) {
        if (count) *count = 0;
        return WIFI_RSSI_NONE;
    }
    if (count) *count = staList.num;

    int best = WIFI_RSSI_NONE;
    for (int i = 0; i < staList.num; i++) {
        if (staList.sta[i].rssi > best) best = staList.sta[i].rssi;
    }
    return best;
}

// 周期更新（主循环里调用，内部节流）
void wifi_update() {
    const unsigned long now = millis();
    if (now - WIFI_lastUpdate < 150) return;
    WIFI_lastUpdate = now;

    int cnt = 0;
    int raw = wifi_readClientRSSI(&cnt);
    WIFI_clientCount = cnt;

    if (raw == WIFI_RSSI_NONE) {
        // 没客户端：只在长时间没数据时才归零，避免页面闪烁
        if (WIFI_lastSeen == 0 || (now - WIFI_lastSeen) > WIFI_STALE_MS) {
            WIFI_emaValid = false;
            WIFI_outRSSI = WIFI_RSSI_NONE;
        }
    } else {
        WIFI_lastSeen = now;
        if (!WIFI_emaValid) {
            WIFI_ema = (float)raw;
            WIFI_emaValid = true;
        } else {
            WIFI_ema += WIFI_EMA_ALPHA * ((float)raw - WIFI_ema);
        }
        WIFI_outRSSI = (int)lroundf(WIFI_ema);
    }

    // 迟滞判断
    if (WIFI_outRSSI == WIFI_RSSI_NONE) {
        WIFI_outNear = false;
    } else if (!WIFI_outNear && WIFI_outRSSI >= WIFI_RSSI_NEAR_THRESHOLD) {
        WIFI_outNear = true;
    } else if (WIFI_outNear && WIFI_outRSSI < WIFI_RSSI_FAR_THRESHOLD) {
        WIFI_outNear = false;
    }
}

// ============ 对外只读接口 ============
bool wifi_isDevsProximity() { return WIFI_outNear; }
int wifi_getAverageRSSI() { return WIFI_outRSSI; }
int wifi_getClientCount() { return WIFI_clientCount; }

#endif
