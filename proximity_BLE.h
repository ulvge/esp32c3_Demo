#ifndef __PROXIMITY_BLE_H
#define __PROXIMITY_BLE_H

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "mbedtls/aes.h"

#if defined(CONFIG_NIMBLE_ENABLED)
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_id.h>
#endif

/* =====================================================================
 * BLE RSSI（信号强度）采集
 * ---------------------------------------------------------------------
 * 设计要点（踩过的坑都在这）：
 * ① NimBLE 下不要用 BLEScan 的回调：同一 MAC 只 new 一个
 *    BLEAdvertisedDevice 且永不释放，每个包都 realloc 追加 payload、
 *    push_back service UUID → 堆被慢慢吃光 → bad_alloc → abort() 重启。
 *    这里直接调 ble_gap_disc() 收原始包，零动态分配。
 * ② 不能写 pScan->start(0, false)：那是阻塞重载，duration=0 永不返回，
 *    loop() 卡死 → 网页打不开。
 * ③ filter_duplicates = 0，否则同一设备只上报一次，RSSI 不刷新。
 * ④ 主动扫描（passive=0），手机名字一般只在 scan response 里。
 * ⑤ 扫描占空比 400/80ms（20%），BLE 与 WiFi AP 共用射频，太高会挤死网页。
 * ⑥ 回调跑在蓝牙 host 任务，共享数据一律进临界区。
 *
 * 【关于“用 MAC 还是用名字认设备”】
 *   BLE 地址有 4 种，稳定性完全不同（这里会自动识别并标注）：
 *    - public          厂商烧死的公开地址，永久不变（多数 IoT/老设备）
 *    - static random   随机生成但固定，直到设备重启/重新配对（部分手机）
 *    - RPA (可解析随机) 手机会周期性更换（约 15 分钟），换完 MAC 就变了
 *    - NRPA (不可解析)  完全随机，会变，且无法反解
 *   所以：iOS / 部分 Android 上锁 MAC 一定会失效 —— 名字才是首选。
 *   本文件的做法是「名字优先、MAC 兜底、网页上还能手动点选锁定」，
 *   并且名字会按 MAC 记住：即使某个包里没带名字（常见），
 *   只要这台设备曾经广播过名字，后续所有包都能认出来。
 * ===================================================================== */

// ============ 配置区 ============
// 目标蓝牙名：不区分大小写的“包含”匹配。
// ★ 支持用逗号分隔多个关键词，任一命中即可，例如："haha,RuiCh,iPhone"
//   留空 "" 表示不按名字匹配。
static const char *BLE_TARGET_NAME = "haha";

// 目标 MAC：精确匹配。只有确认目标设备是 public / static random 地址时才用。
// 留空 "" 表示不用 MAC 匹配。
static const char *BLE_TARGET_MAC = "";

// 名字一直匹配不到时，是否退化为“跟踪信号最强的设备”。
// 注意：仅在「按名字」模式下生效；手动锁定 / MAC 锁定时不会退化，
//       目标离开就是 -100（这样才测得出远近）。
static const bool BLE_FALLBACK_STRONGEST = true;

// 近/远门限（带迟滞）
static const int BLE_RSSI_NEAR_THRESHOLD = -65;
static const int BLE_RSSI_FAR_THRESHOLD = -75;
static const int BLE_RSSI_NONE = -100;

// 扫描参数
static const bool BLE_ACTIVE_SCAN = true;
static const uint16_t BLE_SCAN_INTERVAL_MS = 400;
static const uint16_t BLE_SCAN_WINDOW_MS = 80;
static const int32_t BLE_SCAN_SESSION_MS = 6000;  // 每轮扫描时长，到点自动续扫

static const unsigned long BLE_STALE_MS = 4000;        // 多久没收到包算“不在附近”
static const unsigned long BLE_PEER_FORGET_MS = 30000; // 设备多久没出现就从列表里删掉
static const float BLE_EMA_ALPHA = 0.35f;
static const int BLE_MAX_SLOTS = 16;
static const int BLE_STICKY_DB = 20;  // 兜底模式下的切换去抖

/* 目标地址自己会变化时（手机很常见），是否允许自动接管新地址。
 * 手机（尤其 iPhone）每隔十几分钟换一次随机地址，锁死旧 MAC 会直接失联。
 * 开启后：当“已锁定的目标消失”且“恰好出现 1 台信号接近的新随机地址设备”时，
 * 自动把跟踪目标切到这台新地址上（只改内存，不写 NVS，不会磨损 flash）。
 * 附近同时有好几台手机时会认错 —— 那种场景请改用名字匹配，或把这个开关关掉。 */
static const bool BLE_AUTO_REACQUIRE = true;
static const int BLE_REACQUIRE_DB = 12;                     // 信号接近范围（dB）
static const unsigned long BLE_REACQUIRE_WINDOW_MS = 8000;  // 只接管“刚出现”的设备

// 白名单条数上限（每条 = 一个名字 + 一个 IRK 或一个 MAC）
static const int BLE_MAX_IDS = 8;

// ============ 内部状态 ============
struct BlePeer {
    bool used;
    char mac[18];
    char name[32];
    uint8_t nameType;  // 0=没有名字, 8=短名, 9=完整名
    uint8_t addrType;  // 0=public 1=static 2=RPA 3=NRPA
    int8_t idIdx;      // 命中的白名单下标：-2 待验算, -1 不是, >=0 是
    int rssi;
    uint32_t pkt;
    unsigned long firstSeen;
    unsigned long lastSeen;
};

static BlePeer s_peers[BLE_MAX_SLOTS];
static portMUX_TYPE s_bleMux = portMUX_INITIALIZER_UNLOCKED;

