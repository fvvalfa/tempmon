#include "snmp_module.h"
#include <SNMP_Agent.h>
#include <SNMPTrap.h>
#include "globals.h"
#include "config.h"
#include "settings.h"
#include "Network.h"
#include <ETH.h>

SNMPAgent* snmp = nullptr;
static NetworkUDP* snmpUdp = nullptr;
int tempOID[BUS_COUNT][MAX_SENSORS_PER_BUS];

// Trap infrastructure
static SNMPTrap* trapHighTemp = nullptr;
static SNMPTrap* trapMissing  = nullptr;
static TimestampCallback* uptimeCb = nullptr;
static uint32_t tensOfMillis = 0;

// Runtime state per sensor
struct TrapRuntime {
    unsigned long overSince;     // когда впервые превысили порог (0 = не превышен)
    bool highTempSent;           // trap уже отправлен за текущий эпизод
    bool wasPresent;             // был ли датчик виден в прошлый раз
    bool missingSent;            // trap missing уже отправлен
};
static TrapRuntime trapRt[4][MAX_SENSORS_PER_BUS];

static void freeTraps() {
    if (trapHighTemp) { delete trapHighTemp; trapHighTemp = nullptr; }
    if (trapMissing)  { delete trapMissing;  trapMissing  = nullptr; }
    uptimeCb = nullptr;
}

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
    if (!snmpUdp) { Serial.println(F("SNMP: UDP alloc fail")); return; }
    if (!snmpUdp->begin(161)) {
        Serial.println(F("SNMP: UDP begin(161) FAIL"));
        delete snmpUdp; snmpUdp = nullptr; return;
    }

    snmp = new SNMPAgent(settings.snmpCommunityRead, settings.snmpCommunityWrite);
    if (!snmp) {
        snmpUdp->stop(); delete snmpUdp; snmpUdp = nullptr; return;
    }
    snmp->setUDP(snmpUdp);

    for (int b = 0; b < BUS_COUNT; b++)
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++)
            tempOID[b][i] = SNMP_NO_DATA;

    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            char oidStr[64];
            snprintf(oidStr, sizeof(oidStr), "%s.%d.%d.0",
                     settings.snmpOidPrefix, b + 1, i + 1);
            snmp->addIntegerHandler(oidStr, &tempOID[b][i], false, false);
        }
    }

    // Uptime for traps (sysUpTime style, in 1/100 s)
    tensOfMillis = millis() / 10;
    uptimeCb = (TimestampCallback*)snmp->addTimestampHandler(
        ".1.3.6.1.2.1.1.3.0", &tensOfMillis);

    // --- Traps setup ---
    freeTraps();
    if (settings.trapsEnabled) {
        char trapOid[64];
        snprintf(trapOid, sizeof(trapOid), "%s.0", settings.snmpOidPrefix);

        trapHighTemp = new SNMPTrap(settings.snmpCommunityRead, SNMP_VERSION_2C);
        trapHighTemp->setUDP(snmpUdp);
        trapHighTemp->setTrapOID(new OIDType(trapOid));
        trapHighTemp->setSpecificTrap(TRAP_SPECIFIC_HIGH_TEMP);
        trapHighTemp->setUptimeCallback(uptimeCb);
        trapHighTemp->setInform(false);
        trapHighTemp->setIP(ETH.localIP());

        trapMissing = new SNMPTrap(settings.snmpCommunityRead, SNMP_VERSION_2C);
        trapMissing->setUDP(snmpUdp);
        trapMissing->setTrapOID(new OIDType(trapOid));
        trapMissing->setSpecificTrap(TRAP_SPECIFIC_MISSING);
        trapMissing->setUptimeCallback(uptimeCb);
        trapMissing->setInform(false);
        trapMissing->setIP(ETH.localIP());

        // reset runtime
        memset(trapRt, 0, sizeof(trapRt));
        for (int b = 0; b < BUS_COUNT; b++)
            for (int i = 0; i < MAX_SENSORS_PER_BUS; i++)
                trapRt[b][i].wasPresent = true;  // не слать missing при старте

        Serial.println(F("SNMP traps ready"));
    }

    snmp->sortHandlers();
    snmp->begin();
    Serial.print(F("SNMP OID base: "));
    Serial.println(settings.snmpOidPrefix);
    Serial.println(F("SNMP agent on UDP/161"));
}

void deinitSNMP() {
    Serial.println(F("SNMP: deinit"));
    freeTraps();
    if (snmpUdp) snmpUdp->stop();
    if (snmp) { delete snmp; snmp = nullptr; }
    if (snmpUdp) { delete snmpUdp; snmpUdp = nullptr; }
}

void updateSNMPValues(BusConfig (&bus)[BUS_COUNT]) {
    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            if (i < bus[b].count && isSensorVisible(bus[b], i))
                tempOID[b][i] = (int32_t)lroundf(bus[b].temperatures[i] * 10.0f);
            else
                tempOID[b][i] = SNMP_NO_DATA;
        }
    }
}

void checkAndSendTraps(BusConfig (&bus)[BUS_COUNT]) {
    if (!snmp || !settings.trapsEnabled || !settings.snmpEnabled) return;
    if (!eth_connected) return;

    tensOfMillis = millis() / 10;
    IPAddress dest = settingsTrapIP();
    if (dest == IPAddress(0, 0, 0, 0)) return;

    unsigned long now = millis();

    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < bus[b].count && i < MAX_SENSORS_PER_BUS; i++) {
            SensorTrapCfg &cfg = settings.trapCfg[b][i];
            TrapRuntime &rt = trapRt[b][i];
            bool present = isSensorVisible(bus[b], i);

            // ---- Missing sensor trap ----
            if (cfg.missingEnabled) {
                if (present) {
                    rt.wasPresent = true;
                    rt.missingSent = false;
                } else if (rt.wasPresent && !rt.missingSent && trapMissing) {
                    // датчик был и пропал
                    Serial.printf("TRAP: sensor B%dS%d missing\n", b + 1, i + 1);
                    // привязываем OID температуры этого датчика
                    // (значение уже SNMP_NO_DATA)
                    snmp->sendTrapTo(trapMissing, dest, true, 0, 0);
                    rt.missingSent = true;
                    rt.wasPresent = false;
                }
            }

            // ---- High temperature trap ----
            if (cfg.tempEnabled && present) {
                float t = bus[b].temperatures[i];
                if (t >= cfg.tempThreshold) {
                    if (rt.overSince == 0) rt.overSince = now;
                    unsigned long held = (now - rt.overSince) / 1000UL;
                    if (held >= cfg.holdSec && !rt.highTempSent && trapHighTemp) {
                        Serial.printf("TRAP: B%dS%d high temp %.1f >= %.1f for %us\n",
                                      b + 1, i + 1, t, cfg.tempThreshold, cfg.holdSec);
                        snmp->sendTrapTo(trapHighTemp, dest, true, 0, 0);
                        rt.highTempSent = true;
                    }
                } else {
                    // температура упала — сбрасываем, можно снова слать
                    rt.overSince = 0;
                    rt.highTempSent = false;
                }
            } else if (!present) {
                rt.overSince = 0;
                rt.highTempSent = false;
            }
        }
    }
}
