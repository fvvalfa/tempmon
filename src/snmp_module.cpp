#include "snmp_module.h"
#include <SNMP_Agent.h>
#include "globals.h"
#include "config.h"
#include "Network.h"

//SNMPAgent   snmp(SNMP_COMMUNITY_READ, SNMP_COMMUNITY_WRITE);
SNMPAgent* snmp = nullptr;
static NetworkUDP* snmpUdp;   // <-- создаем UDP-объект для ETH
int tempOID[BUS_COUNT][MAX_SENSORS_PER_BUS];

void initSNMP() {
    Serial.println(F("\n=== Initializing SNMP ==="));
    if (snmp != nullptr) return;          // защита от повторного вызова

     if (!eth_connected) {
        Serial.println(F("SNMP: ETH not connected, deferring"));
        return;
    }
     // Создаём UDP только теперь, когда Ethernet уже работает
    snmpUdp = new NetworkUDP();
    if (snmpUdp == nullptr) {
        Serial.println(F("SNMP: UDP allocation failed!"));
        return;
    }
    if (!snmpUdp->begin(161)) {
    Serial.println(F("SNMP: UDP begin(161) FAILED!"));
    delete snmpUdp; 
    snmpUdp = nullptr;
    return;
    }
    snmp = new SNMPAgent(SNMP_COMMUNITY_READ, SNMP_COMMUNITY_WRITE);
    if (snmp == nullptr) {
        Serial.println(F("SNMP: allocation failed!"));
        snmpUdp->stop();
        delete snmpUdp;
        snmpUdp = nullptr;
        return;
        
    }
    
    snmp->setUDP(snmpUdp);
    
    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            tempOID[b][i] = SNMP_NO_DATA;
        }
    }

    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            char oidStr[48];
            snprintf(oidStr, sizeof(oidStr), SNMP_OID_PREFIX ".%d.%d.0", b + 1, i + 1);
            snmp->addIntegerHandler(oidStr, &tempOID[b][i],false,false);
        }
    }
    snmp->sortHandlers();
    snmp->begin();
    Serial.print(F("SNMP OID base: "));
    Serial.println(F(SNMP_OID_PREFIX));
    Serial.println(F("SNMP agent listening on UDP/161 (read-only)"));

}

void deinitSNMP() {
    Serial.println(F("SNMP: deinit"));

    if (snmp == nullptr && snmpUdp == nullptr) return;

    // 1. Останавливаем UDP
    if (snmpUdp) {
        snmpUdp->stop();
    }

    // 2. Удаляем агент
    if (snmp) {
        delete snmp;
        snmp = nullptr;
    }

    // 3. Освобождаем UDP
    if (snmpUdp) {
        delete snmpUdp;
        snmpUdp = nullptr;
    }
}

void updateSNMPValues(BusConfig (&bus)[BUS_COUNT]) {
    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            if (i < bus[b].count && isSensorVisible(bus[b], i)) {
                tempOID[b][i] = (int32_t)lroundf(bus[b].temperatures[i] * 10.0f);
            } else {
                tempOID[b][i] = SNMP_NO_DATA;
            }
        }
    }
}