static int s_rawRSSI = BLE_RSSI_NONE;
static unsigned long s_rawSeen = 0;
static char s_rawName[32] = {0};
static char s_rawMac[18] = {0};
static uint8_t s_rawAddrType = 0;
static int s_rawKind = 0;  // 0=没命中, 1=按名字, 2=按配置 MAC, 3=按手动锁定
static uint32_t s_pktCount = 0;
static volatile unsigned long s_lastPktMs = 0;
static volatile bool s_scanning = false;
static volatile bool s_scanPaused = false;  // 抓取 IRK 时暂停扫描（射频让给配对）

// 对外输出（只在 loop 任务里写）
static int s_outRSSI = BLE_RSSI_NONE;
static int s_lastGoodRSSI = BLE_RSSI_NONE;  // 目标最后一次有信号时的值（用于接管时比较）
static int s_outRaw = BLE_RSSI_NONE;
static bool s_outNear = false;
static bool s_outMatched = false;
static bool s_outWhitelisted = false;
static char s_outName[32] = {0};
static char s_outMac[18] = {0};
static uint8_t s_outAddrType = 0;
static long s_outAgeMs = -1;
static uint32_t s_outPackets = 0;

static float s_ema = 0.0f;
static bool s_emaValid = false;
static char s_emaKey[40] = {0};  // 平滑值当前对应的“身份”，换设备就重置
static unsigned long s_lastUpdateMs = 0;
static unsigned long s_lastStartTry = 0;
static char s_lockedMac[18] = {0};  // 兜底模式下当前粘住的设备
static bool s_lockedValid = false;

// 网页上点选锁定的目标（存 NVS，掉电不丢）
static char s_manualMac[18] = {0};
static bool s_manualValid = false;
static bool s_manualLoaded = false;
static unsigned long s_reacqAt = 0;   // 自动接管发生的时间（用于网页提示），0=没有
static unsigned long s_lockSetAt = 0; // 本次锁定生效的时间，用于“锁了个死地址”自愈

// ============ 工具函数 ============
static bool ble_nameContains(const char *hay, const char *needle) {
    if (!hay || !needle || needle[0] == 0) return false;
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        while (j < nn && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (j == nn) return true;
    }
    return false;
}

// 关键词列表，支持 , ; | 分隔（方便同时盯多台设备）
static bool ble_nameInList(const char *name, const char *list) {
    if (!name || !name[0] || !list || !list[0]) return false;
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '|') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ',' && *e != ';' && *e != '|') e++;
        size_t n = (size_t)(e - p);
        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        while (n > 0 && (*p == ' ' || *p == '\t')) { p++; n--; }
        if (n > 0) {
            char kw[24];
            if (n > sizeof(kw) - 1) n = sizeof(kw) - 1;
            memcpy(kw, p, n);
            kw[n] = 0;
            if (ble_nameContains(name, kw)) return true;
        }
        p = e;
    }
    return false;
}

// ble_addr_t.val[] 是小端序（val[5] 才是写出来最前面那个字节）
static void ble_addrToStr(const uint8_t *v, char *out) {
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", v[5], v[4], v[3], v[2], v[1], v[0]);
}

