#ifndef __BLE_IRK_CAPTURE_H
#define __BLE_IRK_CAPTURE_H

/* =====================================================================
 * IRK 抓取模式（让手机配对一次，自动读走它下发的 IRK）
 * ---------------------------------------------------------------------
 * 为什么可行：
 *   LE 配对建立加密之后，双方会交换“密钥分发”包。只要本机在配对参数里声明
 *   需要对方的 Identity Key（IRK），手机就会把自己的 16 字节 IRK 直接发过来
 *   （明文，走的是加密链路）。NimBLE 收到后写进 security store，
 *   我们再从 store 里把它读出来 —— 全程不需要 root、不需要 ADB。
 *
 * 为什么安卓会给：
 *   安卓在地址隐私（RPA）开启时，一定会把 IRK 作为 Identity Key 分发给
 *   已经 bond 的对端，否则对端无法在下次连接时解析它的随机地址。
 *   所以本机必须请求 bonding（sm_bonding = 1），且要 ID key distribution。
 *
 * 不可行的情形：
 *   - 手机之前已经和本机配对过，且没有在手机端“忘记此设备”：
 *     手机会直接用旧 LTK 加密，不会重新分发 IRK。此时如果 store 里还留着
 *     上次的 IRK，直接复用；否则请在手机上取消配对后重试。
 *   - 个别机型（尤其国产 ROM）在 periphery 角色下不分发 IRK，只能退回 MAC 白名单。
 *
 * 附：ESP32-C3 只有一个 2.4G 射频，配对期间会暂停扫描（否则会互相抢占），
 *     抓完自动恢复扫描。
 * ===================================================================== */

#if defined(CONFIG_NIMBLE_ENABLED)

#include <stdarg.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/ble_store.h>

// ---- 状态机 ----
#define BLE_CAP_IDLE 0     // 未启用
#define BLE_CAP_ADV 1      // 广播中，等手机连
#define BLE_CAP_CONN 2     // 手机已连，正在发起配对
#define BLE_CAP_PAIRING 3  // 已加密，正在读 IRK
#define BLE_CAP_DONE 4     // 抓到了
#define BLE_CAP_FAIL 5     // 失败（超时 / 手机没给 IRK）
#define BLE_CAP_PREP 6     // 正在停扫描，等射频空出来（见下方 PREP 说明）

static const char *BLE_CAP_ADV_NAME = "C3-IRK-CAP";

static uint8_t s_capState = BLE_CAP_IDLE;
static char s_capLabel[20] = "haha";
static char s_capMsg[96] = "idle";
static char s_capIrkHex[33] = {0};
static char s_capPeerMac[18] = {0};
static char s_capPeerId[18] = {0};
static bool s_capGotIrk = false;
static int8_t s_capSlot = -1;
static uint16_t s_capConn = BLE_HS_CONN_HANDLE_NONE;
static unsigned long s_capConnAt = 0;    // 连上的时间（用来判超时）
static unsigned long s_capEncAt = 0;     // 加密完成的时间
static unsigned long s_capNextTry = 0;   // 下次重试安全请求 / 读 store
static unsigned long s_capPrepAt = 0;    // 进入 PREP 的时间
static uint8_t s_capTries = 0;
static bool s_capInited = false;
static BLEServer *s_capSrv = nullptr;
static BLECharacteristic *s_capChr = nullptr;

static void ble_capSay(const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    memcpy(s_capMsg, buf, sizeof(s_capMsg));
    Serial.printf("[CAP] %s\n", buf);
}

static void ble_capHex(const uint8_t *in, char *out /* 33 */) {
    for (int i = 0; i < 16; i++) snprintf(&out[i * 2], 3, "%02x", in[i]);
    out[32] = 0;
}

/* ★★ 一个必须绕开的坑 ★★
 * NimBLE 在「扫描 / 广播 / 已连接」任一进行中时，ble_gatts_mutable() 返回 NULL，
 * 于是 ble_gatts_add_svcs() 返回 BLE_HS_EBUSY(15)。
 * 而 Arduino 的 BLEDevice::createServer() 里写的是：
 *     ble_gatts_reset(); ble_svc_gap_init(); ble_svc_gatt_init();
 * 其中 ble_svc_gap_init() 对返回值是 assert(rc == 0) —— 直接 abort() 复位！
 * 我们平时一直在扫描，所以「点一下开始抓取就重启」就是这个原因。
 * 结论：建服务之前，必须先确认射频空闲（扫描停稳），否则一定崩。 */
