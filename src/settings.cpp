#include "settings.h"
#include <Preferences.h>
#include <string.h>

AppSettings settings;
static Preferences prefs;

static void setDefaultIP() {
    uint8_t defIP[]   = {MYIPADDR};
    uint8_t defGW[]   = {MYGW};
    uint8_t defMask[] = {MYIPMASK};
    uint8_t defDNS[]  = {MYDNS};
    memcpy(settings.ip,   defIP,   4);
    memcpy(settings.gw,   defGW,   4);
    memcpy(settings.mask, defMask, 4);
    memcpy(settings.dns,  defDNS,  4);
}

void resetSettingsToDefault() {
    memset(&settings, 0, sizeof(settings));

    settings.useDHCP = true;
    setDefaultIP();

    settings.snmpEnabled = true;
    strncpy(settings.snmpCommunityRead,  SNMP_COMMUNITY_READ,  MAX_SNMP_COMM_LEN - 1);
    strncpy(settings.snmpCommunityWrite, SNMP_COMMUNITY_WRITE, MAX_SNMP_COMM_LEN - 1);
    strncpy(settings.snmpOidPrefix,      SNMP_OID_PREFIX,      MAX_SNMP_OID_LEN - 1);

    // Имена датчиков по умолчанию: B1S1, B1S2, ...
    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            snprintf(settings.sensorNames[b][i], MAX_SENSOR_NAME_LEN,
                     "B%dS%d", b + 1, i + 1);
        }
    }

    // Пользователь admin / admin
    settings.userCount = 1;
    strncpy(settings.users[0].username, "admin", MAX_USERNAME_LEN - 1);
    strncpy(settings.users[0].password, "admin", MAX_PASSWORD_LEN - 1);
    settings.users[0].active = true;
}

void loadSettings() {
    resetSettingsToDefault();   // сначала дефолты

    if (!prefs.begin("tempmon", true)) {  // read-only
        Serial.println(F("Settings: NVS open RO failed, using defaults"));
        return;
    }

    size_t len = prefs.getBytesLength("cfg");
    if (len == sizeof(AppSettings)) {
        prefs.getBytes("cfg", &settings, sizeof(AppSettings));
        Serial.println(F("Settings: loaded from NVS"));
    } else {
        Serial.println(F("Settings: no valid blob, using defaults"));
    }
    prefs.end();

    // Защита: хотя бы один активный пользователь
    bool anyUser = false;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++) {
        if (settings.users[i].active && settings.users[i].username[0]) {
            anyUser = true;
            break;
        }
    }
    if (!anyUser) {
        Serial.println(F("Settings: no users → restore admin/admin"));
        settings.userCount = 1;
        strncpy(settings.users[0].username, "admin", MAX_USERNAME_LEN - 1);
        strncpy(settings.users[0].password, "admin", MAX_PASSWORD_LEN - 1);
        settings.users[0].active = true;
    }
}

void saveSettings() {
    if (!prefs.begin("tempmon", false)) {
        Serial.println(F("Settings: NVS open RW failed"));
        return;
    }
    prefs.putBytes("cfg", &settings, sizeof(AppSettings));
    prefs.end();
    Serial.println(F("Settings: saved to NVS"));
}

IPAddress settingsIP()   { return IPAddress(settings.ip[0], settings.ip[1], settings.ip[2], settings.ip[3]); }
IPAddress settingsGW()   { return IPAddress(settings.gw[0], settings.gw[1], settings.gw[2], settings.gw[3]); }
IPAddress settingsMask() { return IPAddress(settings.mask[0], settings.mask[1], settings.mask[2], settings.mask[3]); }
IPAddress settingsDNS()  { return IPAddress(settings.dns[0], settings.dns[1], settings.dns[2], settings.dns[3]); }

void setSettingsIP(const IPAddress &ip, const IPAddress &gw,
                   const IPAddress &mask, const IPAddress &dns) {
    for (int i = 0; i < 4; i++) {
        settings.ip[i]   = ip[i];
        settings.gw[i]   = gw[i];
        settings.mask[i] = mask[i];
        settings.dns[i]  = dns[i];
    }
}

const char* getSensorName(int bus, int sensor) {
    if (bus < 0 || bus >= 4 || sensor < 0 || sensor >= MAX_SENSORS_PER_BUS)
        return "???";
    if (settings.sensorNames[bus][sensor][0] == '\0')
        return "---";
    return settings.sensorNames[bus][sensor];
}

bool setSensorName(int bus, int sensor, const char *name) {
    if (bus < 0 || bus >= 4 || sensor < 0 || sensor >= MAX_SENSORS_PER_BUS || !name)
        return false;
    strncpy(settings.sensorNames[bus][sensor], name, MAX_SENSOR_NAME_LEN - 1);
    settings.sensorNames[bus][sensor][MAX_SENSOR_NAME_LEN - 1] = '\0';
    return true;
}

bool authenticateUser(const char *user, const char *pass) {
    if (!user || !pass) return false;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++) {
        if (!settings.users[i].active) continue;
        if (strcmp(settings.users[i].username, user) == 0 &&
            strcmp(settings.users[i].password, pass) == 0) {
            return true;
        }
    }
    return false;
}

int findUser(const char *user) {
    if (!user) return -1;
    for (int i = 0; i < settings.userCount && i < MAX_USERS; i++) {
        if (settings.users[i].active &&
            strcmp(settings.users[i].username, user) == 0) {
            return i;
        }
    }
    return -1;
}

bool addUser(const char *user, const char *pass) {
    if (!user || !pass || !user[0] || !pass[0]) return false;
    if (findUser(user) >= 0) return false;          // уже есть
    if (settings.userCount >= MAX_USERS) return false;

    int idx = settings.userCount;
    strncpy(settings.users[idx].username, user, MAX_USERNAME_LEN - 1);
    strncpy(settings.users[idx].password, pass, MAX_PASSWORD_LEN - 1);
    settings.users[idx].username[MAX_USERNAME_LEN - 1] = '\0';
    settings.users[idx].password[MAX_PASSWORD_LEN - 1] = '\0';
    settings.users[idx].active = true;
    settings.userCount++;
    return true;
}

bool deleteUser(const char *user) {
    int idx = findUser(user);
    if (idx < 0) return false;
    // Нельзя удалить последнего пользователя
    int activeCount = 0;
    for (int i = 0; i < settings.userCount; i++)
        if (settings.users[i].active) activeCount++;
    if (activeCount <= 1) return false;

    // Сдвигаем массив
    for (int i = idx; i < settings.userCount - 1; i++) {
        settings.users[i] = settings.users[i + 1];
    }
    settings.userCount--;
    memset(&settings.users[settings.userCount], 0, sizeof(UserAccount));
    return true;
}

bool changePassword(const char *user, const char *newPass) {
    int idx = findUser(user);
    if (idx < 0 || !newPass || !newPass[0]) return false;
    strncpy(settings.users[idx].password, newPass, MAX_PASSWORD_LEN - 1);
    settings.users[idx].password[MAX_PASSWORD_LEN - 1] = '\0';
    return true;
}
