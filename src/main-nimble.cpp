#include <Arduino.h>
#include <NimBLEDevice.h>
#define SERVICE_UUID "8000ff00-ff00-ffff-ffff-ffffffffffff"
#define CONTROL_UUID "ff01"
#define STATUS_UUID "ff02"


static NimBLEAdvertisedDevice* advDevice;

static bool doConnect = false;
static uint32_t scanTime = 30; /** 0 = scan forever */

/** Callback to process the results of the last scan or restart it */
void scanEndedCB(NimBLEScanResults results) { Serial.println("Scan Ended"); }
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("Connected -------------   on connection");
        /** After connection we should change the parameters if we don't need
         * fast response times. These settings are 150ms interval, 0 latency,
         * 450ms timout. Timeout should be a multiple of the interval, minimum
         * is 100ms. I find a multiple of 3-5 * the interval works best for
         * quick response/reconnect. Min interval: 120 * 1.25ms = 150, Max
         * interval: 120 * 1.25ms = 150, 0 latency, 60 * 10ms = 600ms timeout
         */
        // pClient->updateConnParams(120,120,0,60);
    };

    void onDisconnect(NimBLEClient* pClient) {
        Serial.print(pClient->getPeerAddress().toString().c_str());
        Serial.println(" Disconnected - not Starting scan");
        // NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    };

    /** Called when the peripheral requests a change to the connection
     * parameters. Return true to accept and apply them or false to reject and
     * keep the currently used parameters. Default will return true.
     */
    bool onConnParamsUpdateRequest(NimBLEClient* pClient,
                                   const ble_gap_upd_params* params) {
        if (params->itvl_min < 24) { /** 1.25ms units */
            return false;
        } else if (params->itvl_max > 40) { /** 1.25ms units */
            return false;
        } else if (params->latency >
                   2) { /** Number of intervals allowed to skip */
            return false;
        } else if (params->supervision_timeout > 100) { /** 10ms units */
            return false;
        }

        return true;
    };

    /********************* Security handled here **********************
    ****** Note: these are the same return values as defaults ********/
    uint32_t onPassKeyRequest() {
        Serial.println("Client Passkey Request");
        /** return the passkey to send to the server */
        return 123456;
    };

    bool onConfirmPIN(uint32_t pass_key) {
        Serial.print("The passkey YES/NO number: ");
        Serial.println(pass_key);
        /** Return false if passkeys don't match. */
        return true;
    };

    /** Pairing process complete, we can check the results in ble_gap_conn_desc
     */
    void onAuthenticationComplete(ble_gap_conn_desc* desc) {
        Serial.println("onAuthenticationComplete");
        if (!desc->sec_state.encrypted) {
            Serial.println("Encrypt connection failed - disconnecting");
            /** Find the client with the connection handle provided in desc */
            NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
            return;
        }
    };
};

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        // Serial.print("Advertised Device found: ");
        // Serial.println(advertisedDevice->toString().c_str());

        constexpr std::array<uint8_t, 4> CAMERA_MANUFACTURER_LOOKUP = {
            0x2D, 0x01, 0x03, 0x00};
        auto mfd = advertisedDevice->getManufacturerData();
        bool is_sony_cam =
            std::equal(CAMERA_MANUFACTURER_LOOKUP.begin(),
                       CAMERA_MANUFACTURER_LOOKUP.end(), mfd.begin());
        // can also add check for it is ready for pairing and display on screen
        if (is_sony_cam) {
            Serial.println("Found Sony Camera");
            NimBLEDevice::getScan()->stop();
            advDevice = advertisedDevice;
            doConnect = true;
        }
    };
};

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData,
              size_t length, bool isNotify) {
    std::string str = (isNotify == true) ? "Notification" : "Indication";
    str += " from ";
    /** NimBLEAddress and NimBLEUUID have std::string operators */
    str += std::string(pRemoteCharacteristic->getRemoteService()
                           ->getClient()
                           ->getPeerAddress());
    str += ": Service = " +
           std::string(pRemoteCharacteristic->getRemoteService()->getUUID());
    str +=
        ", Characteristic = " + std::string(pRemoteCharacteristic->getUUID());
    char stream[100];
    for (auto i = 0; i < length; i++) {
        sprintf(stream + 2 * i, "%02x", pData[i]);
    }
    str += ", Value = 0x" + std::string((char*)stream, 2 * length);
    Serial.println(str.c_str());
}

/** Create a single global instance of the callback class to be used by all
 * clients */
static ClientCallbacks clientCB;

/** Handles the provisioning of clients and connects / interfaces with the
 * server */
