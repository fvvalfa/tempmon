#pragma once

#include "Arduino.h"
#include <globals.h>

void initSensors(BusConfig (&bus)[BUS_COUNT]);
void scanBus(BusConfig &bus, int busIndex);
bool readTemperatures(BusConfig (&bus)[BUS_COUNT]);
void softRescanBuses(BusConfig (&bus)[BUS_COUNT]);
unsigned long getConversionTime(uint8_t resolution);