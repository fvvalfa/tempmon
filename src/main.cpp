#include "config.h"
#include <esp_task_wdt.h>
#include <ETH.h>
#include "globals.h"
#include "Wire.h"
#include "display.h"
#include "sensors.h"
#include "webserver.h"
#include "snmp_module.h"


bool networkInitialized = 0;
bool eth_connected = 0;

static network_event_handle_t eth_event_handle = 0;


// ========== ИНИЦИАЛИЗАЦИЯ СЕТИ ==========
void resetNetwork() {
    pinMode(ETH_PHY_RST, OUTPUT);
    digitalWrite(ETH_PHY_RST, LOW);
    delay(10);
    digitalWrite(ETH_PHY_RST, HIGH);
    delay(100);
}

// ========== ETH EVENT ==========
void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println(F("ETH Started"));
            ETH.setHostname("temp-monitor");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println(F("ETH Connected"));
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print(F("ETH Got IP: "));
            Serial.println(ETH.localIP());
            eth_connected = true;
            networkInitialized = true;
            need_update_oled=true ;
            needInitSNMP=true;
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println(F("ETH Lost connection"));
            eth_connected = false;
            networkInitialized = false;
            needdeInitSNMP=true;
            need_update_oled=true;
            break;
        default:
            break;
    }
}

// ========== INIT NETWORK ==========
bool initNetwork() {
    Serial.println(F("\nInitializing Ethernet (ETH.h)..."));
    resetNetwork();
    // Если handle уже есть — удаляем старый колбэк
    if (eth_event_handle != 0) {
        Network.removeEvent(eth_event_handle);
        eth_event_handle = 0;
    }

    // Регистрируем колбэк и сохраняем handle
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
    Serial.println(F("DHCP mode: waiting for IP..."));
    return true;
}

// ========== NETWORK WATCHDOG ==========
void checkNetwork() {
    static unsigned long lastIpTime = 0;
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < CHECK_INTERVAL_NETWORK) return;
    lastCheck = millis();

    // 1) Линк есть и IP валиден
    if (eth_connected && ETH.localIP() != IPAddress(0,0,0,0)) {
        lastIpTime = 0;
        return;
    }

    // 2) Линк есть, но IP нет — ждём 10 сек
    if (eth_connected && ETH.localIP() == IPAddress(0,0,0,0)) {
        if (lastIpTime == 0) lastIpTime = millis();
        if (millis() - lastIpTime < 10000) return;
        Serial.println(F("NET: no IP for 10s, restarting ETH..."));
    }
    // 3) Линка нет
    else {
        Serial.println(F("NET: link down, reinitializing ETH..."));
        lastIpTime = 0;
    }

    // Реинициализация
    ETH.end();
    delay(200);
    deinitSNMP();
    needdeInitSNMP = false;    // ← гасим флаг, чтобы loop() не вызвал повторно
    deinitWebServer();
    initNetwork();
    initWebServer();
}

void setup() {
    
    Serial.begin(115200);
    //Настраиваем новую конфигурацию WDT на 20 секунд
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 20000,               // 20 секунд
        .idle_core_mask = 1 << 0,          // Мониторить ядро 0 (IDLE0)
        .trigger_panic = true              // Перезагружать при срабатывании
    };
    //Переконфигурируем работающий WDT
    esp_task_wdt_reconfigure(&wdt_config);
   //Регистрируем текущую задачу loopTask в WDT
    esp_task_wdt_add(NULL);


    delay(3000);
    initNetwork();
    initWebServer();   
    //Инициализация дисплея
    Wire.begin(21, 22);
     if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDRESS)) {
        Serial.println(F("SSD1306 not found!"));
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("=== TEMP MONITOR ==="));
    display.println(F("ETH.h version"));
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
    // Сбрасываем WDT, чтобы показать, что задача работает нормально
    esp_task_wdt_reset();
    unsigned long now = millis();
    bool dataReady = readTemperatures(buses);   // возвращает true при чтении
    if (dataReady) {
        updateSNMPValues(buses);
        need_update_oled = true;
    }

    // Мягкий rescan датчиков раз в 60 секунд
    if (now - lastSoftRescan >= 60000UL) {
        lastSoftRescan = now;
        softRescanBuses(buses);
        calculateScrollParams(buses);   // пересчёт прокрутки OLED
    }

    if (now - lastOledUpdate >= SCROLL_INTERVAL) {
        lastOledUpdate = now;
        tickScroll();
        need_update_oled= true;
    }

    checkNetwork();
    handleWebClient();
    if (snmp != nullptr) {
    snmp->loop();
    }
    else {
       static unsigned long lastNoSnmpMsg = 0;
        if (now - lastNoSnmpMsg >= 5000) {
            lastNoSnmpMsg = millis();
            Serial.println(F("No SNMP"));
    }
    }

    if (needInitSNMP) {initSNMP();needInitSNMP=false;};
    if (needdeInitSNMP) {deinitSNMP();needdeInitSNMP=false;};
    
    if (need_update_oled){updateOled(buses);need_update_oled=false;};
  }