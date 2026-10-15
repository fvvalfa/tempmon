#include "sensors.h"

static void printAddress(DeviceAddress deviceAddress);

void setTotalSensors(BusConfig (&bus)[BUS_COUNT]) {
    totalSensors = 0;
    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < bus[b].count; i++) {
            if (bus[b].sensorOk[i]) totalSensors++;
        }
    }
}


// ========== ОСВОБОЖДЕНИЕ РЕСУРСОВ ==========
void deinitSensors(BusConfig (&bus)[BUS_COUNT]) {
    for (int b = 0; b < BUS_COUNT; b++) {
        if (bus[b].sensors) {
            delete bus[b].sensors;
            bus[b].sensors = nullptr;
        }
        if (bus[b].oneWire) {
            delete bus[b].oneWire;
            bus[b].oneWire = nullptr;
        }
        bus[b].count = 0;
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            bus[b].sensorOk[i] = false;
            bus[b].temperatures[i] = -127.0f;
        }
    }
    totalSensors = 0;
}

// ========== ИНИЦИАЛИЗАЦИЯ ДАТЧИКОВ ==========
void initSensors(BusConfig (&bus)[BUS_COUNT]) {
    Serial.println(F("\n=== Initializing DS18B20 sensors ==="));
    // Защита от повторного вызова / утечек
    deinitSensors(bus);
    for (int b = 0; b < BUS_COUNT; b++) {
        bus[b].pin=ONE_WIRE_BUSES_PINS[b];
        bus[b].count = 0;

        bus[b].oneWire = new OneWire(bus[b].pin);
        bus[b].sensors = new DallasTemperature(bus[b].oneWire);
        bus[b].sensors->begin();
        bus[b].sensors->setResolution(TEMPERATURE_PRECISION);
        // Опрос без задержки
        bus[b].sensors->setWaitForConversion(false); 
        scanBus(bus[b], b);
    }

    setTotalSensors(buses);

    Serial.print(F("\nTotal sensors found: "));
    Serial.println(totalSensors);
}



// ========== СКАНИРОВАНИЕ ОДНОЙ ШИНЫ ==========
void scanBus(BusConfig &bus, int busIndex) {
    bus.count = bus.sensors->getDeviceCount();

    if (bus.count == 0) {
        Serial.print(F("  Bus "));
        Serial.print(busIndex + 1);
        Serial.println(F(": No sensors found!"));
        return;
    }

    Serial.print(F("  Bus "));
    Serial.print(busIndex + 1);
    Serial.print(F(": found "));
    Serial.print(bus.count);
    Serial.println(F(" sensors"));

    if (bus.count > MAX_SENSORS_PER_BUS) {
        Serial.print(F("    Limiting to "));
        Serial.println(MAX_SENSORS_PER_BUS);
        bus.count = MAX_SENSORS_PER_BUS;
    }

    for (int i = 0; i < bus.count; i++) {
        if (bus.sensors->getAddress(bus.addresses[i], i)) {
            bus.sensorOk[i] = true;
            Serial.print(F("    Sensor "));
            Serial.print(i + 1);
            Serial.print(F(": "));
            printAddress(bus.addresses[i]);

            float t = bus.sensors->getTempC(bus.addresses[i]);
            if (t != DEVICE_DISCONNECTED_C) {
                Serial.print(F(" temp: "));
                Serial.print(t, 1);
                Serial.print(F("°C"));
            }
            Serial.println();
        } else {
            bus.sensorOk[i] = false;
            Serial.print(F("    Sensor "));
            Serial.print(i + 1);
            Serial.println(F(": FAILED to get address"));
        }
    }
}

