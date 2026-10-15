#include "webserver.h"
#include "config.h"
#include "display.h"
#include "settings.h"
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <ETH.h>

static NetworkServer server(80);
extern bool networkInitialized;

// ========== SESSION ==========
static char sessionToken[17] = {0};
static unsigned long sessionExpire = 0;
static char sessionUser[MAX_USERNAME_LEN] = {0};

static void clearSession() {
    sessionToken[0] = 0;
    sessionUser[0] = 0;
    sessionExpire = 0;
}

static void createSession(const char *user) {
    // Простой токен из millis + random
    snprintf(sessionToken, sizeof(sessionToken), "%08lX%08lX",
             (unsigned long)millis(), (unsigned long)esp_random());
    strncpy(sessionUser, user, MAX_USERNAME_LEN - 1);
    sessionUser[MAX_USERNAME_LEN - 1] = 0;
    sessionExpire = millis() + SESSION_TIMEOUT_MS;
}

static bool isLoggedIn() {
    if (!sessionToken[0]) return false;
    if (millis() > sessionExpire) {
        clearSession();
        return false;
    }
    // Продлеваем сессию при активности
    sessionExpire = millis() + SESSION_TIMEOUT_MS;
    return true;
}

// ========== HTTP HELPERS ==========
static void sendHeaders(NetworkClient &client, const __FlashStringHelper *contentType,
                        bool noCache = true, const char *extraHeader = nullptr) {
    if (!client.connected()) return;
    client.println(F("HTTP/1.1 200 OK"));
    client.print(F("Content-Type: "));
    client.println(contentType);
    if (noCache)
        client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
    if (extraHeader)
        client.println(extraHeader);
    client.println(F("Connection: close"));
    client.println();
}

static void sendRedirect(NetworkClient &client, const char *location,
                         const char *setCookie = nullptr) {
    client.println(F("HTTP/1.1 302 Found"));
    client.print(F("Location: "));
    client.println(location);
    if (setCookie) {
        client.print(F("Set-Cookie: "));
        client.println(setCookie);
    }
    client.println(F("Connection: close"));
    client.println();
}

static void send404(NetworkClient &client) {
    client.println(F("HTTP/1.1 404 Not Found"));
    client.println(F("Content-Type: text/plain"));
    client.println(F("Connection: close"));
    client.println();
    client.println(F("404 Not Found"));
}

// Читаем первую строку + заголовки, ищем Cookie и Content-Length
struct HttpRequest {
    String method;
    String path;
    String cookie;
    int contentLength;
    String body;
};

static bool parseRequest(NetworkClient &client, HttpRequest &req) {
    req.method = "";
    req.path = "/";
    req.cookie = "";
    req.contentLength = 0;
    req.body = "";

    // Request line
    String line;
    line.reserve(128);
    unsigned long start = millis();
    while (client.connected() && (millis() - start) < 2000) {
        if (!client.available()) { delay(1); continue; }
        char c = client.read();
        if (c == '\n') break;
        if (c != '\r' && line.length() < 200) line += c;
    }
    if (line.length() == 0) return false;

    int sp1 = line.indexOf(' ');
    if (sp1 <= 0) return false;
    int sp2 = line.indexOf(' ', sp1 + 1);
    req.method = line.substring(0, sp1);
    req.method.toUpperCase();
    if (sp2 > sp1)
        req.path = line.substring(sp1 + 1, sp2);
    else
        req.path = line.substring(sp1 + 1);
    int q = req.path.indexOf('?');
    if (q >= 0) req.path = req.path.substring(0, q);

    // Headers
    while (client.connected() && (millis() - start) < 3000) {
        if (!client.available()) { delay(1); continue; }
        String h;
        h.reserve(128);
        while (client.available()) {
            char c = client.read();
            if (c == '\n') break;
            if (c != '\r' && h.length() < 250) h += c;
        }
        if (h.length() == 0) break;  // пустая строка = конец заголовков

        if (h.startsWith("Cookie:") || h.startsWith("cookie:")) {
            req.cookie = h.substring(7);
            req.cookie.trim();
        } else if (h.startsWith("Content-Length:") || h.startsWith("content-length:")) {
            req.contentLength = h.substring(15).toInt();
        }
    }

    // Body (для POST)
    if (req.contentLength > 0 && req.contentLength < 2048) {
        req.body.reserve(req.contentLength + 1);
        unsigned long bodyStart = millis();
        while ((int)req.body.length() < req.contentLength &&
               client.connected() && (millis() - bodyStart) < 3000) {
            if (client.available()) {
                req.body += (char)client.read();
            } else {
                delay(1);
            }
        }
    }
    return true;
}

