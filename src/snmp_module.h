#pragma once
#include "globals.h"
#include "config.h"
#include <SNMP_Agent.h>

extern int tempOID[BUS_COUNT][MAX_SENSORS_PER_BUS];
extern SNMPAgent* snmp;

void initSNMP();
void deinitSNMP();
void updateSNMPValues(BusConfig (&bus)[BUS_COUNT]);
void checkAndSendTraps(BusConfig (&bus)[BUS_COUNT]);  // вызывать после чтения температур