static bool ble_macEquals(const char *a, const char *b) {
    if (!a || !b || b[0] == 0) return false;
    if (strlen(a) < 17 || strlen(b) < 17) return false;
    for (int i = 0; i < 17; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

/* =====================================================================
 * 白名单（身份识别）—— 这才是“认出我的手机”的正确做法
 * ---------------------------------------------------------------------
 * 安卓手机的 BLE 广播地址是 RPA（可解析私有地址），大约每 15 分钟换一次，
 * 名字也不在广播包里，所以「锁 MAC」「按名字」对手机都注定失败。
 *
 * RPA 的构造是标准化的：
 *     RPA = prand(3字节) || hash(3字节)
 *     hash = AES-128(IRK, 0x00...00 || prand) 取前 24 位
 * 也就是说，只要拿到手机自己的 IRK（128 位），就能把任何一个陌生地址
 * 验算一遍：算出来的 hash 对上 → 这台就是我的手机，无论它换了多少地址。
 *
 * 白名单一条 = 名字 + (IRK 或 MAC)：
 *   - 手机用 IRK（认 RPA，换地址照样认）
 *   - 固定地址设备（public / static random）用 MAC
 * ===================================================================== */
struct BleId {
    bool used;
    bool hasIrk;
    char name[20];
    char mac[18];
    uint8_t irk[16];
};

static BleId s_ids[BLE_MAX_IDS];
static portMUX_TYPE s_idMux = portMUX_INITIALIZER_UNLOCKED;

static int ble_hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* RPA 验算：val[] 是协议栈给的小端数组
 *   显示成 aa:bb:cc:dd:ee:ff 时，aa bb cc = prand，dd ee ff = hash
 *   小端数组里 val[5]=aa val[4]=bb val[3]=cc，val[2]=dd val[1]=ee val[0]=ff */
static bool ble_rpaResolve(const uint8_t irk[16], const uint8_t val[6]) {
    uint8_t in[16];
    uint8_t out[16];
    memset(in, 0, sizeof(in));
    in[13] = val[5];  // prand 放在 128 位块的最低 24 位
    in[14] = val[4];
    in[15] = val[3];

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, irk, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    int rc = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
    if (rc != 0) return false;

    return (out[0] == val[2] && out[1] == val[1] && out[2] == val[0]);
}

// 命中白名单则返回下标，否则 -1
static int ble_matchId(const char *mac, const uint8_t *addrVal, uint8_t addrType) {
    for (int i = 0; i < BLE_MAX_IDS; i++) {
        bool used, hasIrk;
        char imac[18];
        uint8_t irk[16];

        portENTER_CRITICAL(&s_idMux);
        used = s_ids[i].used;
        hasIrk = s_ids[i].hasIrk;
        memcpy(imac, s_ids[i].mac, sizeof(imac));
        memcpy(irk, s_ids[i].irk, sizeof(irk));
        portEXIT_CRITICAL(&s_idMux);

        if (!used) continue;
        if (imac[0] && ble_macEquals(mac, imac)) return i;              // MAC 直接对上
        if (hasIrk && addrType != 0 && ble_rpaResolve(irk, addrVal)) return i;  // 随机地址做 RPA 验算
    }
    return -1;
}

// 白名单变动后，让所有设备重新验算一次
static void ble_resetPeerIds() {
    portENTER_CRITICAL(&s_bleMux);
    for (int i = 0; i < BLE_MAX_SLOTS; i++) {
        if (s_peers[i].used) s_peers[i].idIdx = -2;
    }
    portEXIT_CRITICAL(&s_bleMux);
}

static void ble_saveIds() {
    // 先在锁内拷出来，再在锁外拼字符串：临界区里不能做堆分配（String += 会 malloc）
    BleId tmp[BLE_MAX_IDS];
    int n = 0;
    portENTER_CRITICAL(&s_idMux);
    for (int i = 0; i < BLE_MAX_IDS; i++) {
        if (s_ids[i].used) tmp[n++] = s_ids[i];
    }
    portEXIT_CRITICAL(&s_idMux);

    String s = "";
    for (int i = 0; i < n; i++) {
        if (s.length()) s += ';';
        s += tmp[i].name;
        s += '|';
        s += tmp[i].mac;
        s += '|';
        if (tmp[i].hasIrk) {
            char hex[33];
            for (int k = 0; k < 16; k++) snprintf(&hex[k * 2], 3, "%02x", tmp[i].irk[k]);
            hex[32] = 0;
            s += hex;
        }
    }

    Preferences prefs;
    prefs.begin("blescan", false);
    prefs.putString("ids", s);
    prefs.end();
}

static void ble_loadIds() {
    Preferences prefs;
    prefs.begin("blescan", true);
    String all = prefs.getString("ids", "");
    prefs.end();
    if (!all.length()) return;

    int idx = 0;
    int from = 0;
    while (from < (int)all.length() && idx < BLE_MAX_IDS) {
        int sep = all.indexOf(';', from);
        if (sep < 0) sep = all.length();
        String item = all.substring(from, sep);
        from = sep + 1;

        int p1 = item.indexOf('|');
        int p2 = (p1 >= 0) ? item.indexOf('|', p1 + 1) : -1;
        if (p1 < 0) continue;

        String nm = item.substring(0, p1);
        String mc = (p2 > p1) ? item.substring(p1 + 1, p2) : item.substring(p1 + 1);
        String ik = (p2 > p1) ? item.substring(p2 + 1) : "";

        BleId &d = s_ids[idx];
        memset(&d, 0, sizeof(d));
        d.used = true;
        strncpy(d.name, nm.c_str(), sizeof(d.name) - 1);
        if (mc.length() == 17) strncpy(d.mac, mc.c_str(), sizeof(d.mac) - 1);
        if (ik.length() == 32) {
            bool ok = true;
            for (int k = 0; k < 16; k++) {
                int hi = ble_hexVal(ik[k * 2]);
                int lo = ble_hexVal(ik[k * 2 + 1]);
                if (hi < 0 || lo < 0) { ok = false; break; }
                d.irk[k] = (uint8_t)((hi << 4) | lo);
            }
            d.hasIrk = ok;
        }
        idx++;
    }
}

// 返回下标，-1 = 满了或参数不合法
int BLE_addId(const char *name, const char *mac, const char *irkHex) {
    uint8_t irk[16];
    bool hasIrk = false;

    if (irkHex && strlen(irkHex) == 32) {
        for (int k = 0; k < 16; k++) {
            int hi = ble_hexVal(irkHex[k * 2]);
            int lo = ble_hexVal(irkHex[k * 2 + 1]);
            if (hi < 0 || lo < 0) return -1;
            irk[k] = (uint8_t)((hi << 4) | lo);
        }
        hasIrk = true;
    }
    const bool hasMac = (mac && strlen(mac) == 17);
    if (!hasIrk && !hasMac) return -1;

    // 先在锁外把条目准备好，临界区里只做一次结构体拷贝
    BleId nd;
    memset(&nd, 0, sizeof(nd));
    nd.used = true;
    nd.hasIrk = hasIrk;
    if (hasIrk) memcpy(nd.irk, irk, 16);
    snprintf(nd.name, sizeof(nd.name), "%s", (name && name[0]) ? name : "device");
    for (char *q = nd.name; *q; q++) {
        if (*q == '|' || *q == ';') *q = '_';
    }
    if (hasMac) {
        for (int i = 0; i < 17; i++) nd.mac[i] = (char)tolower((unsigned char)mac[i]);
        nd.mac[17] = 0;
    }

    int slot = -1;
    portENTER_CRITICAL(&s_idMux);
    for (int i = 0; i < BLE_MAX_IDS; i++) {
        if (!s_ids[i].used) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) s_ids[slot] = nd;
    portEXIT_CRITICAL(&s_idMux);

    if (slot < 0) return -1;
    ble_saveIds();
    ble_resetPeerIds();
    Serial.printf("[BLE] whitelist add [%d] name=%s mac=%s irk=%d\n", slot,
                  (name && name[0]) ? name : "device", (hasMac ? mac : "-"), (int)hasIrk);
    return slot;
}

bool BLE_removeId(int idx) {
    if (idx < 0 || idx >= BLE_MAX_IDS) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_idMux);
    if (s_ids[idx].used) {
        memset(&s_ids[idx], 0, sizeof(s_ids[idx]));
        ok = true;
    }
    portEXIT_CRITICAL(&s_idMux);
    if (ok) {
        ble_saveIds();
        ble_resetPeerIds();
        Serial.printf("[BLE] whitelist remove [%d]\n", idx);
    }
    return ok;
}

// 按下标取一条（保留原始下标，网页删除时要用）
bool BLE_getId(int idx, BleId *out) {
    if (idx < 0 || idx >= BLE_MAX_IDS || !out) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_idMux);
    if (s_ids[idx].used) {
        *out = s_ids[idx];
        ok = true;
    }
    portEXIT_CRITICAL(&s_idMux);
    return ok;
}