static bool checkAuthCookie(const String &cookie) {
    if (!isLoggedIn()) return false;
    // Ищем session=TOKEN
    int idx = cookie.indexOf("session=");
    if (idx < 0) return false;
    String tok = cookie.substring(idx + 8);
    int end = tok.indexOf(';');
    if (end >= 0) tok = tok.substring(0, end);
    tok.trim();
    return tok.equals(sessionToken);
}

// URL-decode простого application/x-www-form-urlencoded
static String urlDecode(const String &s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.length()) {
            char hex[3] = {s[i+1], s[i+2], 0};
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

static String getFormValue(const String &body, const char *key) {
    String search = String(key) + "=";
    int idx = body.indexOf(search);
    if (idx < 0) return "";
    // Убедимся что это начало параметра
    if (idx > 0 && body[idx - 1] != '&') {
        // может быть не первое вхождение — ищем все
        while (idx >= 0) {
            if (idx == 0 || body[idx - 1] == '&') break;
            idx = body.indexOf(search, idx + 1);
        }
        if (idx < 0) return "";
    }
    int start = idx + search.length();
    int end = body.indexOf('&', start);
    String val = (end < 0) ? body.substring(start) : body.substring(start, end);
    return urlDecode(val);
}

// ========== JSON ==========
static void sendJsonSensors(NetworkClient &client) {
    client.print(F("{\"sensors\":["));
    bool first = true;
    for (int b = 0; b < BUS_COUNT; b++) {
        if (!client.connected()) return;
        for (int i = 0; i < buses[b].count; i++) {
            if (!isSensorVisible(buses[b], i)) continue;
            if (!first) client.print(',');
            first = false;
            client.print(F("{\"bus\":"));
            client.print(b + 1);
            client.print(F(",\"id\":"));
            client.print(i + 1);
            client.print(F(",\"name\":\""));
            client.print(getSensorName(b, i));
            client.print(F("\",\"temp\":"));
            client.print(buses[b].temperatures[i], 1);
            client.print('}');
        }
    }
    client.print(F("]}"));
}

// ========== CSS (общий) ==========
static void sendCss(NetworkClient &client) {
    client.print(F(
        "body{background:#0a0e17;color:#eee;font-family:Arial,sans-serif;padding:16px;margin:0}"
        "h1,h2{color:#4ecdc4}a{color:#4ecdc4}"
        ".card{background:#1a2238;padding:16px;margin:10px 0;border-radius:10px}"
        "input,select{background:#0a0e17;color:#eee;border:1px solid #334;padding:8px;border-radius:5px;width:100%;box-sizing:border-box;margin:4px 0}"
        "label{font-size:13px;color:#8899bb}"
        ".btn{background:#4ecdc4;color:#0a0e17;padding:10px 18px;border:none;border-radius:5px;"
        "cursor:pointer;text-decoration:none;display:inline-block;margin:4px 2px;font-weight:bold}"
        ".btn-d{background:#ff6b6b}.btn-s{background:#8899bb}"
        ".s{background:#1a2238;padding:12px;margin:6px;border-radius:10px;display:inline-block;min-width:110px;text-align:center}"
        ".t{font-size:28px;font-weight:bold;color:#4ecdc4}.e{color:#ff6b6b}"
        "table{width:100%;border-collapse:collapse}td,th{padding:6px;text-align:left;border-bottom:1px solid #334}"
        ".msg{background:#2a3a1a;color:#8f8;padding:10px;border-radius:6px;margin:10px 0}"
        ".err{background:#3a1a1a;color:#f88}"
        ".nav{margin-bottom:16px}.nav a{margin-right:12px}"
    ));
}

// ========== PAGES ==========
static void handleLoginPage(NetworkClient &client, const char *msg = nullptr) {
    sendHeaders(client, F("text/html; charset=utf-8"));
    client.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    client.println(F("<title>Login</title><style>"));
    sendCss(client);
    client.println(F("</style></head><body>"));
    client.println(F("<h1>🔐 Вход</h1>"));
    if (msg) {
        client.print(F("<div class='err card'>"));
        client.print(msg);
        client.println(F("</div>"));
    }
    client.println(F("<div class='card'><form method='POST' action='/login'>"));
    client.println(F("<label>Логин</label><input name='user' required autofocus>"));
    client.println(F("<label>Пароль</label><input name='pass' type='password' required>"));
    client.println(F("<br><button class='btn' type='submit'>Войти</button>"));
    client.println(F("</form></div></body></html>"));
}

static void handleRoot(NetworkClient &client) {
    sendHeaders(client, F("text/html; charset=utf-8"));
    client.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"));
    client.println(F("<meta http-equiv='refresh' content='15'>"));
    client.println(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
    client.println(F("<title>Temp Monitor</title><style>"));
    sendCss(client);
    client.println(F("</style></head><body>"));
    client.println(F("<div class='nav'>"));
    client.println(F("<a href='/'>📊 Монитор</a>"));
    client.println(F("<a href='/settings'>⚙️ Настройки</a>"));
    client.println(F("<a href='/logout'>🚪 Выход</a>"));
    client.println(F("</div>"));
    client.println(F("<h1>🌡️ Температурный мониторинг</h1>"));
    client.print(F("<div class='card'>📍 IP: "));
    client.print(ETH.localIP());
    client.print(F(" | Датчиков: "));
    client.print(totalSensors);
    client.print(F(" | Uptime: "));
    client.print(millis() / 1000);
    client.println(F(" с</div>"));

    for (int b = 0; b < BUS_COUNT; b++) {
        if (!client.connected()) return;
        client.print(F("<h2>🔌 Bus "));
        client.print(b + 1);
        client.println(F("</h2>"));
        for (int i = 0; i < buses[b].count; i++) {
            client.print(F("<div class='s'><b>"));
            client.print(getSensorName(b, i));
            client.print(F("</b><br>"));
            if (isSensorVisible(buses[b], i)) {
                client.print(F("<span class='t'>"));
                client.print(buses[b].temperatures[i], 1);
                client.println(F("°C</span>"));
            } else {
                client.print(F("<span class='e'>---</span>"));
            }
            client.println(F("</div>"));
        }
    }
    client.println(F("<div class='card'><a href='/temp'>JSON API</a></div>"));
    client.println(F("</body></html>"));
}

static void handleSettingsPage(NetworkClient &client, const char *msg = nullptr) {
    sendHeaders(client, F("text/html; charset=utf-8"));
    client.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"));
    client.println(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
    client.println(F("<title>Настройки</title><style>"));
    sendCss(client);
    client.println(F("</style></head><body>"));
    client.println(F("<div class='nav'>"));
    client.println(F("<a href='/'>📊 Монитор</a>"));
    client.println(F("<a href='/settings'>⚙️ Настройки</a>"));
    client.println(F("<a href='/logout'>🚪 Выход</a>"));
    client.println(F("</div>"));
    client.println(F("<h1>⚙️ Настройки</h1>"));
    if (msg) {
        client.print(F("<div class='msg'>"));
        client.print(msg);
        client.println(F("</div>"));
    }

    // --- Network ---
    client.println(F("<div class='card'><h2>🌐 Сеть</h2>"));
    client.println(F("<form method='POST' action='/settings'>"));
    client.println(F("<input type='hidden' name='section' value='net'>"));
    client.print(F("<label><input type='checkbox' name='dhcp' value='1'"));
    if (settings.useDHCP) client.print(F(" checked"));
    client.println(F("> Использовать DHCP (при неудаче — static)</label><br><br>"));
    client.println(F("<label>IP</label><input name='ip' value='"));
    client.print(settingsIP());
    client.println(F("'>"));
    client.println(F("<label>Шлюз</label><input name='gw' value='"));
    client.print(settingsGW());
    client.println(F("'>"));
    client.println(F("<label>Маска</label><input name='mask' value='"));
    client.print(settingsMask());
    client.println(F("'>"));
    client.println(F("<label>DNS</label><input name='dns' value='"));
    client.print(settingsDNS());
    client.println(F("'>"));
    client.println(F("<button class='btn' type='submit'>💾 Сохранить сеть</button>"));
    client.println(F("</form></div>"));

    // --- SNMP ---
    client.println(F("<div class='card'><h2>📡 SNMP</h2>"));
    client.println(F("<form method='POST' action='/settings'>"));
    client.println(F("<input type='hidden' name='section' value='snmp'>"));
    client.print(F("<label><input type='checkbox' name='snmp_en' value='1'"));
    if (settings.snmpEnabled) client.print(F(" checked"));
    client.println(F("> SNMP включён</label><br><br>"));
    client.println(F("<label>Community Read</label><input name='snmp_r' value='"));
    client.print(settings.snmpCommunityRead);
    client.println(F("'>"));
    client.println(F("<label>Community Write</label><input name='snmp_w' value='"));
    client.print(settings.snmpCommunityWrite);
    client.println(F("'>"));
    client.println(F("<label>OID Prefix</label><input name='snmp_oid' value='"));
    client.print(settings.snmpOidPrefix);
    client.println(F("'>"));
    client.println(F("<button class='btn' type='submit'>💾 Сохранить SNMP</button>"));
    client.println(F("</form></div>"));

    // --- Sensor names ---
    client.println(F("<div class='card'><h2>🌡️ Имена датчиков</h2>"));
    client.println(F("<form method='POST' action='/settings'>"));
    client.println(F("<input type='hidden' name='section' value='sensors'>"));
    for (int b = 0; b < BUS_COUNT; b++) {
        for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
            // Показываем только существующие или все слоты
            client.print(F("<label>Bus "));
            client.print(b + 1);
            client.print(F(" Sensor "));
            client.print(i + 1);
            client.println(F("</label>"));
            client.print(F("<input name='sn_"));
            client.print(b);
            client.print('_');
            client.print(i);
            client.print(F("' value='"));
            client.print(getSensorName(b, i));
            client.println(F("'>"));
        }
    }
    client.println(F("<button class='btn' type='submit'>💾 Сохранить имена</button>"));
    client.println(F("</form></div>"));

    // --- Users ---
    client.println(F("<div class='card'><h2>👤 Пользователи</h2>"));
    client.println(F("<table><tr><th>Логин</th><th>Действие</th></tr>"));
    for (int i = 0; i < settings.userCount; i++) {
        if (!settings.users[i].active) continue;
        client.print(F("<tr><td>"));
        client.print(settings.users[i].username);
        client.print(F("</td><td>"));
        // Удаление
        client.print(F("<form method='POST' action='/settings' style='display:inline'>"));
        client.print(F("<input type='hidden' name='section' value='deluser'>"));
        client.print(F("<input type='hidden' name='deluser' value='"));
        client.print(settings.users[i].username);
        client.print(F("'><button class='btn btn-d' type='submit'>Удалить</button></form>"));
        client.println(F("</td></tr>"));
    }
    client.println(F("</table><br>"));
    // Добавить пользователя
    client.println(F("<form method='POST' action='/settings'>"));
    client.println(F("<input type='hidden' name='section' value='adduser'>"));
    client.println(F("<label>Новый логин</label><input name='newuser' maxlength='15'>"));
    client.println(F("<label>Пароль</label><input name='newpass' type='password' maxlength='23'>"));
    client.println(F("<button class='btn' type='submit'>➕ Добавить</button>"));
    client.println(F("</form><br>"));
    // Смена пароля
    client.println(F("<form method='POST' action='/settings'>"));
    client.println(F("<input type='hidden' name='section' value='chpass'>"));
    client.println(F("<label>Логин</label><input name='chuser' value='"));
    client.print(sessionUser);
    client.println(F("'>"));
    client.println(F("<label>Новый пароль</label><input name='chpass' type='password' maxlength='23'>"));
    client.println(F("<button class='btn' type='submit'>🔑 Сменить пароль</button>"));
    client.println(F("</form></div>"));

    client.println(F("</body></html>"));
}

static void handleSettingsPost(NetworkClient &client, const String &body) {
    String section = getFormValue(body, "section");
    const char *msg = "Сохранено";

    if (section == "net") {
        settings.useDHCP = (getFormValue(body, "dhcp") == "1");
        IPAddress ip, gw, mask, dns;
        ip.fromString(getFormValue(body, "ip"));
        gw.fromString(getFormValue(body, "gw"));
        mask.fromString(getFormValue(body, "mask"));
        dns.fromString(getFormValue(body, "dns"));
        setSettingsIP(ip, gw, mask, dns);
        saveSettings();
        msg = "Сеть сохранена. Применение — после перезагрузки или смены линка.";
    } else if (section == "snmp") {
        settings.snmpEnabled = (getFormValue(body, "snmp_en") == "1");
        String r = getFormValue(body, "snmp_r");
        String w = getFormValue(body, "snmp_w");
        String o = getFormValue(body, "snmp_oid");
        strncpy(settings.snmpCommunityRead,  r.c_str(), MAX_SNMP_COMM_LEN - 1);
        strncpy(settings.snmpCommunityWrite, w.c_str(), MAX_SNMP_COMM_LEN - 1);
        strncpy(settings.snmpOidPrefix,      o.c_str(), MAX_SNMP_OID_LEN - 1);
        saveSettings();
        msg = "SNMP сохранён. Переинициализация при следующем подключении сети.";
        needdeInitSNMP = true;
        needInitSNMP = true;
    } else if (section == "sensors") {
        for (int b = 0; b < BUS_COUNT; b++) {
            for (int i = 0; i < MAX_SENSORS_PER_BUS; i++) {
                char key[12];
                snprintf(key, sizeof(key), "sn_%d_%d", b, i);
                String name = getFormValue(body, key);
                if (name.length() > 0)
                    setSensorName(b, i, name.c_str());
            }
        }
        saveSettings();
        msg = "Имена датчиков сохранены";
    } else if (section == "adduser") {
        String u = getFormValue(body, "newuser");
        String p = getFormValue(body, "newpass");
        if (addUser(u.c_str(), p.c_str())) {
            saveSettings();
            msg = "Пользователь добавлен";
        } else {
            msg = "Ошибка: пользователь уже есть или лимит";
        }
    } else if (section == "deluser") {
        String u = getFormValue(body, "deluser");
        if (deleteUser(u.c_str())) {
            saveSettings();
            msg = "Пользователь удалён";
        } else {
            msg = "Нельзя удалить (последний или не найден)";
        }
    } else if (section == "chpass") {
        String u = getFormValue(body, "chuser");
        String p = getFormValue(body, "chpass");
        if (changePassword(u.c_str(), p.c_str())) {
            saveSettings();
            msg = "Пароль изменён";
        } else {
            msg = "Ошибка смены пароля";
        }
    }

    handleSettingsPage(client, msg);
}

static void handleTempJson(NetworkClient &client) {
    sendHeaders(client, F("application/json"), true, "Access-Control-Allow-Origin: *");
    sendJsonSensors(client);
}

// ========== PUBLIC API ==========
static bool webServerStarted = false;

void initWebServer() {
    if (webServerStarted) return;
    server.begin();
    webServerStarted = true;
    Serial.println(F("Web server started on port 80"));
}

void deinitWebServer() {
    if (!webServerStarted) return;
    server.end();
    webServerStarted = false;
    Serial.println(F("Web server stopped"));
}

void handleWebClient() {
    NetworkClient client = server.accept();
    if (!client) return;
    client.setTimeout(2000);

    HttpRequest req;
    if (!parseRequest(client, req)) {
        client.stop();
        return;
    }

    bool authed = checkAuthCookie(req.cookie);

    // --- Routing ---
    if (req.path == "/temp") {
        // JSON доступен без авторизации (для мониторинга)
        handleTempJson(client);
    } else if (req.path == "/login") {
        if (req.method == "POST") {
            String user = getFormValue(req.body, "user");
            String pass = getFormValue(req.body, "pass");
            if (authenticateUser(user.c_str(), pass.c_str())) {
                createSession(user.c_str());
                char cookie[64];
                snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly", sessionToken);
                sendRedirect(client, "/", cookie);
            } else {
                handleLoginPage(client, "Неверный логин или пароль");
            }
        } else {
            if (authed) sendRedirect(client, "/");
            else handleLoginPage(client);
        }
    } else if (req.path == "/logout") {
        clearSession();
        sendRedirect(client, "/login", "session=; Path=/; Max-Age=0");
    } else if (!authed) {
        sendRedirect(client, "/login");
    } else if (req.path == "/" || req.path == "/index.html") {
        handleRoot(client);
    } else if (req.path == "/settings") {
        if (req.method == "POST")
            handleSettingsPost(client, req.body);
        else
            handleSettingsPage(client);
    } else {
        send404(client);
    }

    client.stop();
}
