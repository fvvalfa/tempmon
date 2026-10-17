#pragma once
#include "config.h"
#include <IPAddress.h>

struct UserAccount {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    bool active;
};

// Per-sensor trap config
struct SensorTrapCfg {
    bool   tempEnabled;      // trap по превышению температуры
    float  tempThreshold;    // °C
    uint16_t holdSec;        // сколько секунд держать превышение
    bool   missingEnabled;   // trap если датчик пропал
};

struct AppSettings {
    bool useDHCP;
    uint8_t ip[4], gw[4], mask[4], dns[4];

    bool snmpEnabled;
    char snmpCommunityRead[MAX_SNMP_COMM_LEN];
    char snmpCommunityWrite[MAX_SNMP_COMM_LEN];
    char snmpOidPrefix[MAX_SNMP_OID_LEN];

    // Trap manager
    uint8_t trapManagerIP[4];   // куда слать traps
    bool    trapsEnabled;       // глобальный выключатель traps

    char sensorNames[4][MAX_SENSORS_PER_BUS][MAX_SENSOR_NAME_LEN];
    SensorTrapCfg trapCfg[4][MAX_SENSORS_PER_BUS];

    UserAccount users[MAX_USERS];
    uint8_t userCount;
};

extern AppSettings settings;

void loadSettings();
void saveSettings();
void resetSettingsToDefault();

IPAddress settingsIP();
IPAddress settingsGW();
IPAddress settingsMask();
IPAddress settingsDNS();
IPAddress settingsTrapIP();
void setSettingsIP(const IPAddress &ip, const IPAddress &gw,
                   const IPAddress &mask, const IPAddress &dns);

const char* getSensorName(int bus, int sensor);
bool setSensorName(int bus, int sensor, const char *name);

bool authenticateUser(const char *user, const char *pass);
int  findUser(const char *user);
bool addUser(const char *user, const char *pass);
bool deleteUser(const char *user);
bool changePassword(const char *user, const char *newPass);
