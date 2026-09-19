#ifndef __PROXIMITY_WIFI_H
#define __PROXIMITY_WIFI_H

#include <esp_wifi.h>

// ===== 可调参数（实测后修改） =====
static const int WIFI_RSSI_NEAR_THRESHOLD = -55; // 大于等于此值 → 近
static const int WIFI_RSSI_FAR_THRESHOLD = -62;  // 小于此值 → 远
                                     // 中间 -56 ~ -61 是死区，保持原状态
                                     
static const int WIFI_RSSI_FAR_FAR = -100;  // 默认的 RSSI 值，表示没有设备
static const int WIFI_SAMPLE_COUNT = 10;         // 滑动平均样本数

// ===== 内部状态 =====
static int WIFI_rssiBuffer[WIFI_SAMPLE_COUNT] = {0};
static int WIFI_rssiIndex = 0;
static bool WIFI_isBufferFull = false;
static bool WIFI_isDevsNear = false; // 当前判定结果

// 读取当前连接客户端的 RSSI（AP 模式下第一个客户端）
static int wifi_readClientRSSI()
{
    wifi_sta_list_t staList;
    esp_wifi_ap_get_sta_list(&staList);
    if (staList.num > 0)
    {
        return staList.sta[0].rssi;
    }
    return WIFI_RSSI_FAR_FAR; // 没有客户端时返回一个很弱的值
}

// 更新滑动平均并判断近/远，返回是否“近”
bool wifi_isDevsProximity()
{
    int raw = wifi_readClientRSSI();

    // 写入环形缓冲
    WIFI_rssiBuffer[WIFI_rssiIndex] = raw;
    WIFI_rssiIndex = (WIFI_rssiIndex + 1) % WIFI_SAMPLE_COUNT;
    if (WIFI_rssiIndex == 0)
        WIFI_isBufferFull = true;

    // 求平均
    int validDataCount = WIFI_isBufferFull ? WIFI_SAMPLE_COUNT : WIFI_rssiIndex;
    if (validDataCount == 0)
        return WIFI_isDevsNear;
    long sum = 0;
    for (int i = 0; i < validDataCount; i++)
    {
        sum += WIFI_rssiBuffer[i];
    }
    int avg = sum / validDataCount;

    // 迟滞判断
    if (!WIFI_isDevsNear && avg >= WIFI_RSSI_NEAR_THRESHOLD)
    {
        WIFI_isDevsNear = true;
    }
    else if (WIFI_isDevsNear && avg < WIFI_RSSI_FAR_THRESHOLD)
    {
        WIFI_isDevsNear = false;
    }

    return WIFI_isDevsNear;
}

// 给网页用的原始平均值（调试用）
int wifi_getAverageRSSI()
{
    int validDataCount = WIFI_isBufferFull ? WIFI_SAMPLE_COUNT : WIFI_rssiIndex;
    if (validDataCount == 0)
        return WIFI_RSSI_FAR_FAR;
    long sum = 0;
    for (int i = 0; i < validDataCount; i++)
        sum += WIFI_rssiBuffer[i];
    
    int avg = sum / validDataCount;
    return avg;
}

#endif