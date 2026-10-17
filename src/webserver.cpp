#include "webserver.h"
#include "config.h"
#include "display.h"
#include "settings.h"
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <ETH.h>

static NetworkServer server(80);
extern bool networkInitialized;

static char sessionToken[17] = {0};
static unsigned long sessionExpire = 0;
static char sessionUser[MAX_USERNAME_LEN] = {0};

static void clearSession() { sessionToken[0]=0; sessionUser[0]=0; sessionExpire=0; }
static void createSession(const char *user) {
    snprintf(sessionToken, sizeof(sessionToken), "%08lX%08lX", (unsigned long)millis(), (unsigned long)esp_random());
    strncpy(sessionUser, user, MAX_USERNAME_LEN-1); sessionUser[MAX_USERNAME_LEN-1]=0;
    sessionExpire = millis() + SESSION_TIMEOUT_MS;
}
static bool isLoggedIn() {
    if (!sessionToken[0]) return false;
    if (millis() > sessionExpire) { clearSession(); return false; }
    sessionExpire = millis() + SESSION_TIMEOUT_MS; return true;
}

static void sendHeaders(NetworkClient &c, const __FlashStringHelper *ct, const char *extra=nullptr) {
    if (!c.connected()) return;
    c.println(F("HTTP/1.1 200 OK"));
    c.print(F("Content-Type: ")); c.println(ct);
    c.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
    if (extra) c.println(extra);
    c.println(F("Connection: close")); c.println();
}
static void sendRedirect(NetworkClient &c, const char *loc, const char *cookie=nullptr) {
    c.println(F("HTTP/1.1 302 Found"));
    c.print(F("Location: ")); c.println(loc);
    if (cookie) { c.print(F("Set-Cookie: ")); c.println(cookie); }
    c.println(F("Connection: close")); c.println();
}
static void send404(NetworkClient &c) {
    c.println(F("HTTP/1.1 404 Not Found"));
    c.println(F("Content-Type: text/plain"));
    c.println(F("Connection: close")); c.println();
    c.println(F("404"));
}

struct HttpRequest { String method, path, cookie, body; int contentLength; };

static bool parseRequest(NetworkClient &client, HttpRequest &req) {
    req.method=""; req.path="/"; req.cookie=""; req.body=""; req.contentLength=0;
    String line; line.reserve(128);
    unsigned long start = millis();
    while (client.connected() && (millis()-start)<2000) {
        if (!client.available()) { delay(1); continue; }
        char ch = client.read();
        if (ch=='\n') break;
        if (ch!='\r' && line.length()<200) line += ch;
    }
    if (!line.length()) return false;
    int sp1 = line.indexOf(' '); if (sp1<=0) return false;
    int sp2 = line.indexOf(' ', sp1+1);
    req.method = line.substring(0, sp1); req.method.toUpperCase();
    req.path = (sp2>sp1) ? line.substring(sp1+1, sp2) : line.substring(sp1+1);
    int q = req.path.indexOf('?'); if (q>=0) req.path = req.path.substring(0,q);
    while (client.connected() && (millis()-start)<3000) {
        if (!client.available()) { delay(1); continue; }
        String h; h.reserve(128);
        while (client.available()) {
            char ch = client.read();
            if (ch=='\n') break;
            if (ch!='\r' && h.length()<250) h += ch;
        }
        if (!h.length()) break;
        if (h.startsWith("Cookie:") || h.startsWith("cookie:")) { req.cookie = h.substring(7); req.cookie.trim(); }
        else if (h.startsWith("Content-Length:") || h.startsWith("content-length:")) req.contentLength = h.substring(15).toInt();
    }
    if (req.contentLength>0 && req.contentLength<4096) {
        req.body.reserve(req.contentLength+1);
        unsigned long bs = millis();
        while ((int)req.body.length()<req.contentLength && client.connected() && (millis()-bs)<3000) {
            if (client.available()) req.body += (char)client.read(); else delay(1);
        }
    }
    return true;
}

static bool checkAuthCookie(const String &cookie) {
    if (!isLoggedIn()) return false;
    int idx = cookie.indexOf("session="); if (idx<0) return false;
    String tok = cookie.substring(idx+8);
    int end = tok.indexOf(';'); if (end>=0) tok = tok.substring(0,end);
    tok.trim(); return tok.equals(sessionToken);
}