bool connectToServer() {
    NimBLEClient* pClient = nullptr;
    // ble_store_clear();
    /** Check if we have a client we should reuse first **/
    if (NimBLEDevice::getClientListSize()) {
        /** Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service
         * database. This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if (pClient) {
            if (!pClient->connect(advDevice, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        }
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
        else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    /** No client to reuse? Create a new one. */
    if (!pClient) {
        if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println(
                "Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();


        Serial.println("New client created");

        pClient->setClientCallbacks(&clientCB, false);
        /** Set initial connection parameters: These settings are 15ms interval,
         * 0 latency, 120ms timout. These settings are safe for 3 clients to
         * connect reliably, can go faster if you have less connections. Timeout
         * should be a multiple of the interval, minimum is 100ms. Min interval:
         * 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 *
         * 10ms = 510ms timeout
         */
        // pClient->setConnectionParams(12, 12, 0, 51);
        /** Set how long we are willing to wait for the connection to complete
         * (seconds), default is 30. */
        pClient->setConnectTimeout(5);

        if (!pClient->connect(advDevice)) {
            /** Created a client but failed to connect, don't need to keep it as
             * it has no data */
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected()) {
        if (!pClient->connect(advDevice)) {
            Serial.println("Failed to connect");
            return false;
        }
    }
    // pClient->secureConnection();
    // Serial.println(
    //     "===========-==-=========-=-=-=-=------------------================");
    // Serial.print("Connected to: ");
    // Serial.println(pClient->getPeerAddress().toString().c_str());
    // Serial.print("RSSI: ");
    // Serial.println(pClient->getRssi());

    /** Now we can read/write/subscribe the charateristics of the services we
     * are interested in */
    NimBLERemoteService* pSvc = nullptr;
    NimBLERemoteCharacteristic* pChr = nullptr;
    NimBLERemoteDescriptor* pDsc = nullptr;

    pSvc = pClient->getService(SERVICE_UUID);
    if (pSvc) { /** make sure it's not null */
        pChr = pSvc->getCharacteristic(STATUS_UUID);

        if (pChr) { /** make sure it's not null */
            /** registerForNotify() has been deprecated and replaced with
             * subscribe() / unsubscribe(). Subscribe parameter defaults are:
             * notifications=true, notifyCallback=nullptr, response=false.
             *  Unsubscribe parameter defaults are: response=false.
             */
            if (pChr->canNotify()) {
                // if(!pChr->registerForNotify(notifyCB)) {
                if (!pChr->subscribe(true, notifyCB)) {
                    /** Disconnect if subscribe failed */
                    pClient->disconnect();
                    return false;
                }
            } else if (pChr->canIndicate()) {
                /** Send false as first argument to subscribe to indications
                 * instead of notifications */
                // if(!pChr->registerForNotify(notifyCB, false)) {
                if (!pChr->subscribe(false, notifyCB)) {
                    /** Disconnect if subscribe failed */
                    pClient->disconnect();
                    return false;
                }
            }
        }

        char shu[2] = {0x01, 0x06};  // Shutter Half Up
        char shd[2] = {0x01, 0x07};  // Shutter Half Down
        char sfu[2] = {0x01, 0x08};  // Shutter Fully Up
        char sfd[2] = {0x01, 0x09};  // Shutter Fully Down
        pChr = pSvc->getCharacteristic(CONTROL_UUID);
        if (pChr->canWrite()) {
            pChr->writeValue(shd, true);
            delay(50);   
            pChr->writeValue(sfd, true);
            delay(50);
            pChr->writeValue(sfu, true);
            delay(50);
            pChr->writeValue(shu, true); // somehow it returns false but it works
            if (1) {
                Serial.print("Wrote new value to: ");
                Serial.println(pChr->getUUID().toString().c_str());
            } else {
                /** Disconnect if write failed */
                pClient->disconnect();
                return false;
            }
        }
        else {
            Serial.println("Control Characteristic cannot be written.");
        }

    } else {
        Serial.println("remote service not found.");
    }

    Serial.println("Done with this device!");
    return true;
}

void setup() {
    Serial.begin(500000);
    // delay(5000);
    Serial.println("Starting NimBLE Client");
    // setup advertisement name
    NimBLEDevice::init("NAlpha");
    NimBLEDevice::setSecurityAuth(
        BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM /* |
        BLE_SM_PAIR_AUTHREQ_SC*/);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    ble_hs_cfg.sm_our_key_dist = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    // ble_hs_cfg.sm_their_key_dist = 5;

//     /** Optional: set the transmit power, default is 3db */
// #ifdef ESP_PLATFORM
//     NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
// #else
//     NimBLEDevice::setPower(9); /** +9db */
// #endif
    // NimBLEDevice::setMTU(517);
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setInterval(45);
    pScan->setWindow(15);
    pScan->setActiveScan(true);
    pScan->start(scanTime, scanEndedCB);
}

void loop() {
    /** Loop here until we find a device we want to connect to */
    while (!doConnect) {
        delay(10);
    }

    doConnect = false;

    /** Found a device we want to connect to, do it now */
    if (connectToServer()) {
        Serial.println(
            "Success! we should now be getting notifications, scanning for "
            "more!");
    } else {
        Serial.println("Failed to connect, starting scan");
    }
}
