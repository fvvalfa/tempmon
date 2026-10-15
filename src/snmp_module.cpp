#include "snmp_module.h"
#include <SNMP_Agent.h>
#include "globals.h"
#include "config.h"
#include "settings.h"
#include "Network.h"

SNMPAgent* snmp = nullptr;
static NetworkUDP* snmpUdp = nullptr;
int tempOID[BUS_COUNT][MAX_SENSORS_PER_BUS];

void initSNMP() {
    Serial.println(F("\n=== Initializing SNMP ==="));
    if (snmp != nullptr) return;

    if (!settings.snmpEnabled) {
        Serial.println(F("SNMP: disabled in settings"));
        return;
    }

    if (!eth_connected) {
        Serial.println(F("SNMP: ETH not connected, deferring"));
        return;
    }

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

    snmp = new SNMPAgent(settings.snmpCommunityRead, settings.snmpCommunityWrite);
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
            char oidStr[64];
            snprintf(oidStr, sizeof(oidStr), "%s.%d.%d.0",
                     settings.snmpOidPrefix, b + 1, i + 1);
            snmp->addIntegerHandler(oidStr, &tempOID[b][i], false, false);
        }
    }
    snmp->sortHandlers();
    snmp->begin();
    Serial.print(F("SNMP OID base: "));
    Serial.println(settings.snmpOidPrefix);
    Serial.println(F("SNMP agent listening on UDP/161"));
}

void deinitSNMP() {
    Serial.println(F("SNMP: deinit"));

    if (snmp == nullptr && snmpUdp == nullptr) return;

    if (snmpUdp) {
        snmpUdp->stop();
    }
    if (snmp) {
        delete snmp;
        snmp = nullptr;
    }
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