static bool ble_capRadioBusy() {
    return ble_gap_adv_active() || ble_gap_disc_active() || ble_gap_conn_active();
}

// 已经在白名单里的同一条 IRK 就复用，避免重复占位
// 返回：>=0 白名单下标；-2 白名单已满
static int8_t ble_capSaveIrk(const uint8_t *irk) {
    BleId cur[BLE_MAX_IDS];
    const int n = BLE_copyIds(cur, BLE_MAX_IDS);
    for (int i = 0; i < n; i++) {
        if (!cur[i].hasIrk) continue;
        if (memcmp(cur[i].irk, irk, 16) == 0) return (int8_t)i;
    }

    char hex[33];
    ble_capHex(irk, hex);
    const int slot = BLE_addId(s_capLabel, nullptr, hex);
    return (slot >= 0) ? (int8_t)slot : (int8_t)-2;
}

/* 从 NimBLE 的 security store 里找 IRK。
 * 只遍历 PEER_SEC；手机分发 IRK 时 NimBLE 就是用这个类型落库的。
 * 优先取“地址能对上本次连接”的那条，其次取第一条带 IRK 的（干净设备只有一条）。 */
struct BleCapCtx {
    ble_addr_t idAddr;
    ble_addr_t otaAddr;
    uint8_t irk[16];
    bool addrHit;
    bool anyHit;
};

static int ble_capStoreCb(int obj_type, union ble_store_value *val, void *cookie) {
    BleCapCtx *c = (BleCapCtx *)cookie;
    if (obj_type != BLE_STORE_OBJ_TYPE_PEER_SEC || val == nullptr) return 0;

    const struct ble_store_value_sec *sec = &val->sec;
    if (!sec->irk_present) return 0;

    bool hit = (sec->peer_addr.type == c->idAddr.type &&
                memcmp(sec->peer_addr.val, c->idAddr.val, 6) == 0);
    if (!hit && sec->peer_addr.type == c->otaAddr.type &&
        memcmp(sec->peer_addr.val, c->otaAddr.val, 6) == 0) {
        hit = true;
    }

    if (hit) {
        memcpy(c->irk, sec->irk, 16);
        c->addrHit = true;
        c->anyHit = true;
    } else if (!c->anyHit) {
        memcpy(c->irk, sec->irk, 16);
        c->anyHit = true;
    }
    return 0;  // 0 = 继续遍历
}

/* 抓取开始前先记下 store 里已有的 IRK（之前抓过的别的手机），
 * 这样本次连接如果什么都没发，就不会被旧记录误判成“抓到了”。 */
static const int BLE_CAP_PRE_MAX = 4;
static uint8_t s_capPreIrk[4][16];
static uint8_t s_capPreCount = 0;

static int ble_capPreCb(int obj_type, union ble_store_value *val, void *cookie) {
    (void)cookie;
    if (obj_type != BLE_STORE_OBJ_TYPE_PEER_SEC || val == nullptr) return 0;
    if (!val->sec.irk_present) return 0;
    if (s_capPreCount < BLE_CAP_PRE_MAX) {
        memcpy(s_capPreIrk[s_capPreCount], val->sec.irk, 16);
        s_capPreCount++;
    }
    return 0;
}

static void ble_capSnapshotIrks() {
    s_capPreCount = 0;
    ble_store_iterate(BLE_STORE_OBJ_TYPE_PEER_SEC, ble_capPreCb, nullptr);
    Serial.printf("[CAP] store already has %u irk(s)\n", (unsigned)s_capPreCount);
}

static bool ble_capIrkKnown(const uint8_t *irk) {
    for (int i = 0; i < (int)s_capPreCount; i++) {
        if (memcmp(s_capPreIrk[i], irk, 16) == 0) return true;
    }
    return false;
}

