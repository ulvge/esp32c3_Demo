#ifndef __IRK_MANAGER_H
#define __IRK_MANAGER_H

#include <Preferences.h>

Preferences prefs;
String g_capturedIRK = "";      // 当前捕获到的 IRK（内存）
bool   g_irkSaved = false;      // 是否已保存到 NVS

// 从 NVS 读取已保存的 IRK
void loadIRKFromNVS() {
  prefs.begin("ble_irk", true);
  g_capturedIRK = prefs.getString("phone_irk", "");
  prefs.end();
  g_irkSaved = (g_capturedIRK.length() > 0);
  if (g_irkSaved) {
    Serial.println("已加载 IRK: " + g_capturedIRK);
  }
}

// 保存 IRK 到 NVS
void saveIRKToNVS() {
  if (g_capturedIRK.length() == 0) return;
  prefs.begin("ble_irk", false);
  prefs.putString("phone_irk", g_capturedIRK);
  prefs.end();
  g_irkSaved = true;
  Serial.println("IRK 已保存: " + g_capturedIRK);
}

// 清除已保存的 IRK（方便重新配对）
void clearIRK() {
  prefs.begin("ble_irk", false);
  prefs.remove("phone_irk");
  prefs.end();
  g_capturedIRK = "";
  g_irkSaved = false;
}

#endif