static String urlDecode(const String &s) {
    String out; out.reserve(s.length());
    for (size_t i=0; i<s.length(); i++) {
        if (s[i]=='+') out += ' ';
        else if (s[i]=='%' && i+2<s.length()) {
            char hex[3]={s[i+1],s[i+2],0}; out += (char)strtol(hex,nullptr,16); i+=2;
        } else out += s[i];
    }
    return out;
}
static String getFormValue(const String &body, const char *key) {
    String search = String(key) + "=";
    int idx = body.indexOf(search);
    while (idx >= 0) {
        if (idx==0 || body[idx-1]=='&') break;
        idx = body.indexOf(search, idx+1);
    }
    if (idx<0) return "";
    int start = idx + search.length();
    int end = body.indexOf('&', start);
    return urlDecode((end<0) ? body.substring(start) : body.substring(start, end));
}

static void sendCss(NetworkClient &c) {
    c.print(F(
        "body{background:#0a0e17;color:#eee;font-family:Arial,sans-serif;padding:16px;margin:0}"
        "h1,h2{color:#4ecdc4}a{color:#4ecdc4}"
        ".card{background:#1a2238;padding:14px;margin:10px 0;border-radius:10px}"
        "input,select{background:#0a0e17;color:#eee;border:1px solid #334;padding:7px;border-radius:5px;width:100%;box-sizing:border-box;margin:3px 0}"
        "label{font-size:12px;color:#8899bb}"
        ".btn{background:#4ecdc4;color:#0a0e17;padding:9px 16px;border:none;border-radius:5px;cursor:pointer;text-decoration:none;display:inline-block;margin:3px;font-weight:bold}"
        ".btn-d{background:#ff6b6b}.s{background:#1a2238;padding:10px;margin:5px;border-radius:10px;display:inline-block;min-width:100px;text-align:center}"
        ".t{font-size:26px;font-weight:bold;color:#4ecdc4}.e{color:#ff6b6b}"
        "table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:5px;text-align:left;border-bottom:1px solid #334}"
        ".msg{background:#2a3a1a;color:#8f8;padding:8px;border-radius:6px;margin:8px 0}"
        ".err{background:#3a1a1a;color:#f88}.nav a{margin-right:10px}"
        "input[type=checkbox]{width:auto}"
    ));
}

static void sendJsonSensors(NetworkClient &c) {
    c.print(F("{\"sensors\":["));
    bool first=true;
    for (int b=0;b<BUS_COUNT;b++) for (int i=0;i<buses[b].count;i++) {
        if (!isSensorVisible(buses[b],i)) continue;
        if (!first) c.print(','); first=false;
        c.print(F("{\"bus\":")); c.print(b+1);
        c.print(F(",\"id\":")); c.print(i+1);
        c.print(F(",\"name\":\"")); c.print(getSensorName(b,i));
        c.print(F("\",\"temp\":")); c.print(buses[b].temperatures[i],1); c.print('}');
    }
    c.print(F("]}"));
}

static void handleLoginPage(NetworkClient &c, const char *msg=nullptr) {
    sendHeaders(c, F("text/html; charset=utf-8"));
    c.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    c.println(F("<title>Login</title><style>")); sendCss(c); c.println(F("</style></head><body>"));
    c.println(F("<h1>Login</h1>"));
    if (msg) { c.print(F("<div class='err card'>")); c.print(msg); c.println(F("</div>")); }
    c.println(F("<div class='card'><form method='POST' action='/login'>"));
    c.println(F("<label>User</label><input name='user' required autofocus>"));
    c.println(F("<label>Password</label><input name='pass' type='password' required>"));
    c.println(F("<br><button class='btn' type='submit'>Login</button></form></div></body></html>"));
}

