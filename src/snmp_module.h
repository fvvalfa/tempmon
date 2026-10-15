#pragma once
#include "globals.h"
#include "config.h"
#include <SNMP_Agent.h>


// Значения для OID: [шина][датчик], температура × 10
// Отсутствующий датчик = SNMP_NO_DATA (-1270)
extern int tempOID[BUS_COUNT][MAX_SENSORS_PER_BUS];
extern  SNMPAgent* snmp;
void initSNMP();                    
void deinitSNMP();
void updateSNMPValues(BusConfig (&bus)[BUS_COUNT]);