static bool ble_capReadIrk(uint16_t conn, uint8_t *out) {
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0) return false;

    BleCapCtx c;
    memset(&c, 0, sizeof(c));
    c.idAddr = desc.peer_id_addr;
    c.otaAddr = desc.peer_ota_addr;

    int rc = ble_store_iterate(BLE_STORE_OBJ_TYPE_PEER_SEC, ble_capStoreCb, &c);
    if (rc != 0 && rc != BLE_HS_ENOENT) {
        Serial.printf("[CAP] store iterate rc=%d\n", rc);
    }
    if (!c.anyHit) return false;

    /* 地址对不上、拿到的又只是“抓取开始前就已经存在”的那条 IRK，
     * 说明这是别的手机留下的（本次连接没发新的），不能算成功。 */
    if (!c.addrHit && ble_capIrkKnown(c.irk)) return false;

    memcpy(out, c.irk, 16);
    return true;
}

// 收尾：断开、停广播、恢复扫描
static void ble_capTeardown() {
    if (s_capConn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_capConn, BLE_ERR_REM_USER_CONN_TERM);
        s_capConn = BLE_HS_CONN_HANDLE_NONE;
    }
    if (s_capInited) BLEDevice::getAdvertising()->stop();
    ble_hs_cfg.sm_bonding = 0;  // 恢复默认：平时不做绑定
    BLE_resumeScanAfterCapture();
}

static void ble_capFail(const char *why) {
    s_capState = BLE_CAP_FAIL;
    ble_capSay("%s", why);
    ble_capTeardown();
}

static void ble_capSuccess(const uint8_t *irk) {
    ble_capHex(irk, s_capIrkHex);
    const int8_t slot = ble_capSaveIrk(irk);
    s_capGotIrk = true;
    s_capSlot = (slot >= 0) ? slot : -1;

    s_capState = BLE_CAP_DONE;
    if (slot >= 0) {
        ble_capSay("captured -> whitelist[%d] \"%s\": %s", (int)slot, s_capLabel, s_capIrkHex);
    } else {
        ble_capSay("captured %s but whitelist is full, delete one first", s_capIrkHex);
    }
    ble_capTeardown();
}

// 建 GATT server / 广播数据（只建一次，后面只 start/stop）
// 定义放在 ServerCallbacks 之后：必须在 createServer() 里立刻把回调挂上，
// 否则手机连上来时 onConnect 永远不会被调用（状态会一直卡在 adv）。
static bool ble_capSetup();

class BleCapServerCallbacks : public BLEServerCallbacks {
  public:
    void onConnect(BLEServer *srv) override { (void)srv; }

    void onConnect(BLEServer *srv, ble_gap_conn_desc *desc) override {
        (void)srv;
        if (desc == nullptr) return;
        // 只在自己发起的抓取会话里接受连接（扫描时的偶然连接不理会）
        if (s_capState != BLE_CAP_ADV && s_capState != BLE_CAP_CONN) return;

        s_capConn = desc->conn_handle;
        s_capConnAt = millis();
        s_capNextTry = 0;
        s_capTries = 0;
        ble_addrToStr(desc->peer_ota_addr.val, s_capPeerMac);
        ble_addrToStr(desc->peer_id_addr.val, s_capPeerId);

        s_capState = BLE_CAP_CONN;
        ble_capSay("phone connected %s (%s), waiting for pairing", s_capPeerMac,
                   desc->peer_ota_addr.type == 0 ? "public" : "random");
    }

    void onDisconnect(BLEServer *srv, ble_gap_conn_desc *desc) override {
        (void)srv;
        (void)desc;
        if (s_capConn != BLE_HS_CONN_HANDLE_NONE) s_capConn = BLE_HS_CONN_HANDLE_NONE;
        // 抓完 / 失败之后是 DONE / FAIL，不再回到广播状态
        if (s_capState == BLE_CAP_CONN || s_capState == BLE_CAP_PAIRING) {
            s_capState = BLE_CAP_ADV;
            ble_capSay("disconnected, advertising again");
        }
    }
};

static BleCapServerCallbacks s_capCallbacks;

