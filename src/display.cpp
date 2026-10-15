#include "display.h"
#include "config.h"
#include "globals.h"
#include <ETH.h>
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
void printSensorLine(BusConfig (&bus)[BUS_COUNT], int lineIndex) ;

String getStatusLine();

static int scrollOffset = 0;
static int totalLines = 0;
static int maxScrollOffset = 0;

void tickScroll() {
    if (needScroll) {
        scrollOffset++;
        if (scrollOffset >= totalLines) scrollOffset = 0;
    }
}


// ========== ОБНОВЛЕНИЕ OLED ==========
void updateOled(BusConfig (&bus)[BUS_COUNT]) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    String status = getStatusLine();
    display.println(status);

    if (totalSensors == 0) {
        display.println(F("No sensors found"));
        display.display();
        return;
    }

    if (!needScroll) {
    for (int line = 0; line < OLED_VISIBLE_LINES; line++) {
        printSensorLine(bus, line);
    }
    } else {
        for (int line = 0; line < OLED_VISIBLE_LINES; line++) {
            int sensorIndex = (scrollOffset + line) % totalLines;
            printSensorLine(bus, sensorIndex);
        }
    }
    display.display();
    
}

// ========== ПОЛУЧЕНИЕ СТРОКИ СТАТУСА ==========
String getStatusLine() {
    String status = "";

    if (networkInitialized) {
        status = "";
         status += ETH.localIP().toString();  // Читаем напрямую
    } else {
        status = "NO NETWORK";
    }

    status += " S:";
    status += totalSensors;

    if (status.length() > 21) {
        status = status.substring(0, 21);
    }

    return status;
}


// ========== ВЫВОД ОДНОЙ СТРОКИ С ДВУМЯ ДАТЧИКАМИ ==========
void printSensorLine(BusConfig (&bus)[BUS_COUNT], int lineIndex) {
    int currentLine = 0;

    for (int b = 0; b < BUS_COUNT; b++) {
        int sensorsInBus = bus[b].count;
        int linesInBus;

        if (sensorsInBus == 0) {
            linesInBus = 1;
        } else {
            linesInBus = (sensorsInBus + 1) / 2;
        }

        if (lineIndex >= currentLine && lineIndex < currentLine + linesInBus) {
            int localLine = lineIndex - currentLine;

            display.print("B");
            display.print(b + 1);
            display.print(":");

            if (sensorsInBus == 0) {
                display.print(" ---");
                display.println();
                return;
            }

            int firstSensor = localLine * 2;
            int secondSensor = firstSensor + 1;

            if (firstSensor < sensorsInBus) {
                display.print(" T");
                display.print(firstSensor + 1);
                display.print(":");

                if (isSensorVisible(bus[b], firstSensor)) {
                    display.print(bus[b].temperatures[firstSensor], 1);
                    display.print("C");
                } else {
                    display.print("---");
                }
            }

            if (secondSensor < sensorsInBus) {
                display.print(" T");
                display.print(secondSensor + 1);
                display.print(":");

                if (isSensorVisible(bus[b], secondSensor)) {
                    display.print(bus[b].temperatures[secondSensor], 1);
                    display.print("C");
                } else {
                    display.print("---");
                }
            }

            display.println();
            return;
        }

        currentLine += linesInBus;
    }
}


// ========== РАСЧЕТ ПАРАМЕТРОВ ПРОКРУТКИ ==========
void calculateScrollParams(BusConfig (&bus)[BUS_COUNT]) {
    totalLines = 0;
    for (int b = 0; b < BUS_COUNT; b++) {
        int sensorsInBus = bus[b].count;
        if (sensorsInBus == 0) {
            totalLines += 1;
        } else {
            totalLines += (sensorsInBus + 1) / 2;
        }
    }

    needScroll = (totalLines > OLED_VISIBLE_LINES);

    if (needScroll) {
        maxScrollOffset = totalLines - OLED_VISIBLE_LINES;
    } else {
        scrollOffset = 0;
        maxScrollOffset = 0;
    }
}