int BLE_copyIds(BleId *out, int maxOut) {
    int n = 0;
    portENTER_CRITICAL(&s_idMux);
    for (int i = 0; i < BLE_MAX_IDS && n < maxOut; i++) {
        if (!s_ids[i].used) continue;
        out[n++] = s_ids[i];
    }
    portEXIT_CRITICAL(&s_idMux);
    return n;
}

int BLE_getIdCount() {
    int n = 0;
    portENTER_CRITICAL(&s_idMux);
    for (int i = 0; i < BLE_MAX_IDS; i++) if (s_ids[i].used) n++;
    portEXIT_CRITICAL(&s_idMux);
    return n;
}

const char *BLE_getIdName(int idx) {
    static char buf[20];
    buf[0] = 0;
    if (idx < 0 || idx >= BLE_MAX_IDS) return buf;
    portENTER_CRITICAL(&s_idMux);
    if (s_ids[idx].used) memcpy(buf, s_ids[idx].name, sizeof(buf));
    portEXIT_CRITICAL(&s_idMux);
    return buf;
}

/* 判定地址类型：
 *   addrType 来自协议栈（0=PUBLIC 1=RANDOM）；
 *   只有 RANDOM 才需要看最高字节的低 2 位。
 *   0b11 → static random（固定）   0b01 → RPA（约 15 分钟变一次）
 *   0b00 → NRPA（随机、不可解析） */
static uint8_t ble_classifyAddr(uint8_t addrType, const uint8_t *val) {
    if (addrType == 0) return 0;
    uint8_t hi = (uint8_t)(val[5] & 0xC0);
    if (hi == 0xC0) return 1;
    if (hi == 0x40) return 2;
    return 3;
}

static const char *ble_addrTypeTag(uint8_t t) {
    switch (t) {
        case 0: return "pub";
        case 1: return "sta";
        case 2: return "rpa";
        case 3: return "nrpa";
        default: return "?";
    }
}

// 从广播数据里自己抠设备名：0x09 完整名优先，0x08 短名兜底
static uint8_t ble_parseName(const uint8_t *d, uint8_t len, char *out, size_t outSize) {
    out[0] = 0;
    if (!d || len == 0) return 0;
    char shortName[32] = {0};
    bool hasShort = false;

    uint8_t i = 0;
    while ((int)i + 1 < (int)len) {
        uint8_t l = d[i];
        if (l == 0) break;
        if ((int)i + 1 + (int)l > (int)len) break;  // 结构越界，丢弃
        uint8_t type = d[i + 1];
        if ((type == 0x09 || type == 0x08) && l >= 2) {
            size_t n = (size_t)(l - 1);
            if (n >= outSize) n = outSize - 1;
            if (type == 0x09) {
                memcpy(out, &d[i + 2], n);
                out[n] = 0;
                return 9;
            }
            if (n >= sizeof(shortName)) n = sizeof(shortName) - 1;
            memcpy(shortName, &d[i + 2], n);
            shortName[n] = 0;
            hasShort = true;
        }
        i = (uint8_t)(i + l + 1);
    }
    if (hasShort) {
        strncpy(out, shortName, outSize - 1);
        out[outSize - 1] = 0;
        return 8;
    }
    return 0;
}

// 维护“见过的设备”表（调用者已进入临界区）。返回槽位下标。
static int ble_peerUpdate(const char *mac, const char *nm, uint8_t nmType,
                          uint8_t addrType, int rssi, unsigned long now, bool *isNew) {
    int freeIdx = -1;
    int oldestIdx = -1;
    unsigned long oldestSeen = 0;

    for (int i = 0; i < BLE_MAX_SLOTS; i++) {
        BlePeer &p = s_peers[i];
        if (p.used && (unsigned long)(now - p.lastSeen) > BLE_PEER_FORGET_MS) {
            p.used = false;  // 太久没出现，腾出槽位
            p.name[0] = 0;
            p.nameType = 0;
        }
        if (!p.used) {
            if (freeIdx < 0) freeIdx = i;
            continue;
        }
        if (strncmp(p.mac, mac, 17) == 0) {
            p.rssi = rssi;
            p.lastSeen = now;
            p.addrType = addrType;
            if (p.pkt != 0xFFFFFFFFu) p.pkt++;
            // 名字只升级不降级：完整名(9) > 短名(8)，且不会被空名字覆盖
            if (nmType && (p.name[0] == 0 || nmType > p.nameType)) {
                strncpy(p.name, nm, sizeof(p.name) - 1);
                p.name[sizeof(p.name) - 1] = 0;
                p.nameType = nmType;
            }
            if (isNew) *isNew = false;
            return i;
        }
        if (oldestIdx < 0 || (long)(p.lastSeen - oldestSeen) < 0) {
            oldestIdx = i;
            oldestSeen = p.lastSeen;
        }
    }

    int idx = (freeIdx >= 0) ? freeIdx : (oldestIdx >= 0 ? oldestIdx : 0);
    BlePeer &p = s_peers[idx];
    memset(&p, 0, sizeof(p));
    p.used = true;
    p.idIdx = -2;  // 待做白名单验算
    strncpy(p.mac, mac, sizeof(p.mac) - 1);
    if (nmType) {
        strncpy(p.name, nm, sizeof(p.name) - 1);
        p.nameType = nmType;
    }
    p.addrType = addrType;
    p.rssi = rssi;
    p.pkt = 1;
    p.firstSeen = now;
    p.lastSeen = now;
    if (isNew) *isNew = true;
    return idx;
}