static bool ble_capSetup() {
    if (s_capInited) return true;

    // 手机在蓝牙列表里看到的是广播包里的名字（C3-IRK-CAP），
    // GAP 服务里的名字沿用 BLEDevice::init() 传的那个，不用管。
    s_capSrv = BLEDevice::createServer();
    if (s_capSrv == nullptr) return false;

    // ★ 必须挂回调，否则连上来也不会触发 onConnect / onDisconnect
    s_capSrv->setCallbacks(&s_capCallbacks);
    s_capSrv->advertiseOnDisconnect(false);  // 断连后由本模块自己决定要不要重播

    BLEService *svc = s_capSrv->createService(BLEUUID((uint16_t)0xFFF0));
    if (svc == nullptr) return false;

    // 访问权限要求加密：万一手机端先读写特征（nRF Connect 之类），会被协议栈
    // 挡回去并要求加密，等于多一条触发配对的路径。
    s_capChr = svc->createCharacteristic(
        BLEUUID((uint16_t)0xFFF1),
        BLECharacteristic::PROPERTY_READ_ENC | BLECharacteristic::PROPERTY_WRITE_ENC);
    if (s_capChr != nullptr) s_capChr->setValue("IRK-CAPTURE");
    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->reset();
    adv->setName(String(BLE_CAP_ADV_NAME));
    adv->addServiceUUID(BLEUUID((uint16_t)0xFFF0));
    adv->setScanResponse(true);
    adv->setMinInterval(48);  // 30ms，连得快一点
    adv->setMaxInterval(80);

    s_capInited = true;
    Serial.println("[CAP] gatt ready, svc=0xFFF0 chr=0xFFF1");
    return true;
}

// ===== 对外接口 =====
bool BLE_isCapturing() {
    return s_capState == BLE_CAP_PREP || s_capState == BLE_CAP_ADV || s_capState == BLE_CAP_CONN ||
           s_capState == BLE_CAP_PAIRING;
}

void BLE_captureStop() {
    if (s_capState == BLE_CAP_DONE || s_capState == BLE_CAP_FAIL) {
        ble_capTeardown();
        return;
    }
    ble_capTeardown();
    s_capState = BLE_CAP_IDLE;
    ble_capSay("capture stopped");
}

bool BLE_captureStart(const char *label) {
    if (BLE_isCapturing()) return true;

    if (label && label[0]) {
        snprintf(s_capLabel, sizeof(s_capLabel), "%s", label);
        for (char *q = s_capLabel; *q; q++) {
            if (*q == '|' || *q == ';') *q = '_';
        }
    }

    // 复位上次结果（IRK 文本保留给网页看，直到下一次成功覆盖）
    s_capGotIrk = false;
    s_capSlot = -1;
    s_capConn = BLE_HS_CONN_HANDLE_NONE;
    s_capTries = 0;
    s_capEncAt = 0;
    s_capPeerMac[0] = 0;
    s_capPeerId[0] = 0;

    /* 配对参数（建服务之前设好，建完立刻可用）：
     *   bonding = 1  —— 必须。不 bond 手机不会把 IRK 当作长期身份密钥发过来。
     *   mitm    = 0  —— Just Works，手机上只需点“配对”，没有 6 位码的麻烦。
     *   sc      = 1  —— 优先 LE Secure Connections，安卓 5.0+ 都支持；
     *                   不支持时协议栈会自动退回 legacy。
     *   key_dist     —— 双方的 ENC + ID 都要，ID 就是 IRK/身份地址。 */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* 扫描和广播抢同一个射频，抓取期间先停扫描，抓完自动恢复。
     * ★ 这里只是「发起取消」，扫描要等 DISC_COMPLETE 事件回来才会真正停。
     *   建 GATT 服务必须等它停稳，所以先进入 PREP，由 BLE_capturePoll() 接着做。 */
    BLE_pauseScanForCapture();

    s_capPrepAt = millis();
    s_capNextTry = 0;
    s_capState = BLE_CAP_PREP;
    ble_capSay("stopping scan, preparing peripheral...");
    return true;
}

