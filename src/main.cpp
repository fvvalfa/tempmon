#include "config.h"
#include <esp_task_wdt.h>
#include <ETH.h>
#include "globals.h"
#include "Wire.h"
#include "display.h"
#include "sensors.h"
#include "webserver.h"
#include "snmp_module.h"
#include "settings.h"


bool networkInitialized = 0;
bool eth_connected = 0;

static network_event_handle_t eth_event_handle = 0;

static bool usingStaticIP = false;
static unsigned long linkUpNoIpSince = 0;
static unsigned long linkDownSince = 0;
static unsigned long lastDhcpRetry = 0;


void resetNetwork() {
    pinMode(ETH_PHY_RST, OUTPUT);
    digitalWrite(ETH_PHY_RST, LOW);
    delay(10);
    digitalWrite(ETH_PHY_RST, HIGH);
    delay(100);
}

static void applyStaticIP() {
    IPAddress ip   = settingsIP();
    IPAddress gw   = settingsGW();
    IPAddress mask = settingsMask();
    IPAddress dns  = settingsDNS();

    Serial.print(F("NET: applying static IP "));
    Serial.println(ip);

    if (ETH.config(ip, gw, mask, dns)) {
        usingStaticIP = true;
        eth_connected = true;
        networkInitialized = true;
        need_update_oled = true;
        needInitSNMP = true;
        linkUpNoIpSince = 0;
        Serial.println(F("NET: static IP configured OK"));
    } else {
        Serial.println(F("NET: ETH.config(static) FAILED"));
    }
}

static void tryRestartDHCP() {
    Serial.println(F("NET: trying to restart DHCP..."));
    if (ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
                   IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
        usingStaticIP = false;
        linkUpNoIpSince = millis();
        lastDhcpRetry = millis();
        Serial.println(F("NET: DHCP client restarted"));
    } else {
        Serial.println(F("NET: failed to restart DHCP"));
        lastDhcpRetry = millis();
    }
}

void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println(F("ETH Started"));
            ETH.setHostname("temp-monitor");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println(F("ETH Connected (link up)"));
            eth_connected = true;
            linkDownSince = 0;
            if (ETH.localIP() == IPAddress(0, 0, 0, 0) && !usingStaticIP) {
                if (linkUpNoIpSince == 0) linkUpNoIpSince = millis();
            }
            if (!settings.useDHCP && !usingStaticIP) {
                applyStaticIP();
            }
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print(F("ETH Got IP: "));
            Serial.println(ETH.localIP());
            eth_connected = true;
            networkInitialized = true;
            usingStaticIP = false;
            linkUpNoIpSince = 0;
            linkDownSince = 0;
            need_update_oled = true;
            needInitSNMP = true;
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            Serial.println(F("ETH Lost IP"));
            networkInitialized = false;
            needdeInitSNMP = true;
            need_update_oled = true;
            if (!usingStaticIP && linkUpNoIpSince == 0)
                linkUpNoIpSince = millis();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println(F("ETH Disconnected (link down)"));
            eth_connected = false;
            networkInitialized = false;
            usingStaticIP = false;
            linkUpNoIpSince = 0;
            if (linkDownSince == 0) linkDownSince = millis();
            needdeInitSNMP = true;
            need_update_oled = true;
            break;
        default:
            break;
    }
}

bool initNetwork(bool doHwReset = true) {
    Serial.println(F("\nInitializing Ethernet (ETH.h)..."));
    if (doHwReset) resetNetwork();

    if (eth_event_handle != 0) {
        Network.removeEvent(eth_event_handle);
        eth_event_handle = 0;
    }

    eth_event_handle = Network.onEvent(onEthEvent);
    SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);

    bool ok = ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS,
                        ETH_PHY_IRQ, ETH_PHY_RST, SPI, ETH_PHY_SPI_FREQ_MHZ);

    if (!ok) {
        Serial.println(F("ETH.begin() failed!"));
        if (eth_event_handle != 0) {
            Network.removeEvent(eth_event_handle);
            eth_event_handle = 0;
        }
        return false;
    }

    usingStaticIP = false;
    linkUpNoIpSince = 0;
    linkDownSince = 0;
    lastDhcpRetry = 0;

    if (!settings.useDHCP) {
        Serial.println(F("Static IP mode (from settings)"));
        delay(50);
        applyStaticIP();
    } else {
        Serial.println(F("DHCP mode: waiting for IP..."));
    }
    return true;
}