/* 当前锁定级别：3=网页手动锁定  2=配置 MAC  1=配置名字  0=自动跟最强 */
static int ble_lockLevel() {
    if (s_manualValid) return 3;
    if (BLE_TARGET_MAC[0]) return 2;
    if (BLE_TARGET_NAME[0]) return 1;
    return 0;
}

// 处理一条广播/扫描响应（蓝牙任务上下文，绝不做动态内存分配）
static void ble_handleReport(const uint8_t *addrVal, uint8_t addrType, int rssi,
                             const uint8_t *data, uint8_t len) {
    char mac[18];
    char nm[32] = {0};
    ble_addrToStr(addrVal, mac);
    uint8_t nmType = ble_parseName(data, len, nm, sizeof(nm));
    const uint8_t aType = ble_classifyAddr(addrType, addrVal);
    const unsigned long now = millis();

    bool isNew = false;
    char shown[32] = {0};
    int idx = -1;
    int8_t idIdx = -1;

    portENTER_CRITICAL(&s_bleMux);
    idx = ble_peerUpdate(mac, nm, nmType, aType, rssi, now, &isNew);
    idIdx = s_peers[idx].idIdx;
    memcpy(shown, s_peers[idx].name, sizeof(shown));
    portEXIT_CRITICAL(&s_bleMux);

    /* 白名单验算必须放在临界区外：AES 有点耗时，而且在 s_bleMux 里再去拿 s_idMux
     * 一旦有人反序调用就会死锁。只有“第一次见到这个地址”才会真的算一次。 */
    if (idIdx == -2) {
        int hit = ble_matchId(mac, addrVal, aType);
        portENTER_CRITICAL(&s_bleMux);
        if (s_peers[idx].used) s_peers[idx].idIdx = (int8_t)hit;
        portEXIT_CRITICAL(&s_bleMux);
        idIdx = (int8_t)hit;
        if (hit >= 0) {
            Serial.printf("[BLE] RPA resolved: %s -> %s\n", mac, BLE_getIdName(hit));
        }
    }

    portENTER_CRITICAL(&s_bleMux);
    BlePeer &pr = s_peers[idx];

    // ★ 名字按 MAC 记忆后再生效匹配：某些包不带名字（scan rsp 才有），
    //   只看“本包有没有名字”会漏掉绝大多数包。
    const int level = ble_lockLevel();
    bool hit = false;
    int kind = 0;
    if (level == 3) {
        hit = ble_macEquals(pr.mac, s_manualMac);
        kind = 3;
    } else if (level == 2) {
        hit = ble_macEquals(pr.mac, BLE_TARGET_MAC);
        kind = 2;
    } else if (level == 1) {
        hit = ble_nameInList(pr.name, BLE_TARGET_NAME);
        kind = 1;
    }

    if (hit) {
        s_rawRSSI = rssi;
        s_rawSeen = now;
        memcpy(s_rawName, pr.name, sizeof(s_rawName));
        memcpy(s_rawMac, pr.mac, sizeof(s_rawMac));
        s_rawAddrType = pr.addrType;
        s_rawKind = kind;
    }
    s_pktCount++;
    s_lastPktMs = now;
    portEXIT_CRITICAL(&s_bleMux);

    // 新设备只在串口提示一行，方便你对照“到底哪台才是 haha”
    if (isNew) {
        Serial.printf("[BLE] new dev name=%s mac=%s addr=%s id=%s\n",
                      shown[0] ? shown : "(no name)", mac, ble_addrTypeTag(aType),
                      (idIdx >= 0) ? BLE_getIdName(idIdx) : "-");
    }
}

/* =====================================================================
 * 扫描引擎
 *  NimBLE：ble_gap_disc 直接收原始包，零堆分配。
 *  Bluedroid（老核心）：退回 BLEScan，wantDuplicates=true。
 * ===================================================================== */
#if defined(CONFIG_NIMBLE_ENABLED)

static int ble_gapEventCb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            ble_handleReport(event->disc.addr.val, event->disc.addr.type, event->disc.rssi,
                             event->disc.data, event->disc.length_data);
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            s_scanning = false;  // 到点了，由 BLE_update() 续扫
            return 0;

        default:
            return 0;
    }
}

static bool ble_startScan() {
    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.itvl = (uint16_t)(BLE_SCAN_INTERVAL_MS * 8 / 5);  // ms -> 0.625ms 单位
    p.window = (uint16_t)(BLE_SCAN_WINDOW_MS * 8 / 5);
    p.filter_policy = 0;
    p.limited = 0;
    p.passive = BLE_ACTIVE_SCAN ? 0 : 1;
    p.filter_duplicates = 0;  // ★ 0 = 不过滤重复，RSSI 才会持续刷新

    uint8_t ownType = 0;  // BLE_OWN_ADDR_PUBLIC
    if (ble_hs_id_infer_auto(0, &ownType) != 0) ownType = 0;

    int rc = ble_gap_disc(ownType, BLE_SCAN_SESSION_MS, &p, ble_gapEventCb, nullptr);
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_scanning = true;
        return true;
    }
    Serial.printf("[BLE] ble_gap_disc rc=%d\n", rc);
    return false;
}

static void ble_enginePoll(unsigned long now) {
    if (!s_scanning) {
        if (now - s_lastStartTry >= 1000) {  // 1 秒最多重试一次
            s_lastStartTry = now;
            ble_startScan();
        }
        return;
    }
    // 15 秒一个包都没有：控制器可能被射频争用搞停了，强制重来
    unsigned long lastPkt = s_lastPktMs;
    if (lastPkt != 0 && (now - lastPkt) > 15000) {
        if (now - s_lastStartTry >= 2000) {
            s_lastStartTry = now;
            ble_gap_disc_cancel();
            s_scanning = false;
        }
    }
}

