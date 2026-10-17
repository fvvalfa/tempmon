#include "config.h"
#include <esp_task_wdt.h>

#include "globals.h"
#include "Wire.h"
#include "display.h"
#include "sensors.h"
#include "webserver.h"
#include "snmp_module.h"
#include "settings.h"
#include "network_module.h"



void setup() {
    Serial.begin(115200);
    esp_task_wdt_config_t wdt = { .timeout_ms = 20000, .idle_core_mask = 1 << 0, .trigger_panic = true };
    esp_task_wdt_reconfigure(&wdt);
    esp_task_wdt_add(NULL);
    delay(1000);

    loadSettings();
    Serial.print(F("DHCP=")); Serial.print(settings.useDHCP ? "Y" : "N");
    Serial.print(F(" IP=")); Serial.println(settingsIP());

    initNetwork();
    initWebServer();

    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS))
        Serial.println(F("SSD1306 not found"));
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.println(F("TEMP MONITOR")); display.display();

    initSensors(buses);
    calculateScrollParams(buses);
    display.clearDisplay(); display.setCursor(0, 0);
    display.println(F("TEMP MONITOR")); display.println(F("Wait network...")); display.display();
}

void loop() {
    static unsigned long lastOled = 0, lastRescan = 0;
    esp_task_wdt_reset();
    unsigned long now = millis();

    bool ready = readTemperatures(buses);
    if (ready) {
        updateSNMPValues(buses);
        checkAndSendTraps(buses);
        need_update_oled = true;
    }

    if (now - lastRescan >= 60000UL) {
        lastRescan = now;
        softRescanBuses(buses);
        calculateScrollParams(buses);
    }
    if (now - lastOled >= SCROLL_INTERVAL) {
        lastOled = now; tickScroll(); need_update_oled = true;
    }

    checkNetwork();
    handleWebClient();
    if (snmp) snmp->loop();

    if (needInitSNMP)  { initSNMP(); needInitSNMP = false; }
    if (needdeInitSNMP){ deinitSNMP(); needdeInitSNMP = false; }
    if (need_update_oled) { updateOled(buses); need_update_oled = false; }
}