void softRescanBuses(BusConfig (&bus)[BUS_COUNT]) {
    Serial.println(F("Soft rescan buses..."));
    bool changed = false;

    for (int b = 0; b < BUS_COUNT; b++) {
        if (!bus[b].sensors) continue;

        // Если шина пустая — попробуем просканировать заново
        if (bus[b].count == 0) {
            scanBus(bus[b], b);
            if (bus[b].count > 0) changed = true;
            continue;
        }

        // Проверяем существующие датчики
        bus[b].sensors->requestTemperatures();
        // Небольшая задержка только для rescan (редко вызывается)
        delay(50);

        for (int i = 0; i < bus[b].count; i++) {
            float t = bus[b].sensors->getTempC(bus[b].addresses[i]);
            if (t == DEVICE_DISCONNECTED_C) {
                if (bus[b].sensorOk[i]) {
                    bus[b].sensorOk[i] = false;
                    bus[b].temperatures[i] = -127.0f;
                    Serial.printf("  Bus %d Sensor %d disappeared\n", b + 1, i + 1);
                    changed = true;
                }
            } else {
                if (!bus[b].sensorOk[i]) {
                    bus[b].sensorOk[i] = true;
                    bus[b].temperatures[i] = t;
                    Serial.printf("  Bus %d Sensor %d reconnected (%.1f C)\n", b + 1, i + 1, t);
                    changed = true;
                }
            }
        }
    }

    if (changed) {
        setTotalSensors(buses);
        need_update_oled = true;
    }
}
// ========== ВЫВОД АДРЕСА ==========
static void printAddress(DeviceAddress deviceAddress) {
    for (uint8_t i = 0; i < 8; i++) {
        if (deviceAddress[i] < 16) Serial.print(F("0"));
        Serial.print(deviceAddress[i], HEX);
        if (i < 7) Serial.print(F(":"));
    }
}

unsigned long getConversionTime(uint8_t resolution) {
    switch (resolution) {
        case 9:  return 94;
        case 10: return 188;
        case 11: return 375;
        case 12:
        default: return 750;
    }
}

// ========== ЧТЕНИЕ ТЕМПЕРАТУР ==========
bool readTemperatures(BusConfig (&bus)[BUS_COUNT]) {
    // Статические переменные помнят состояние между вызовами

    // Ранний выход: нет ни одного датчика на всех шинах
    bool anySensor = false;
    for (int b = 0; b < BUS_COUNT; b++) {
        if (bus[b].count > 0) { anySensor = true; break; }
    }
    if (!anySensor) return false;

    static unsigned long lastRequestTime = 0;
    static bool conversionInProgress = false;
    
    static unsigned long conversionTime = 0;
    if (conversionTime == 0) {
        unsigned long maxConv = 0;
        for (int b = 0; b < BUS_COUNT; b++) {
            if (bus[b].sensors != nullptr) {
                unsigned long t = getConversionTime(bus[b].sensors->getResolution());
                if (t > maxConv) maxConv = t;
            }
        }
        if (maxConv == 0) maxConv = 750;   // fallback: 12 бит
        conversionTime = maxConv;
        Serial.printf("DS18B20: время преобразования = %lu мс\n", conversionTime);
    }
    unsigned long now = millis();

    // ЭТАП 1: Если преобразование не запущено — запускаем его
    if (!conversionInProgress) {
        for (int b = 0; b < BUS_COUNT; b++) {
            if (bus[b].count > 0) {
                bus[b].sensors->requestTemperatures(); // Неблокирующий вызов
            }
        }
        lastRequestTime = now;
        conversionInProgress = true;
        return false; // Выходим, чтобы не читать данные сразу
    }

    // ЭТАП 2: Проверяем, прошло ли достаточно времени
    bool anyDisconnected = false;
    if (now - lastRequestTime >= conversionTime) {
        for (int b = 0; b < BUS_COUNT; b++) {
            for (int i = 0; i < bus[b].count; i++) {
                if (bus[b].sensorOk[i]) {
                    bus[b].temperatures[i] = bus[b].sensors->getTempC(bus[b].addresses[i]);
                    if (bus[b].temperatures[i] == DEVICE_DISCONNECTED_C) {
                        bus[b].temperatures[i] = -127.0;
                        bus[b].sensorOk[i] = false;
                        anyDisconnected = true;
                        
                    }
                }
            }
        }
        if (anyDisconnected) {setTotalSensors(buses);};
        // Выводим лог температур 
        Serial.print(F("T "));
            for (int b = 0; b < BUS_COUNT; b++) {
        Serial.print(F("[B"));
        Serial.print(b + 1);
        Serial.print(F("] "));
        for (int i = 0; i < bus[b].count; i++) {
            if (isSensorVisible(bus[b], i)) {
                Serial.print(bus[b].temperatures[i], 1);
            } else {
                Serial.print(F("---"));
            }
            Serial.print(F("°C "));
        }
        Serial.print(F(" | "));
        }
        Serial.println();
            
        conversionInProgress = false; // Готовы к следующему запросу
        return true;
    }
    return false;
}