void checkNetwork() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (now - lastCheck < CHECK_INTERVAL_NETWORK) return;
    lastCheck = now;

    bool hasLink = eth_connected || ETH.linkUp();
    bool hasIp   = (ETH.localIP() != IPAddress(0, 0, 0, 0));

    if (hasLink && hasIp) {
        linkUpNoIpSince = 0;
        linkDownSince = 0;
        if (usingStaticIP && settings.useDHCP) {
            if (lastDhcpRetry == 0) lastDhcpRetry = now;
            if (now - lastDhcpRetry >= DHCP_RETRY_INTERVAL_MS)
                tryRestartDHCP();
        }
        return;
    }

    if (hasLink && !hasIp) {
        linkDownSince = 0;
        if (usingStaticIP) {
            applyStaticIP();
            return;
        }
        if (linkUpNoIpSince == 0) linkUpNoIpSince = now;
        if (now - linkUpNoIpSince >= DHCP_TIMEOUT_MS) {
            Serial.println(F("NET: DHCP timeout → static IP"));
            applyStaticIP();
        }
        return;
    }

    if (linkDownSince == 0) linkDownSince = now;
    if (now - linkDownSince < LINK_DOWN_REINIT_MS) return;

    Serial.println(F("NET: link down too long → full reinit"));
    linkDownSince = 0;
    linkUpNoIpSince = 0;
    usingStaticIP = false;

    ETH.end();
    delay(200);
    deinitSNMP();
    needdeInitSNMP = false;
    deinitWebServer();
    initNetwork(true);
    initWebServer();
}

void setup() {
    Serial.begin(115200);

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 20000,
        .idle_core_mask = 1 << 0,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);
    esp_task_wdt_add(NULL);

    delay(1000);

    loadSettings();
    Serial.print(F("Settings: DHCP="));
    Serial.print(settings.useDHCP ? "yes" : "no");
    Serial.print(F(" IP="));
    Serial.println(settingsIP());

    initNetwork();
    initWebServer();

    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        Serial.println(F("SSD1306 not found!"));
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("=== TEMP MONITOR ==="));
    display.println(F("Loading..."));
    display.display();

    initSensors(buses);
    calculateScrollParams(buses);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("=== TEMP MONITOR ==="));
    display.println(F("Waiting network..."));
    display.display();
}

void loop() {
    static unsigned long lastOledUpdate = 0;
    static unsigned long lastSoftRescan = 0;

    esp_task_wdt_reset();
    unsigned long now = millis();

    bool dataReady = readTemperatures(buses);
    if (dataReady) {
        updateSNMPValues(buses);
        need_update_oled = true;
    }

    if (now - lastSoftRescan >= 60000UL) {
        lastSoftRescan = now;
        softRescanBuses(buses);
        calculateScrollParams(buses);
    }

    if (now - lastOledUpdate >= SCROLL_INTERVAL) {
        lastOledUpdate = now;
        tickScroll();
        need_update_oled = true;
    }

    checkNetwork();
    handleWebClient();

    if (snmp != nullptr) {
        snmp->loop();
    }

    if (needInitSNMP)  { initSNMP();  needInitSNMP = false; }
    if (needdeInitSNMP){ deinitSNMP(); needdeInitSNMP = false; }

    if (need_update_oled) {
        updateOled(buses);
        need_update_oled = false;
    }
}
