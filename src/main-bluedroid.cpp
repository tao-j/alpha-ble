/*
 * Connect to the first Sony camera found and take a picture
 */
#include <Arduino.h>

#include "BLEDevice.h"
static const char* LOG_TAG = "EEE";
static BLEUUID serviceUUID("8000ff00-ff00-ffff-ffff-ffffffffffff");
static BLEUUID commandUUID("0000ff01-0000-1000-8000-00805f9b34fb");
static BLEUUID statusUUID("0000ff02-0000-1000-8000-00805f9b34fb");

static bool doConnect = false;
static bool connected = false;
static BLEAddress* pServerAddress;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* pRemoteCharacteristicNotify;

class MySecurity : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() { return 123456; }
    void onPassKeyNotify(uint32_t pass_key) {
        ESP_LOGE(LOG_TAG, "The passkey Notify number:%d", pass_key);
    }
    bool onConfirmPIN(uint32_t pass_key) {
        ESP_LOGI(LOG_TAG, "The passkey YES/NO number:%d", pass_key);
        vTaskDelay(5000);
        return true;
    }
    bool onSecurityRequest() {
        ESP_LOGI(LOG_TAG, "Security Request");
        return true;
    }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) {
        if (auth_cmpl.success) {
            ESP_LOGI(LOG_TAG, "remote BD_ADDR:");
            esp_log_buffer_hex(LOG_TAG, auth_cmpl.bd_addr,
                               sizeof(auth_cmpl.bd_addr));
            ESP_LOGI(LOG_TAG, "address type = %d", auth_cmpl.addr_type);
        }
        ESP_LOGI(LOG_TAG, "pair status = %s",
                 auth_cmpl.success ? "success" : "fail");
    }
};

//TODO: onDisconnect

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("Notify callback for characteristic ");
    Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" of data length ");
    Serial.println(length);
}

int connectToServer(BLEAddress pAddress) {
    Serial.print("Forming a connection to ");
    Serial.println(pAddress.toString().c_str());

    BLEClient* pClient = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->connect(pAddress);
    Serial.println(" - Connected to server");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.print("Failed to find our service UUID: ");
        Serial.println(serviceUUID.toString().c_str());
        return 1;
    }
    Serial.println(" - Found our service");

    pRemoteCharacteristic = pRemoteService->getCharacteristic(commandUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.print("Failed to find command UUID: ");
        Serial.println(commandUUID.toString().c_str());
        return 1;
    }
    Serial.println(" - Found our command interafce");

    pRemoteCharacteristicNotify = pRemoteService->getCharacteristic(statusUUID);
    if (pRemoteCharacteristicNotify == nullptr) {
        Serial.print("Failed to find status UUID: ");
        Serial.println(statusUUID.toString().c_str());
        return 1;
    }
    pRemoteCharacteristicNotify->registerForNotify(notifyCallback);
    Serial.println(" - subscribe to status notification");

    return 0;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE Advertised Device found: ");
        Serial.println(advertisedDevice.toString().c_str());

        constexpr std::array<uint8_t, 4> CAMERA_MANUFACTURER_LOOKUP = {
            0x2D, 0x01, 0x03, 0x00};
        auto mfd = advertisedDevice.getManufacturerData();
        bool is_sony_cam =
            std::equal(CAMERA_MANUFACTURER_LOOKUP.begin(),
                       CAMERA_MANUFACTURER_LOOKUP.end(), mfd.begin());
        // can also add check for it is ready for pairing and display on screen
        if (is_sony_cam) {
            Serial.println("Found Sony Camera");
            advertisedDevice.getScan()->stop();

            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            doConnect = true;
        }
    }
};

void setup() {
    Serial.begin(500000);
    Serial.println("Starting Arduino BLE Client application...");

    BLEDevice::init("Alpha Fairy");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    // BLEDevice::setSecurityCallbacks(new MySecurity());

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(30);
}

void loop() {
    if (doConnect) {
        if (!connectToServer(*pServerAddress)) {
            Serial.println("We are now connected to the Camera.");
            connected = true;
        } else {
            Serial.println("We have failed to connect to the camera; exiting");
        }
        doConnect = false;
    }

    if (connected) {
        // uint8_t rec[2] = {0x01, 0x0f};
        // uint8_t ret[2] = {0x01, 0x0e};
        // pRemoteCharacteristic->writeValue(rec, 2, true);
        // pRemoteCharacteristic->writeValue(ret, 2, true);
        // Serial.print("recording started ");

        uint8_t shu[2] = {0x01, 0x06};  // Shutter Half Up
        uint8_t shd[2] = {0x01, 0x07};  // Shutter Half Down
        uint8_t sfu[2] = {0x01, 0x08};  // Shutter Fully Up
        uint8_t sfd[2] = {0x01, 0x09};  // Shutter Fully Down
        pRemoteCharacteristic->writeValue(shd, 2, true);
        pRemoteCharacteristic->writeValue(sfd, 2, true);
        pRemoteCharacteristic->writeValue(sfu, 2, true);
        pRemoteCharacteristic->writeValue(shu, 2, true);
        Serial.print("completed a full shutter travel");
    }

    delay(10000);
}