// 主循环调用（非阻塞）
void BLE_capturePoll() {
    if (s_capState == BLE_CAP_IDLE || s_capState == BLE_CAP_DONE || s_capState == BLE_CAP_FAIL) return;

    const unsigned long now = millis();

    if (s_capState == BLE_CAP_PREP) {
        /* 等扫描真正停下来。取消扫描是异步的（要等 DISC_COMPLETE 回来），
         * 这段时间里 ble_gatts_add_svcs() 会返回 EBUSY，而库里是 assert，直接崩。
         * 所以必须确认三个状态都为 0 才敢建服务。 */
        if (ble_capRadioBusy()) {
            if ((now - s_capPrepAt) > 6000) {
                ble_capFail("scan still active (radio busy), click Start again");
                return;
            }
            if ((long)(now - s_capNextTry) >= 0) {  // 每 300ms 再催一次
                s_capNextTry = now + 300;
#if defined(CONFIG_NIMBLE_ENABLED)
                ble_gap_disc_cancel();
#endif
            }
            return;
        }

        if (!ble_capSetup()) {
            ble_capFail("failed to init GATT service");  // fail() 会自动恢复扫描
            return;
        }

        ble_capSnapshotIrks();

        s_capState = BLE_CAP_ADV;
        ble_capSay("advertising: connect \"%s\" from the phone and tap Pair", BLE_CAP_ADV_NAME);
        if (!BLEDevice::getAdvertising()->start()) {
            Serial.println("[CAP] adv start failed, will retry");
        }
        return;
    }

    if (s_capState == BLE_CAP_ADV) {
        BLEAdvertising *adv = BLEDevice::getAdvertising();
        if (!adv->isAdvertising() && (now - s_capNextTry) > 1000) {
            s_capNextTry = now;
            adv->start();
        }
        return;
    }

    if (s_capState == BLE_CAP_CONN) {
        struct ble_gap_conn_desc desc;
        if (s_capConn == BLE_HS_CONN_HANDLE_NONE || ble_gap_conn_find(s_capConn, &desc) != 0) {
            s_capConn = BLE_HS_CONN_HANDLE_NONE;
            s_capState = BLE_CAP_ADV;
            return;
        }
        if (desc.sec_state.encrypted) {
            s_capState = BLE_CAP_PAIRING;
            s_capEncAt = now;
            s_capNextTry = 0;  // 别沿用“安全请求”的重试时刻
            s_capTries = 0;
            ble_capSay("paired (bonded=%d), reading IRK...", (int)desc.sec_state.bonded);
            return;
        }
        // 45 秒还没加密：多半是手机弹窗没点，或者两边配对参数不兼容
        if ((now - s_capConnAt) > 45000) {
            ble_capFail("pairing timeout: no Pair tap on phone, or Just Works rejected");
            return;
        }
        // 主动发 Security Request，手机才会弹配对框（每 4 秒重试一次）
        if ((long)(now - s_capNextTry) >= 0) {
            s_capNextTry = now + 4000;
            int rc = ble_gap_security_initiate(s_capConn);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                Serial.printf("[CAP] security_initiate rc=%d\n", rc);
            }
        }
        return;
    }

    if (s_capState == BLE_CAP_PAIRING) {
        // 密钥分发在加密完成后紧跟着走，等 700ms 再读，读不到就重试
        if ((now - s_capEncAt) < 700) return;
        if ((long)(now - s_capNextTry) < 0) return;
        s_capNextTry = now + 700;
        s_capTries++;

        uint8_t irk[16];
        if (ble_capReadIrk(s_capConn, irk)) {
            ble_capSuccess(irk);
            return;
        }
        if (s_capTries >= 12 || (now - s_capEncAt) > 9000) {
            ble_capFail("paired but no IRK received (unsupported ROM, or forget the device on phone and retry)");
        }
    }
}

// ---- 只读状态 ----
uint8_t BLE_captureState() { return s_capState; }
const char *BLE_captureMsg() { return s_capMsg; }
const char *BLE_captureIrkHex() { return s_capIrkHex; }
const char *BLE_capturePeerMac() { return s_capPeerMac; }
const char *BLE_capturePeerId() { return s_capPeerId; }
const char *BLE_captureLabel() { return s_capLabel; }
bool BLE_captureGotIrk() { return s_capGotIrk; }
int8_t BLE_captureSlot() { return s_capSlot; }

#else  // ---------------- 非 NimBLE（老核心用 Bluedroid）：只保留空实现 ----------------

bool BLE_isCapturing() { return false; }
void BLE_captureStop() {}
bool BLE_captureStart(const char *label) {
    (void)label;
    Serial.println("[CAP] 抓取模式需要 NimBLE 主机（当前核心用的是 Bluedroid）");
    return false;
}
void BLE_capturePoll() {}
uint8_t BLE_captureState() { return 0; }
const char *BLE_captureMsg() { return "当前核心不支持"; }
const char *BLE_captureIrkHex() { return ""; }
const char *BLE_capturePeerMac() { return ""; }
const char *BLE_capturePeerId() { return ""; }
const char *BLE_captureLabel() { return ""; }
bool BLE_captureGotIrk() { return false; }
int8_t BLE_captureSlot() { return -1; }

#endif

#endif  // __BLE_IRK_CAPTURE_H