#else  // ---------------- Bluedroid 主机 ----------------

static BLEScan *pScan = nullptr;

class ProximityCallbacks : public BLEAdvertisedDeviceCallbacks {
  public:
    void onResult(BLEAdvertisedDevice dev) override {
        uint8_t val[6];
        memcpy(val, dev.getAddress().getNative(), 6);
        const uint8_t at = (uint8_t)dev.getAddress().getType();
        const int rssi = dev.getRSSI();
        const uint8_t *payload = dev.getPayload();
        const uint8_t plen = (uint8_t)dev.getPayloadLength();
        ble_handleReport(val, at, rssi, payload, plen);
    }
};

static bool ble_startScan() {
    if (!pScan) return false;
    if (pScan->isScanning()) {
        s_scanning = true;
        return true;
    }
    bool ok = pScan->start(0, nullptr, true);  // ★ 非阻塞重载
    s_scanning = ok;
    return ok;
}

static void ble_enginePoll(unsigned long now) {
    if (!s_scanning && (now - s_lastStartTry >= 1000)) {
        s_lastStartTry = now;
        ble_startScan();
    }
}

#endif

/* 抓取 IRK 时把扫描停掉：ESP32-C3 只有一个 2.4G 射频，扫描的 20% 占空比
 * 会和配对的链路建立抢时隙，容易导致配对超时。 */
static void BLE_pauseScanForCapture() {
    s_scanPaused = true;
#if defined(CONFIG_NIMBLE_ENABLED)
    ble_gap_disc_cancel();
#endif
    s_scanning = false;
    s_lastStartTry = millis();
}

static void BLE_resumeScanAfterCapture() {
    if (!s_scanPaused) return;
    s_scanPaused = false;
    s_scanning = false;
    s_lastPktMs = millis();     // 抓取期间没包，别让 15 秒看门狗立刻误判
    s_lastStartTry = millis();  // 让引擎等 1 秒再重启，避免取消扫描的异步余波
}

// ============ 锁定目标的持久化 ============
static void ble_loadLock() {
    if (s_manualLoaded) return;
    s_manualLoaded = true;
    Preferences prefs;
    prefs.begin("blescan", true);
    String m = prefs.getString("lock", "");
    prefs.end();
    if (m.length() == 17) {
        for (int i = 0; i < 17; i++) s_manualMac[i] = (char)tolower((unsigned char)m[i]);
        s_manualMac[17] = 0;
        s_manualValid = true;
        s_lockSetAt = millis();
        Serial.printf("[BLE] restored lock mac=%s\n", s_manualMac);
    }
}

// mac 传 "" 或不足 17 位 = 解除锁定
void BLE_setLock(const char *mac) {
    Preferences prefs;
    if (!mac || strlen(mac) < 17) {
        s_manualValid = false;
        s_manualMac[0] = 0;
        s_lockedValid = false;
        s_reacqAt = 0;
        s_lastGoodRSSI = BLE_RSSI_NONE;
        prefs.begin("blescan", false);
        prefs.remove("lock");
        prefs.end();
        Serial.println("[BLE] lock cleared");
        return;
    }
    for (int i = 0; i < 17; i++) s_manualMac[i] = (char)tolower((unsigned char)mac[i]);
    s_manualMac[17] = 0;
    s_manualValid = true;
    s_lockedValid = false;
    s_reacqAt = 0;
    s_lockSetAt = millis();
    s_lastGoodRSSI = BLE_RSSI_NONE;
    prefs.begin("blescan", false);
    prefs.putString("lock", s_manualMac);
    prefs.end();
    Serial.printf("[BLE] lock set mac=%s\n", s_manualMac);
}

// ============ 初始化 ============
void BLE_initProximity() {
    ble_loadLock();
    ble_loadIds();

    BLEDevice::init("ESP32-C3-Proximity");

#if defined(CONFIG_NIMBLE_ENABLED)
    bool ok = ble_startScan();
    Serial.printf("[BLE] gap scan start=%s (active=%d, interval=%ums, window=%ums)\n",
                  ok ? "OK" : "FAILED", (int)BLE_ACTIVE_SCAN,
                  BLE_SCAN_INTERVAL_MS, BLE_SCAN_WINDOW_MS);
#else
    pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new ProximityCallbacks(), true);
    pScan->setActiveScan(BLE_ACTIVE_SCAN);
    pScan->setInterval(BLE_SCAN_INTERVAL_MS);
    pScan->setWindow(BLE_SCAN_WINDOW_MS);
    bool ok = ble_startScan();
    Serial.printf("[BLE] BLEScan start=%s (active=%d, interval=%ums, window=%ums)\n",
                  ok ? "OK" : "FAILED", (int)BLE_ACTIVE_SCAN,
                  BLE_SCAN_INTERVAL_MS, BLE_SCAN_WINDOW_MS);
#endif

    s_lastUpdateMs = millis() - 1000;
}

