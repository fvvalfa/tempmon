#pragma once
#include <OneWire.h>
#include "config.h"
#include <DallasTemperature.h>



extern bool networkInitialized ;
extern bool eth_connected ;

extern volatile bool need_update_oled;

extern volatile bool needInitSNMP;
extern volatile bool needdeInitSNMP;

const uint8_t ONE_WIRE_BUSES_PINS[] = {
    ONE_WIRE_BUS_1,
    ONE_WIRE_BUS_2,
    ONE_WIRE_BUS_3,
    ONE_WIRE_BUS_4,     
};
const size_t BUS_COUNT = sizeof(ONE_WIRE_BUSES_PINS) / sizeof(ONE_WIRE_BUSES_PINS[0]);

// ========== СТРУКТУРА ДЛЯ ШИНЫ ==========
struct BusConfig {
    int pin;
    OneWire* oneWire;
    DallasTemperature* sensors;
    DeviceAddress addresses[MAX_SENSORS_PER_BUS];
    float temperatures[MAX_SENSORS_PER_BUS];
    bool sensorOk[MAX_SENSORS_PER_BUS];
    int count;
};
extern BusConfig buses[BUS_COUNT];

inline bool isSensorVisible(const BusConfig &bus, int i) {
    if (i < 0 || i >= bus.count) return false;
    if (!bus.sensorOk[i]) return false;
    float t = bus.temperatures[i];
    return t != -127.0f && t != DEVICE_DISCONNECTED_C;
}

//Общее количество датчиков
extern int totalSensors;
//Нужно ли скролить датчики
extern bool needScroll;