static void handleRoot(NetworkClient &c) {
    sendHeaders(c, F("text/html; charset=utf-8"));
    c.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='15'>"));
    c.println(F("<meta name='viewport' content='width=device-width,initial-scale=1'><title>Temp</title><style>"));
    sendCss(c); c.println(F("</style></head><body>"));
    c.println(F("<div class='nav'><a href='/'>Monitor</a> <a href='/settings'>Settings</a> <a href='/logout'>Logout</a></div>"));
    c.println(F("<h1>Temperatures</h1>"));
    c.print(F("<div class='card'>IP: ")); c.print(ETH.localIP());
    c.print(F(" | Sensors: ")); c.print(totalSensors);
    c.print(F(" | Up: ")); c.print(millis()/1000); c.println(F("s</div>"));
    for (int b=0;b<BUS_COUNT;b++) {
        if (!c.connected()) return;
        c.print(F("<h2>Bus ")); c.print(b+1); c.println(F("</h2>"));
        for (int i=0;i<buses[b].count;i++) {
            c.print(F("<div class='s'><b>")); c.print(getSensorName(b,i)); c.print(F("</b><br>"));
            if (isSensorVisible(buses[b],i)) { c.print(F("<span class='t'>")); c.print(buses[b].temperatures[i],1); c.println(F("C</span>")); }
            else c.print(F("<span class='e'>---</span>"));
            c.println(F("</div>"));
        }
    }
    c.println(F("<div class='card'><a href='/temp'>JSON</a></div></body></html>"));
}