// ============ 周期更新（主循环调用，自带节流）============
void BLE_update() {
    const unsigned long now = millis();
    if (now - s_lastUpdateMs < 150) return;
    s_lastUpdateMs = now;

    if (!s_scanPaused) ble_enginePoll(now);

    // ---- 1. 取快照 ----
    int rawRSSI, kind;
    unsigned long rawSeen;
    char rawName[32], rawMac[18];
    uint8_t rawType = 0;
    uint32_t pkts;

    int bestRSSI = BLE_RSSI_NONE;
    unsigned long bestSeen = 0;
    uint8_t bestType = 0;
    char bestName[32] = {0}, bestMac[18] = {0};

    int lockRSSI = BLE_RSSI_NONE;
    unsigned long lockSeen = 0;
    char lockName[32] = {0}, lockMac[18] = {0};

    // 白名单命中的设备（可能有多个同时在附近，取信号最强的那个）
    int idRSSI = BLE_RSSI_NONE;
    unsigned long idSeen = 0;
    uint8_t idType = 0;
    int8_t idHitIdx = -1;
    char idMac[18] = {0};
    char idRawName[32] = {0};

    char lockedMac[18];
    const bool lockedValid = s_lockedValid;
    memcpy(lockedMac, s_lockedMac, sizeof(lockedMac));

    portENTER_CRITICAL(&s_bleMux);
    rawRSSI = s_rawRSSI;
    rawSeen = s_rawSeen;
    kind = s_rawKind;
    rawType = s_rawAddrType;
    memcpy(rawName, s_rawName, sizeof(rawName));
    memcpy(rawMac, s_rawMac, sizeof(rawMac));
    pkts = s_pktCount;

    for (int i = 0; i < BLE_MAX_SLOTS; i++) {
        if (!s_peers[i].used) continue;
        if ((unsigned long)(now - s_peers[i].lastSeen) > BLE_STALE_MS) continue;
        if (s_peers[i].rssi > bestRSSI) {
            bestRSSI = s_peers[i].rssi;
            bestSeen = s_peers[i].lastSeen;
            bestType = s_peers[i].addrType;
            memcpy(bestName, s_peers[i].name, sizeof(bestName));
            memcpy(bestMac, s_peers[i].mac, sizeof(bestMac));
        }
        if (s_peers[i].idIdx >= 0 && s_peers[i].rssi > idRSSI) {
            idRSSI = s_peers[i].rssi;
            idSeen = s_peers[i].lastSeen;
            idType = s_peers[i].addrType;
            idHitIdx = s_peers[i].idIdx;
            memcpy(idMac, s_peers[i].mac, sizeof(idMac));
            memcpy(idRawName, s_peers[i].name, sizeof(idRawName));
        }
        if (lockedValid && strncmp(s_peers[i].mac, lockedMac, 17) == 0) {
            lockRSSI = s_peers[i].rssi;
            lockSeen = s_peers[i].lastSeen;
            memcpy(lockName, s_peers[i].name, sizeof(lockName));
            memcpy(lockMac, s_peers[i].mac, sizeof(lockMac));
        }
    }
    portEXIT_CRITICAL(&s_bleMux);

    // ---- 2. 选目标设备 ----
    const int level = ble_lockLevel();
    const bool strict = (level >= 2);  // 手动锁定 / 配置 MAC：目标不在就报 -100

    int targetRSSI = BLE_RSSI_NONE;
    unsigned long targetSeen = 0;
    uint8_t targetType = 0;
    char tName[32] = {0}, tMac[18] = {0};
    bool matched = false;
    bool whitelisted = false;

    if (idHitIdx >= 0 && idRSSI != BLE_RSSI_NONE) {
        /* 白名单命中优先级最高：这是“按身份认人”，与地址是否轮换无关。
         * 名字用白名单里配置的名字，这样网页上直接显示“haha”而不是一串 MAC。 */
        targetRSSI = idRSSI;
        targetSeen = idSeen;
        targetType = idType;
        memcpy(tMac, idMac, sizeof(tMac));
        snprintf(tName, sizeof(tName), "%s", BLE_getIdName(idHitIdx));
        matched = true;
        whitelisted = true;
        s_lockedValid = false;
    } else if (kind != 0 && rawSeen != 0 && (now - rawSeen) <= BLE_STALE_MS) {
        targetRSSI = rawRSSI;
        targetSeen = rawSeen;
        targetType = rawType;
        memcpy(tName, rawName, sizeof(tName));
        memcpy(tMac, rawMac, sizeof(tMac));
        matched = true;
        s_lockedValid = false;
    } else if (!strict && BLE_FALLBACK_STRONGEST && bestRSSI != BLE_RSSI_NONE) {
        // 兜底：跟踪最强设备，带 dB 差去抖，避免两台设备来回抢
        bool stick = lockedValid && lockRSSI != BLE_RSSI_NONE &&
                     (lockRSSI >= (bestRSSI - BLE_STICKY_DB));
        if (stick) {
            targetRSSI = lockRSSI;
            targetSeen = lockSeen;
            memcpy(tName, lockName, sizeof(tName));
            memcpy(tMac, lockMac, sizeof(tMac));
        } else {
            targetRSSI = bestRSSI;
            targetSeen = bestSeen;
            targetType = bestType;
            memcpy(tName, bestName, sizeof(tName));
            memcpy(tMac, bestMac, sizeof(tMac));
            memcpy(s_lockedMac, bestMac, sizeof(s_lockedMac));
            s_lockedValid = true;
        }
    }

    /* ---- 2.5 目标换了随机地址：自动接管 ----
     * 手机（iPhone/Android）的 BLE 地址每隔十几分钟就会轮换一次，锁死旧 MAC 必然失联。
     * 这里只在“已经锁定 MAC”且“目标失联”时，找一个刚出现、信号和上次接近的随机地址设备；
     * 必须恰好只有 1 台才接管，避免误抓别人的手机。 */
    if (targetRSSI == BLE_RSSI_NONE && strict && BLE_AUTO_REACQUIRE && s_lastGoodRSSI != BLE_RSSI_NONE) {
        char cand[18] = {0};
        int candCount = 0;

        portENTER_CRITICAL(&s_bleMux);
        for (int i = 0; i < BLE_MAX_SLOTS; i++) {
            BlePeer &p = s_peers[i];
            if (!p.used) continue;
            if ((unsigned long)(now - p.lastSeen) > BLE_STALE_MS) continue;
            if ((unsigned long)(now - p.firstSeen) > BLE_REACQUIRE_WINDOW_MS) continue;
            if (p.addrType < 2) continue;  // 只会变地址的设备才需要接管
            int d = p.rssi - s_lastGoodRSSI;
            if (d < 0) d = -d;
            if (d > BLE_REACQUIRE_DB) continue;
            candCount++;
            if (candCount == 1) memcpy(cand, p.mac, sizeof(cand));
        }
        portEXIT_CRITICAL(&s_bleMux);

        if (candCount == 1) {
            portENTER_CRITICAL(&s_bleMux);
            memcpy(s_manualMac, cand, sizeof(s_manualMac));  // 只改内存，不写 NVS
            s_manualValid = true;
            s_lockedValid = false;
            portEXIT_CRITICAL(&s_bleMux);
            s_reacqAt = millis();
            Serial.printf("[BLE] target addr rotated -> adopt %s\n", cand);
        }
    }

    /* 锁了个“死地址”的自愈：锁定后 60 秒一次都没收到过这个 MAC
     * （典型场景：手机换了随机地址，NVS 里存的旧地址永远不会再出现），
     * 就自动解除锁定，退回到按名字 / 跟最强，免得一直卡在 -100。
     * 只改内存，不动 NVS，重启后还会再试一次。 */
    if (s_manualValid && s_lastGoodRSSI == BLE_RSSI_NONE && s_lockSetAt != 0 &&
        (now - s_lockSetAt) > 60000) {
        s_manualValid = false;
        s_manualMac[0] = 0;
        s_lockSetAt = 0;
        s_reacqAt = 0;
        Serial.println("[BLE] locked mac never seen in 60s, lock released");
    }

    /* 换了跟踪对象就把平滑值清零，否则会从上一台的 -39 慢慢“滑”到 -56。
     * 身份键优先用名字：地址会变的设备（RPA/NRPA）换了 MAC 但名字不变时，
     * 曲线应该连续，而不是断一下重新开始。 */
    if (targetRSSI != BLE_RSSI_NONE) {
        char key[40];
        if (tName[0]) snprintf(key, sizeof(key), "n:%s", tName);
        else snprintf(key, sizeof(key), "m:%s", tMac);
        if (strncmp(key, s_emaKey, sizeof(key)) != 0) {
            s_emaValid = false;
            memcpy(s_emaKey, key, sizeof(s_emaKey));
        }
    }

    // ---- 3. 平滑 + 输出 ----
    s_outMatched = matched;
    s_outWhitelisted = whitelisted;
    s_outPackets = pkts;

    if (targetRSSI != BLE_RSSI_NONE) {
        if (!s_emaValid) {
            s_ema = (float)targetRSSI;
            s_emaValid = true;
        } else {
            s_ema += BLE_EMA_ALPHA * ((float)targetRSSI - s_ema);
        }
        s_outRaw = targetRSSI;
        s_lastGoodRSSI = targetRSSI;
        s_outRSSI = (int)lroundf(s_ema);
        s_outAgeMs = (long)(now - targetSeen);
        s_outAddrType = targetType;
        memcpy(s_outName, tName, sizeof(s_outName));
        memcpy(s_outMac, tMac, sizeof(s_outMac));
    } else {
        s_emaValid = false;
        s_outRaw = BLE_RSSI_NONE;
        s_outRSSI = BLE_RSSI_NONE;
        s_outAgeMs = -1;
        s_outAddrType = 0;
        s_outName[0] = 0;
        s_outMac[0] = 0;
        s_lockedValid = false;
    }

    // ---- 4. 迟滞判断 ----
    if (s_outRSSI == BLE_RSSI_NONE) {
        s_outNear = false;
    } else if (!s_outNear && s_outRSSI >= BLE_RSSI_NEAR_THRESHOLD) {
        s_outNear = true;
    } else if (s_outNear && s_outRSSI < BLE_RSSI_FAR_THRESHOLD) {
        s_outNear = false;
    }
}

