#ifndef __PROXIMITY_BLE_H
#define __PROXIMITY_BLE_H

#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===== 配置区：按你的手机修改 =====
static const char* TARGET_BLE_NAME = "haha";  // 改成手机蓝牙名
static const int BLE_RSSI_NEAR_THRESHOLD = -65;  // 大于此值 → 近
static const int BLE_RSSI_FAR_THRESHOLD = -72;  // 小于此值 → 远
static const int BLE_RSSI_FAR_FAR = -100;  // 默认的 RSSI 值，表示没有设备
static const int BLE_SAMPLE_COUNT = 10;

// ===== 内部状态 =====
static int BLE_rssiBuffer[BLE_SAMPLE_COUNT] = {0};
static int BLE_rssiIndex = 0;
static bool BLE_isBufferFull = false;
static bool BLE_isDevsNear = false;
static volatile int BLE_latestRSSI = -100;  // BLE 回调写入，主循环读取
int BLE_getAverageRSSI();
// ===== BLE 扫描回调 =====
class ProximityCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) {
        if (dev.haveName() && dev.getName() == TARGET_BLE_NAME) {
            BLE_latestRSSI = dev.getRSSI();  // 只记录目标设备的 RSSI
        }
    }
};

BLEScan* pScan = nullptr;

// ===== 初始化 BLE =====
void BLE_initProximity() {
    BLEDevice::init("ESP32-DIY-Scanner");
    pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new ProximityCallbacks());
    pScan->setActiveScan(false);  // 被动扫描，省功耗
    pScan->setInterval(200);       // 扫描间隔 200ms
    pScan->setWindow(150);         // 每次监听 150ms（占空比 75%）
    pScan->start(0, false);        // duration=0 持续扫描，不阻塞
}

// ===== 迟滞判断：近/远 =====
bool BLE_isDevsProximity() {
    int avg = BLE_getAverageRSSI();

    if (avg == -100) {
        BLE_isDevsNear = false;  // 扫不到设备，判定为远
        return BLE_isDevsNear;
    }

    if (!BLE_isDevsNear && avg >= BLE_RSSI_NEAR_THRESHOLD) {
        BLE_isDevsNear = true;
    } else if (BLE_isDevsNear && avg < BLE_RSSI_FAR_THRESHOLD) {
        BLE_isDevsNear = false;
    }

    return BLE_isDevsNear;
}
// ===== 获取平均 RSSI =====
int BLE_getAverageRSSI() {
    int raw = BLE_latestRSSI;

    // 如果没扫到目标设备，直接返回很弱的值
    if (raw <= BLE_RSSI_FAR_FAR) {
        return BLE_RSSI_FAR_FAR;
    }

    BLE_rssiBuffer[BLE_rssiIndex] = raw;
    BLE_rssiIndex = (BLE_rssiIndex + 1) % BLE_SAMPLE_COUNT;
    if (BLE_rssiIndex == 0) BLE_isBufferFull = true;

    int count = BLE_isBufferFull ? BLE_SAMPLE_COUNT : BLE_rssiIndex;
    if (count == 0) return raw;

    long sum = 0;
    for (int i = 0; i < count; i++) sum += BLE_rssiBuffer[i];
    return sum / count;
}


#endif