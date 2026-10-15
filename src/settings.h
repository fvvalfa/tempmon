#pragma once

#include "config.h"
#include "globals.h"
#include <IPAddress.h>

struct UserAccount {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    bool active;
};

struct AppSettings {
    // --- Network ---
    bool useDHCP;           // true = пытаться DHCP, false = сразу static
    uint8_t ip[4];
    uint8_t gw[4];
    uint8_t mask[4];
    uint8_t dns[4];

    // --- SNMP ---
    bool snmpEnabled;
    char snmpCommunityRead[MAX_SNMP_COMM_LEN];
    char snmpCommunityWrite[MAX_SNMP_COMM_LEN];
    char snmpOidPrefix[MAX_SNMP_OID_LEN];

    // --- Sensor names [bus][sensor] ---
    char sensorNames[4][MAX_SENSORS_PER_BUS][MAX_SENSOR_NAME_LEN];

    // --- Users ---
    UserAccount users[MAX_USERS];
    uint8_t userCount;
};

extern AppSettings settings;

void loadSettings();          // из NVS при старте
void saveSettings();          // в NVS
void resetSettingsToDefault();

// Хелперы
IPAddress settingsIP();
IPAddress settingsGW();
IPAddress settingsMask();
IPAddress settingsDNS();
void setSettingsIP(const IPAddress &ip, const IPAddress &gw,
                   const IPAddress &mask, const IPAddress &dns);

const char* getSensorName(int bus, int sensor);  // bus/sensor 0-based
bool setSensorName(int bus, int sensor, const char *name);

bool authenticateUser(const char *user, const char *pass);
int  findUser(const char *user);   // -1 если нет
bool addUser(const char *user, const char *pass);
bool deleteUser(const char *user);
bool changePassword(const char *user, const char *newPass);
