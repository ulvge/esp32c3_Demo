#include <Arduino.h>
#include <NimBLEDevice.h>

NimBLEServer *pServer = nullptr;

class MyServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        Serial.println();
        Serial.println("=== Device Connected ===");

        Serial.print("Address: ");
        Serial.println(connInfo.getAddress().toString().c_str());

        Serial.print("Address Type: ");
        Serial.println(connInfo.getAddress().getType());

        Serial.println("========================");
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override
    {
        Serial.println();
        Serial.println("=== Device Disconnected ===");
        Serial.print("Reason: ");
        Serial.println(reason);
        Serial.println("===========================");
    }

    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
    {
        Serial.println();
        Serial.println("=== Authentication Complete ===");

        Serial.print("Address: ");
        Serial.println(connInfo.getAddress().toString().c_str());

        if (connInfo.isBonded())
        {
            Serial.println(">>> BONDED DEVICE <<<");
        }
        else
        {
            Serial.println(">>> NOT BONDED <<<");
        }

        Serial.println("===============================");
    }
};

class MySecurityCallbacks : public NimBLECharacteristicCallbacks
{
};

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println(" ESP32-C3 BLE Bonding Test");
    Serial.println("================================");

    // 初始化 BLE
    NimBLEDevice::init("ESP32-C3-Bond-Test");

    // 安全参数
    NimBLEDevice::setSecurityAuth(
        true, // bonding
        true, // MITM
        true  // secure connection
    );

    // IO capability
    NimBLEDevice::setSecurityIOCap(
        BLE_HS_IO_DISPLAY_YESNO);

    // 建立 GATT Server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // 创建一个简单 Service
    NimBLEService *service =
        pServer->createService("12345678-1234-1234-1234-123456789ABC");

    NimBLECharacteristic *characteristic =
        service->createCharacteristic(
            "12345678-1234-1234-1234-123456789ABD",
            NIMBLE_PROPERTY::READ |
                NIMBLE_PROPERTY::WRITE);

    characteristic->setValue("Hello ESP32");

    service->start();

    // 开始广播
    NimBLEAdvertising *advertising =
        NimBLEDevice::getAdvertising();

    advertising->addServiceUUID(service->getUUID());
    advertising->setName("ESP32-C3-Bond-Test");

    advertising->start();

    Serial.println();
    Serial.println("BLE advertising started.");
    Serial.println("Device name: ESP32-C3-Bond-Test");
    Serial.println();
}

void loop()
{

    // 每隔一段时间打印已经 Bond 的设备数量
    static uint32_t lastPrint = 0;

    if (millis() - lastPrint > 5000)
    {
        lastPrint = millis();

        Serial.println();
        Serial.println("---- Bond Information ----");

        auto bonds = NimBLEDevice::getBondedAddresses();

        Serial.print("Bonded device count: ");
        Serial.println(bonds.size());

        for (size_t i = 0; i < bonds.size(); i++)
        {
            Serial.print("Bond #");
            Serial.print(i);
            Serial.print(": ");

            Serial.println(bonds[i].toString().c_str());
        }

        Serial.println("--------------------------");
    }

    delay(100);
}