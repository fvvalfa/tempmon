#include "webserver.h"
#include "config.h"
#include "display.h"
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <ETH.h>

// NetworkServer из ETH.h
static NetworkServer server(80);

// Внешние флаги из main.cpp
extern bool networkInitialized;


// ========== ГЕНЕРАЦИЯ JSON ==========
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
                client.print(F(",\"temp\":"));
                client.print(buses[b].temperatures[i], 1);
                client.print('}');
            //}
        }
    }
    client.print(F("]}"));
}

// ========== ОБЩИЕ ЗАГОЛОВКИ ==========
static void sendHeaders(NetworkClient &client, const __FlashStringHelper *contentType,
                        bool noCache = true, bool cors = false) {
    if (!client.connected()) return;
    client.println(F("HTTP/1.1 200 OK"));
    client.print(F("Content-Type: "));
    client.println(contentType);
    if (noCache) {
        client.println(F("Cache-Control: no-cache, no-store, must-revalidate"));
    }
    if (cors) {
        client.println(F("Access-Control-Allow-Origin: *"));
    }
    client.println(F("Connection: close"));
    client.println();
}

// ========== ЧТЕНИЕ ЗАПРОСА ==========
// Читаем первую строку запроса (метод + URI), остальное игнорируем.
// Возвращаем строку вида "GET /temp HTTP/1.1" или пустую при таймауте.
static String readRequestLine(NetworkClient &client) {
    String line;
    line.reserve(96);                 // типичная длина строки запроса
    unsigned long start = millis();
    const unsigned long HARD_TIMEOUT = 2000;
    while (client.connected() && (millis() - start) < HARD_TIMEOUT) {
        if (!client.available()) { delay(1); continue; }
        char c = client.read();
        if (c == '\n') break;
        if (c != '\r' && line.length() < 200) line += c;
        // НЕ сбрасываем start
    }
    return line;
}

// ========== ОБРАБОТЧИКИ ==========
static void handleRoot(NetworkClient &client) {
    sendHeaders(client, F("text/html; charset=utf-8"));

    client.println(F("<!DOCTYPE html><html><head>"));
    client.println(F("<meta charset='UTF-8'>"));
    client.println(F("<meta http-equiv='refresh' content='10'>"));
    client.println(F("<title>Temp Monitor</title><style>"));
    client.println(F("body{background:#0a0e17;color:#eee;font-family:Arial;padding:20px;text-align:center}"));
    client.println(F(".s{background:#1a2238;padding:15px;margin:10px;border-radius:12px;display:inline-block;min-width:120px}"));
    client.println(F(".t{font-size:36px;font-weight:bold;color:#4ecdc4}"));
    client.println(F(".e{color:#ff6b6b;font-size:24px}"));
    client.println(F(".json{background:#0a0a1a;padding:15px;border-radius:8px;text-align:left;font-size:12px;max-width:500px;margin:20px auto;overflow:auto}"));
    client.println(F("a{color:#4ecdc4}.ip{color:#8899bb;font-size:14px;margin-bottom:20px}"));
    client.println(F(".btn{background:#4ecdc4;color:#0a0e17;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;text-decoration:none;display:inline-block;margin:10px}"));
    client.println(F("</style></head><body>"));
    client.println(F("<h1>🌡️ Температурный мониторинг</h1>"));

    client.print(F("<div class='ip'>📍 IP: "));
    client.print(ETH.localIP());
    client.println(F("</div>"));

    client.println(F("<div style='margin:10px'>"));
    for (int b = 0; b < BUS_COUNT; b++) {
        if (!client.connected()) return;  
        client.print(F("<h3>🔌 Bus "));
        client.print(b + 1);
        client.println(F("</h3>"));
        for (int i = 0; i < buses[b].count; i++) {
            client.print(F("<div class='s'><b>Sensor "));
            client.print(i + 1);
            client.print(F("</b><br>"));
            if (isSensorVisible(buses[b], i)){
            //if (buses[b].sensorOk[i] && buses[b].temperatures[i] != -127.0) {
                client.print(F("<span class='t'>"));
                client.print(buses[b].temperatures[i], 1);
                client.println(F("°C</span>"));
            } else {
                client.print(F("<span class='e'>---</span>"));
            }
            client.println(F("</div>"));
        }
    }
    client.println(F("</div>"));

    client.println(F("<div class='json'><b>📊 JSON:</b><br>"));
    // Экранированные кавычки для вставки в HTML не нужны — можно просто вывести
    sendJsonSensors(client);
    client.println(F("</div>"));

    client.print(F("<div style='font-size:12px;color:#666;margin-top:20px'>"));
    client.print(F("Uptime: "));
    client.print(millis() / 1000);
    client.print(F(" сек | "));
    client.print(F("<a href='/temp'>JSON</a> | "));
    client.println(F("</div>"));
    client.println(F("</body></html>"));
}

static void handleTempJson(NetworkClient &client) {
    sendHeaders(client, F("application/json"), /*noCache=*/true, /*cors=*/true);
    sendJsonSensors(client);
}

static void handleNotFound(NetworkClient &client) {
    if (!client.connected()) return;
    const char* body = "404 Not Found";
    client.println(F("HTTP/1.1 404 Not Found"));
    client.println(F("Content-Type: text/plain"));
    client.print(F("Content-Length: "));
    client.println(strlen(body));
    client.println(F("Connection: close"));
    client.println();
    client.println(F(body));
}
static bool webServerStarted = false;

void initWebServer() {
    if (webServerStarted) {
        Serial.println(F("Web server already running"));
        return;
    }
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

static void sendMethodNotAllowed(NetworkClient &client, const String &method) {
    if (!client.connected()) return;

    String body = F("405 Method Not Allowed: ");
    body += method;

    client.println(F("HTTP/1.1 405 Method Not Allowed"));
    client.println(F("Content-Type: text/plain"));
    client.print(F("Allow: GET"));
    client.println();
    client.print(F("Content-Length: "));
    client.println(body.length());
    client.println(F("Connection: close"));
    client.println();
    client.println(body);
}
void handleWebClient() {
    NetworkClient client = server.accept();
    if (!client) return;

    client.setTimeout(1000);

    String requestLine = readRequestLine(client);

    if (requestLine.length() == 0 || !client.connected()) {
        client.stop();
        return;
    }

    // Парсим "METHOD /path HTTP/1.1"
    int firstSpace  = requestLine.indexOf(' ');
    if (firstSpace <= 0) {
        // Не HTTP-запрос
        client.stop();
        return;
    }

    int secondSpace = requestLine.indexOf(' ', firstSpace + 1);

    String method = requestLine.substring(0, firstSpace);
    method.trim();
    method.toUpperCase();

    String path = "/";
    if (secondSpace > firstSpace) {
        path = requestLine.substring(firstSpace + 1, secondSpace);
    } else {
        path = requestLine.substring(firstSpace + 1);
    }

    // Отбрасываем query-string
    int q = path.indexOf('?');
    if (q >= 0) path = path.substring(0, q);

    // Разрешаем только GET
    if (method != "GET") {
        Serial.print(F("WEB: 405 for method "));
        Serial.print(method);
        Serial.print(F(" path "));
        Serial.println(path);
        sendMethodNotAllowed(client, method);
        client.stop();
        return;
    }

    // Маршрутизация
    if (path == "/" || path == "/index.html") {
        handleRoot(client);
    } else if (path == "/temp") {
        handleTempJson(client);
    } else {
        handleNotFound(client);
    }

    client.stop();
}