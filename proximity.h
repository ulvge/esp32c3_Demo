#ifndef __PROXIMITY_H
#define __PROXIMITY_H

#include <esp_wifi.h>

// ===== 可调参数（实测后修改） =====
const int RSSI_NEAR_THRESHOLD = -55; // 大于等于此值 → 近
const int RSSI_FAR_THRESHOLD = -62;  // 小于此值 → 远
                                     // 中间 -56 ~ -61 是死区，保持原状态
                                     
const int RSSI_FAR_FAR = -100;  // 默认的 RSSI 值，表示没有设备
const int SAMPLE_COUNT = 10;         // 滑动平均样本数

// ===== 内部状态 =====
int rssiBuffer[SAMPLE_COUNT] = {0};
int rssiIndex = 0;
bool isBufferFull = false;
bool isDevsNear = false; // 当前判定结果

// 读取当前连接客户端的 RSSI（AP 模式下第一个客户端）
int readClientRSSI()
{
    wifi_sta_list_t staList;
    esp_wifi_ap_get_sta_list(&staList);
    if (staList.num > 0)
    {
        return staList.sta[0].rssi;
    }
    return RSSI_FAR_FAR; // 没有客户端时返回一个很弱的值
}

// 更新滑动平均并判断近/远，返回是否“近”
bool isDevsProximity()
{
    int raw = readClientRSSI();

    // 写入环形缓冲
    rssiBuffer[rssiIndex] = raw;
    rssiIndex = (rssiIndex + 1) % SAMPLE_COUNT;
    if (rssiIndex == 0)
        isBufferFull = true;

    // 求平均
    int validDataCount = isBufferFull ? SAMPLE_COUNT : rssiIndex;
    if (validDataCount == 0)
        return isDevsNear;
    long sum = 0;
    for (int i = 0; i < validDataCount; i++)
    {
        sum += rssiBuffer[i];
    }
    int avg = sum / validDataCount;

    // 迟滞判断
    if (!isDevsNear && avg >= RSSI_NEAR_THRESHOLD)
    {
        isDevsNear = true;
    }
    else if (isDevsNear && avg < RSSI_FAR_THRESHOLD)
    {
        isDevsNear = false;
    }

    return isDevsNear;
}

// 给网页用的原始平均值（调试用）
int getAverageRSSI()
{
    int validDataCount = isBufferFull ? SAMPLE_COUNT : rssiIndex;
    if (validDataCount == 0)
        return RSSI_FAR_FAR;
    long sum = 0;
    for (int i = 0; i < validDataCount; i++)
        sum += rssiBuffer[i];
    
    int avg = sum / validDataCount;
    return avg;
}

#endif