// ============ 对外只读接口 ============
int BLE_getAverageRSSI() { return s_outRSSI; }
int BLE_getRawRSSI() { return s_outRaw; }
bool BLE_isDevsProximity() { return s_outNear; }
bool BLE_isTargetMatched() { return s_outMatched; }
bool BLE_isWhitelisted() { return s_outWhitelisted; }
const char *BLE_getTargetName() { return s_outName; }
const char *BLE_getTargetMac() { return s_outMac; }
uint8_t BLE_getTargetAddrType() { return s_outAddrType; }
long BLE_getLastSeenAgeMs() { return s_outAgeMs; }
uint32_t BLE_getPacketCount() { return s_outPackets; }
bool BLE_isScanning() { return s_scanning; }

const char *BLE_getLockMode() {
    switch (ble_lockLevel()) {
        case 3: return "manual";
        case 2: return "cfg-mac";
        case 1: return "cfg-name";
        default: return "auto";
    }
}

// 当前锁定的 MAC（网页用来高亮）：手动 > 配置 MAC > 空
const char *BLE_getLockedMac() {
    if (s_manualValid) return s_manualMac;
    if (BLE_TARGET_MAC[0]) return BLE_TARGET_MAC;
    return "";
}

const char *BLE_getTargetKeyword() { return BLE_TARGET_NAME; }

bool BLE_wasReacquired() { return s_reacqAt != 0 && (millis() - s_reacqAt) < 60000; }

// 拷贝一份设备表（供 /peers 用），按信号强度降序
int BLE_copyPeers(BlePeer *out, int maxOut) {
    int n = 0;
    portENTER_CRITICAL(&s_bleMux);
    for (int i = 0; i < BLE_MAX_SLOTS && n < maxOut; i++) {
        if (!s_peers[i].used) continue;
        out[n++] = s_peers[i];
    }
    portEXIT_CRITICAL(&s_bleMux);

    for (int i = 1; i < n; i++) {
        BlePeer t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return n;
}

/* 抓取模式（ESP32 当可配对外设，让手机把 IRK 发过来）依赖上面所有实现，
 * 所以放在文件最末尾引入。 */
#include "ble_irk_capture.h"

#endif