static void handleSettingsPage(NetworkClient &c, const char *msg=nullptr) {
    sendHeaders(c, F("text/html; charset=utf-8"));
    c.println(F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
    c.println(F("<title>Settings</title><style>")); sendCss(c); c.println(F("</style></head><body>"));
    c.println(F("<div class='nav'><a href='/'>Monitor</a> <a href='/settings'>Settings</a> <a href='/logout'>Logout</a></div>"));
    c.println(F("<h1>Settings</h1>"));
    if (msg) { c.print(F("<div class='msg'>")); c.print(msg); c.println(F("</div>")); }

    // Network
    c.println(F("<div class='card'><h2>Network</h2><form method='POST' action='/settings'>"));
    c.println(F("<input type='hidden' name='section' value='net'>"));
    c.print(F("<label><input type='checkbox' name='dhcp' value='1'")); if(settings.useDHCP) c.print(F(" checked")); c.println(F("> DHCP (fallback static)</label><br>"));
    c.print(F("<label>IP</label><input name='ip' value='")); c.print(settingsIP()); c.println(F("'>"));
    c.print(F("<label>Gateway</label><input name='gw' value='")); c.print(settingsGW()); c.println(F("'>"));
    c.print(F("<label>Mask</label><input name='mask' value='")); c.print(settingsMask()); c.println(F("'>"));
    c.print(F("<label>DNS</label><input name='dns' value='")); c.print(settingsDNS()); c.println(F("'>"));
    c.println(F("<button class='btn' type='submit'>Save network</button></form></div>"));

    // SNMP
    c.println(F("<div class='card'><h2>SNMP</h2><form method='POST' action='/settings'>"));
    c.println(F("<input type='hidden' name='section' value='snmp'>"));
    c.print(F("<label><input type='checkbox' name='snmp_en' value='1'")); if(settings.snmpEnabled) c.print(F(" checked")); c.println(F("> Enabled</label><br>"));
    c.print(F("<label>Community Read</label><input name='snmp_r' value='")); c.print(settings.snmpCommunityRead); c.println(F("'>"));
    c.print(F("<label>Community Write</label><input name='snmp_w' value='")); c.print(settings.snmpCommunityWrite); c.println(F("'>"));
    c.print(F("<label>OID Prefix</label><input name='snmp_oid' value='")); c.print(settings.snmpOidPrefix); c.println(F("'>"));
    c.println(F("<button class='btn' type='submit'>Save SNMP</button></form></div>"));

    // Traps global
    c.println(F("<div class='card'><h2>SNMP Traps</h2><form method='POST' action='/settings'>"));
    c.println(F("<input type='hidden' name='section' value='traps'>"));
    c.print(F("<label><input type='checkbox' name='traps_en' value='1'")); if(settings.trapsEnabled) c.print(F(" checked")); c.println(F("> Traps enabled</label><br>"));
    c.print(F("<label>Manager IP</label><input name='trap_ip' value='")); c.print(settingsTrapIP()); c.println(F("'>"));
    c.println(F("<p style='font-size:12px;color:#889'>High-temp specific=1, Missing specific=2</p>"));
    c.println(F("<button class='btn' type='submit'>Save traps global</button></form></div>"));

    // Per-sensor traps + names
    c.println(F("<div class='card'><h2>Sensors &amp; traps</h2><form method='POST' action='/settings'>"));
    c.println(F("<input type='hidden' name='section' value='sensors'>"));
    c.println(F("<table><tr><th>Sensor</th><th>Name</th><th>T trap</th><th>Threshold C</th><th>Hold s</th><th>Missing</th></tr>"));
    for (int b=0;b<BUS_COUNT;b++) for (int i=0;i<MAX_SENSORS_PER_BUS;i++) {
        SensorTrapCfg &tc = settings.trapCfg[b][i];
        c.print(F("<tr><td>B")); c.print(b+1); c.print('S'); c.print(i+1); c.print(F("</td><td>"));
        c.print(F("<input name='sn_")); c.print(b); c.print('_'); c.print(i); c.print(F("' value='"));
        c.print(getSensorName(b,i)); c.print(F("' style='width:90px'></td><td>"));
        c.print(F("<input type='checkbox' name='te_")); c.print(b); c.print('_'); c.print(i); c.print(F("' value='1'"));
        if (tc.tempEnabled) c.print(F(" checked")); c.print(F("></td><td>"));
        c.print(F("<input name='th_")); c.print(b); c.print('_'); c.print(i); c.print(F("' value='"));
        c.print(tc.tempThreshold,1); c.print(F("' style='width:60px'></td><td>"));
        c.print(F("<input name='ho_")); c.print(b); c.print('_'); c.print(i); c.print(F("' value='"));
        c.print(tc.holdSec); c.print(F("' style='width:50px'></td><td>"));
        c.print(F("<input type='checkbox' name='mi_")); c.print(b); c.print('_'); c.print(i); c.print(F("' value='1'"));
        if (tc.missingEnabled) c.print(F(" checked")); c.println(F("></td></tr>"));
    }
    c.println(F("</table><button class='btn' type='submit'>Save sensors/traps</button></form></div>"));

    // Users
    c.println(F("<div class='card'><h2>Users</h2><table><tr><th>User</th><th></th></tr>"));
    for (int i=0;i<settings.userCount;i++) {
        if (!settings.users[i].active) continue;
        c.print(F("<tr><td>")); c.print(settings.users[i].username); c.print(F("</td><td>"));
        c.print(F("<form method='POST' action='/settings' style='display:inline'><input type='hidden' name='section' value='deluser'>"));
        c.print(F("<input type='hidden' name='deluser' value='")); c.print(settings.users[i].username);
        c.println(F("'><button class='btn btn-d' type='submit'>Del</button></form></td></tr>"));
    }
    c.println(F("</table><form method='POST' action='/settings'><input type='hidden' name='section' value='adduser'>"));
    c.println(F("<label>New user</label><input name='newuser' maxlength='15'><label>Pass</label><input name='newpass' type='password' maxlength='23'>"));
    c.println(F("<button class='btn' type='submit'>Add</button></form>"));
    c.println(F("<form method='POST' action='/settings'><input type='hidden' name='section' value='chpass'>"));
    c.print(F("<label>User</label><input name='chuser' value='")); c.print(sessionUser); c.println(F("'>"));
    c.println(F("<label>New pass</label><input name='chpass' type='password' maxlength='23'>"));
    c.println(F("<button class='btn' type='submit'>Change pass</button></form></div>"));
    c.println(F("</body></html>"));
}

static void handleSettingsPost(NetworkClient &c, const String &body) {
    String section = getFormValue(body, "section");
    const char *msg = "Saved";

    if (section == "net") {
        settings.useDHCP = (getFormValue(body,"dhcp")=="1");
        IPAddress ip,gw,mask,dns;
        ip.fromString(getFormValue(body,"ip")); gw.fromString(getFormValue(body,"gw"));
        mask.fromString(getFormValue(body,"mask")); dns.fromString(getFormValue(body,"dns"));
        setSettingsIP(ip,gw,mask,dns); saveSettings();
        msg = "Network saved (apply on next link/reboot)";
    } else if (section == "snmp") {
        settings.snmpEnabled = (getFormValue(body,"snmp_en")=="1");
        strncpy(settings.snmpCommunityRead,  getFormValue(body,"snmp_r").c_str(), MAX_SNMP_COMM_LEN-1);
        strncpy(settings.snmpCommunityWrite, getFormValue(body,"snmp_w").c_str(), MAX_SNMP_COMM_LEN-1);
        strncpy(settings.snmpOidPrefix,      getFormValue(body,"snmp_oid").c_str(), MAX_SNMP_OID_LEN-1);
        saveSettings(); needdeInitSNMP=true; needInitSNMP=true;
        msg = "SNMP saved, reinit pending";
    } else if (section == "traps") {
        settings.trapsEnabled = (getFormValue(body,"traps_en")=="1");
        IPAddress tip; tip.fromString(getFormValue(body,"trap_ip"));
        for (int i=0;i<4;i++) settings.trapManagerIP[i]=tip[i];
        saveSettings(); needdeInitSNMP=true; needInitSNMP=true;
        msg = "Traps global saved";
    } else if (section == "sensors") {
        for (int b=0;b<BUS_COUNT;b++) for (int i=0;i<MAX_SENSORS_PER_BUS;i++) {
            char key[16];
            snprintf(key,sizeof(key),"sn_%d_%d",b,i);
            String name = getFormValue(body,key);
            if (name.length()) setSensorName(b,i,name.c_str());
            snprintf(key,sizeof(key),"te_%d_%d",b,i);
            settings.trapCfg[b][i].tempEnabled = (getFormValue(body,key)=="1");
            snprintf(key,sizeof(key),"th_%d_%d",b,i);
            settings.trapCfg[b][i].tempThreshold = getFormValue(body,key).toFloat();
            snprintf(key,sizeof(key),"ho_%d_%d",b,i);
            settings.trapCfg[b][i].holdSec = (uint16_t)getFormValue(body,key).toInt();
            if (settings.trapCfg[b][i].holdSec < 1) settings.trapCfg[b][i].holdSec = 1;
            snprintf(key,sizeof(key),"mi_%d_%d",b,i);
            settings.trapCfg[b][i].missingEnabled = (getFormValue(body,key)=="1");
        }
        saveSettings(); msg = "Sensors/traps saved";
    } else if (section == "adduser") {
        if (addUser(getFormValue(body,"newuser").c_str(), getFormValue(body,"newpass").c_str()))
            { saveSettings(); msg="User added"; } else msg="Add user failed";
    } else if (section == "deluser") {
        if (deleteUser(getFormValue(body,"deluser").c_str()))
            { saveSettings(); msg="User deleted"; } else msg="Cannot delete";
    } else if (section == "chpass") {
        if (changePassword(getFormValue(body,"chuser").c_str(), getFormValue(body,"chpass").c_str()))
            { saveSettings(); msg="Password changed"; } else msg="Change failed";
    }
    handleSettingsPage(c, msg);
}

static bool webServerStarted = false;
void initWebServer() {
    if (webServerStarted) return;
    server.begin(); webServerStarted = true;
    Serial.println(F("Web server :80"));
}
void deinitWebServer() {
    if (!webServerStarted) return;
    server.end(); webServerStarted = false;
    Serial.println(F("Web server stopped"));
}

void handleWebClient() {
    NetworkClient client = server.accept();
    if (!client) return;
    client.setTimeout(2000);
    HttpRequest req;
    if (!parseRequest(client, req)) { client.stop(); return; }
    bool authed = checkAuthCookie(req.cookie);

    if (req.path == "/temp") {
        sendHeaders(client, F("application/json"), "Access-Control-Allow-Origin: *");
        sendJsonSensors(client);
    } else if (req.path == "/login") {
        if (req.method == "POST") {
            String u = getFormValue(req.body,"user"), p = getFormValue(req.body,"pass");
            if (authenticateUser(u.c_str(), p.c_str())) {
                createSession(u.c_str());
                char cookie[64]; snprintf(cookie,sizeof(cookie),"session=%s; Path=/; HttpOnly", sessionToken);
                sendRedirect(client, "/", cookie);
            } else handleLoginPage(client, "Bad login");
        } else {
            if (authed) sendRedirect(client,"/"); else handleLoginPage(client);
        }
    } else if (req.path == "/logout") {
        clearSession(); sendRedirect(client, "/login", "session=; Path=/; Max-Age=0");
    } else if (!authed) {
        sendRedirect(client, "/login");
    } else if (req.path=="/" || req.path=="/index.html") {
        handleRoot(client);
    } else if (req.path == "/settings") {
        if (req.method=="POST") handleSettingsPost(client, req.body);
        else handleSettingsPage(client);
    } else send404(client);
    client.stop();
}
