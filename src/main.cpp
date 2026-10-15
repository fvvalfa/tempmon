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

// true = сейчас работаем на статическом IP (fallback)
static bool usingStaticIP = false;

// Момент, когда впервые увидели линк без IP (для таймаута DHCP)
static unsigned long linkUpNoIpSince = 0;
// Момент, когда линк пропал (для отложенного полного reininit)
static unsigned long linkDownSince = 0;
// Когда последний раз пробовали вернуться на DHCP
static unsigned long lastDhcpRetry = 0;


// ========== HARDWARE RESET PHY (только при реальной необходимости) ==========
void resetNetwork() {
    pinMode(ETH_PHY_RST, OUTPUT);
    digitalWrite(ETH_PHY_RST, LOW);
    delay(10);
    digitalWrite(ETH_PHY_RST, HIGH);
    delay(100);
}

// ========== ПРИМЕНИТЬ СТАТИЧЕСКИЙ IP ==========
static void applyStaticIP() {
    IPAddress ip(MYIPADDR);
    IPAddress gw(MYGW);
    IPAddress mask(MYIPMASK);
    IPAddress dns(MYDNS);

    Serial.print(F("NET: applying static IP "));
    Serial.println(ip);

    // После begin() можно переключить на static — DHCP-клиент отключается
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

// ========== ПОПЫТКА ВЕРНУТЬСЯ НА DHCP ==========
static void tryRestartDHCP() {
    Serial.println(F("NET: trying to restart DHCP..."));
    // Передаём 0.0.0.0 — в arduino-esp32 это включает DHCP-клиент обратно
    if (ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
                   IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
        usingStaticIP = false;
        linkUpNoIpSince = millis();   // начнём заново отсчитывать таймаут
        lastDhcpRetry = millis();
        Serial.println(F("NET: DHCP client restarted, waiting for offer..."));
    } else {
        Serial.println(F("NET: failed to restart DHCP, stay on static"));
        lastDhcpRetry = millis();
    }
}

// ========== ETH EVENT ==========
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
            // Если ещё нет IP — начинаем отсчёт DHCP-таймаута
            if (ETH.localIP() == IPAddress(0, 0, 0, 0) && !usingStaticIP) {
                if (linkUpNoIpSince == 0) linkUpNoIpSince = millis();
            }
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print(F("ETH Got IP: "));
            Serial.println(ETH.localIP());
            eth_connected = true;
            networkInitialized = true;
            usingStaticIP = false;          // успешно получили через DHCP
            linkUpNoIpSince = 0;
            linkDownSince = 0;
            need_update_oled = true;
            needInitSNMP = true;
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            Serial.println(F("ETH Lost IP"));
            // IP пропал, но линк может ещё быть
            networkInitialized = false;
            needdeInitSNMP = true;
            need_update_oled = true;
            if (!usingStaticIP && linkUpNoIpSince == 0) {
                linkUpNoIpSince = millis();
            }
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

// ========== INIT NETWORK ==========
// doHwReset = true  → полный hardware reset PHY (редко)
// doHwReset = false → только программный restart ETH
bool initNetwork(bool doHwReset = true) {
    Serial.println(F("\nInitializing Ethernet (ETH.h)..."));
    if (doHwReset) {
        resetNetwork();
    }

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

    usingStaticIP = false;
    linkUpNoIpSince = 0;
    linkDownSince = 0;
    lastDhcpRetry = 0;
    Serial.println(F("DHCP mode: waiting for IP..."));
    return true;
}

// ========== NETWORK WATCHDOG ==========
void checkNetwork() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (now - lastCheck < CHECK_INTERVAL_NETWORK) return;
    lastCheck = now;

    bool hasLink = eth_connected || ETH.linkUp();
    bool hasIp   = (ETH.localIP() != IPAddress(0, 0, 0, 0));

    // ---------- 1. Всё хорошо: линк + IP ----------
    if (hasLink && hasIp) {
        linkUpNoIpSince = 0;
        linkDownSince = 0;

        // Если работаем на статике — периодически пробуем вернуться на DHCP
        if (usingStaticIP) {
            if (lastDhcpRetry == 0) lastDhcpRetry = now;
            if (now - lastDhcpRetry >= DHCP_RETRY_INTERVAL_MS) {
                tryRestartDHCP();
            }
        }
        return;
    }

    // ---------- 2. Линк есть, но IP нет ----------
    if (hasLink && !hasIp) {
        linkDownSince = 0;

        if (usingStaticIP) {
            // Странно: статика должна была дать IP. Попробуем применить снова.
            Serial.println(F("NET: static mode but no IP — re-applying static"));
            applyStaticIP();
            return;
        }

        // Ждём DHCP
        if (linkUpNoIpSince == 0) linkUpNoIpSince = now;

        if (now - linkUpNoIpSince >= DHCP_TIMEOUT_MS) {
            Serial.println(F("NET: DHCP timeout → fallback to static IP"));
            applyStaticIP();
        }
        return;
    }

    // ---------- 3. Линка нет ----------
    if (linkDownSince == 0) linkDownSince = now;

    // Не дёргаем hardware-reset слишком часто
    if (now - linkDownSince < LINK_DOWN_REINIT_MS) {
        // Просто ждём — может линк вернётся сам
        return;
    }

    Serial.println(F("NET: link down for too long → full reinit"));
    linkDownSince = 0;
    linkUpNoIpSince = 0;
    usingStaticIP = false;

    ETH.end();
    delay(200);
    deinitSNMP();
    needdeInitSNMP = false;
    deinitWebServer();

    // Полный hardware reset только после длительного отсутствия линка
    initNetwork(true);
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