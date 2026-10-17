#include "settings.h"
#include <Preferences.h>
#include <string.h>

AppSettings settings;
static Preferences prefs;

static void setDefaultIP() {
    uint8_t a[] = {MYIPADDR}; uint8_t g[] = {MYGW};
    uint8_t m[] = {MYIPMASK}; uint8_t d[] = {MYDNS};
    memcpy(settings.ip, a, 4); memcpy(settings.gw, g, 4);
    memcpy(settings.mask, m, 4); memcpy(settings.dns, d, 4);
    // trap manager default = gateway
    memcpy(settings.trapManagerIP, g, 4);
}

void resetSettingsToDefault() {
    memset(&settings, 0, sizeof(settings));
    settings.useDHCP = true;
    setDefaultIP();
    settings.snmpEnabled = true;
    settings.trapsEnabled = true;
    strncpy(settings.snmpCommunityRead,  SNMP_COMMUNITY_READ,  MAX_SNMP_COMM_LEN - 1);
    strncpy(settings.snmpCommunityWrite, SNMP_COMMUNITY_WRITE, MAX_SNMP_COMM_LEN - 1);
    strncpy(settings.snmpOidPrefix,      SNMP_OID_PREFIX,      MAX_SNMP_OID_LEN - 1);

    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            snprintf(settings.sensorNames[b][i], MAX_SENSOR_NAME_LEN, "B%dS%d", b + 1, i + 1);
            settings.trapCfg[b][i].tempEnabled = false;
            settings.trapCfg[b][i].tempThreshold = 50.0f;
            settings.trapCfg[b][i].holdSec = 60;
            settings.trapCfg[b][i].missingEnabled = false;
        }
    }

    settings.userCount = 1;
    strncpy(settings.users[0].username, "admin", MAX_USERNAME_LEN - 1);
    strncpy(settings.users[0].password, "admin", MAX_PASSWORD_LEN - 1);
    settings.users[0].active = true;
}

void loadSettings() {
    resetSettingsToDefault();
    if (!prefs.begin("tempmon", true)) {
        Serial.println(F("Settings: NVS RO fail, defaults"));
        return;
    }
    size_t len = prefs.getBytesLength("cfg");
    if (len == sizeof(AppSettings)) {
        prefs.getBytes("cfg", &settings, sizeof(AppSettings));
        Serial.println(F("Settings: loaded from NVS"));
    } else {
        Serial.printf("Settings: blob size %u != %u, defaults\n", (unsigned)len, (unsigned)sizeof(AppSettings));
    }
    prefs.end();

    bool any = false;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++)
        if (settings.users[i].active && settings.users[i].username[0]) any = true;
    if (!any) {
        settings.userCount = 1;
        strncpy(settings.users[0].username, "admin", MAX_USERNAME_LEN - 1);
        strncpy(settings.users[0].password, "admin", MAX_PASSWORD_LEN - 1);
        settings.users[0].active = true;
    }
}

void saveSettings() {
    if (!prefs.begin("tempmon", false)) {
        Serial.println(F("Settings: NVS RW fail"));
        return;
    }
    prefs.putBytes("cfg", &settings, sizeof(AppSettings));
    prefs.end();
    Serial.println(F("Settings: saved"));
}

IPAddress settingsIP()   { return IPAddress(settings.ip[0], settings.ip[1], settings.ip[2], settings.ip[3]); }
IPAddress settingsGW()   { return IPAddress(settings.gw[0], settings.gw[1], settings.gw[2], settings.gw[3]); }
IPAddress settingsMask() { return IPAddress(settings.mask[0], settings.mask[1], settings.mask[2], settings.mask[3]); }
IPAddress settingsDNS()  { return IPAddress(settings.dns[0], settings.dns[1], settings.dns[2], settings.dns[3]); }
IPAddress settingsTrapIP(){ return IPAddress(settings.trapManagerIP[0], settings.trapManagerIP[1],
                                              settings.trapManagerIP[2], settings.trapManagerIP[3]); }

void setSettingsIP(const IPAddress &ip, const IPAddress &gw,
                   const IPAddress &mask, const IPAddress &dns) {
    for (int i = 0; i < 4; i++) {
        settings.ip[i] = ip[i]; settings.gw[i] = gw[i];
        settings.mask[i] = mask[i]; settings.dns[i] = dns[i];
    }
}

const char* getSensorName(int bus, int sensor) {
    if (bus < 0 || bus >= 4 || sensor < 0 || sensor >= MAX_SENSORS_PER_BUS) return "???";
    if (!settings.sensorNames[bus][sensor][0]) return "---";
    return settings.sensorNames[bus][sensor];
}

bool setSensorName(int bus, int sensor, const char *name) {
    if (bus < 0 || bus >= 4 || sensor < 0 || sensor >= MAX_SENSORS_PER_BUS || !name) return false;
    strncpy(settings.sensorNames[bus][sensor], name, MAX_SENSOR_NAME_LEN - 1);
    settings.sensorNames[bus][sensor][MAX_SENSOR_NAME_LEN - 1] = 0;
    return true;
}

bool authenticateUser(const char *user, const char *pass) {
    if (!user || !pass) return false;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++) {
        if (!settings.users[i].active) continue;
        if (strcmp(settings.users[i].username, user) == 0 &&
            strcmp(settings.users[i].password, pass) == 0) return true;
    }
    return false;
}

int findUser(const char *user) {
    if (!user) return -1;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++)
        if (settings.users[i].active && strcmp(settings.users[i].username, user) == 0) return i;
    return -1;
}

bool addUser(const char *user, const char *pass) {
    if (!user || !pass || !user[0] || !pass[0]) return false;
    if (findUser(user) >= 0 || settings.userCount >= MAX_USERS) return false;
    int idx = settings.userCount;
    strncpy(settings.users[idx].username, user, MAX_USERNAME_LEN - 1);
    strncpy(settings.users[idx].password, pass, MAX_PASSWORD_LEN - 1);
    settings.users[idx].username[MAX_USERNAME_LEN - 1] = 0;
    settings.users[idx].password[MAX_PASSWORD_LEN - 1] = 0;
    settings.users[idx].active = true;
    settings.userCount++;
    return true;
}

bool deleteUser(const char *user) {
    int idx = findUser(user);
    if (idx < 0) return false;
    int active = 0;
    for (int i = 0; i < settings.userCount; i++) if (settings.users[i].active) active++;
    if (active <= 1) return false;
    for (int i = idx; i < settings.userCount - 1; i++) settings.users[i] = settings.users[i + 1];
    settings.userCount--;
    memset(&settings.users[settings.userCount], 0, sizeof(UserAccount));
    return true;
}

bool changePassword(const char *user, const char *newPass) {
    int idx = findUser(user);
    if (idx < 0 || !newPass || !newPass[0]) return false;
    strncpy(settings.users[idx].password, newPass, MAX_PASSWORD_LEN - 1);
    settings.users[idx].password[MAX_PASSWORD_LEN - 1] = 0;
    return true;
}
