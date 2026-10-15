#pragma once

#include "Arduino.h"


// Пины под мою текущий разводку
//========== СЕТЕВЫЕ НАСТРОЙКИ ==========

#define ETH_PHY_TYPE        ETH_PHY_W5500
#define ETH_PHY_ADDR        1               // обычно 1 для W5500
#define ETH_PHY_CS          5               // ваш W5500_CS
#define ETH_PHY_IRQ         -1              // если не подключён IRQ — ставьте -1
#define ETH_PHY_RST         4               // ваш W5500_RST
#define ETH_PHY_SPI_FREQ_MHZ 16             // 16–25 обычно нормально

#define CHECK_INTERVAL_NETWORK  15000 // проверка каждые 10 секунд


// SPI пины для ESP
#define ETH_SPI_SCK         18
#define ETH_SPI_MISO        19
#define ETH_SPI_MOSI        23

//DEFAULT IP
#define MYIPADDR 192,168,1,177
#define MYIPMASK 255,255,255,0
#define MYDNS 192,168,1,1
#define MYGW 192,168,1,1

//DISPLAY SETTINGS
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_ADDRESS 0x3C
#define OLED_VISIBLE_LINES 3
#define SCROLL_INTERVAL 3000

// ========== НАСТРОЙКИ ПИНОВ ДЛЯ ESP32 ==========
#define ONE_WIRE_BUS_1 26
#define ONE_WIRE_BUS_2 25
#define ONE_WIRE_BUS_3 33
#define ONE_WIRE_BUS_4 32

//НАСТРОЙКА ДАТЧИКОВ
#define TEMPERATURE_PRECISION 12
#define MAX_SENSORS_PER_BUS 5
#define MAX_TOTAL_SENSORS 15


// ========== SNMP: НАСТРОЙКИ ==========
#define SNMP_COMMUNITY_READ  "public"
#define SNMP_COMMUNITY_WRITE "private"
#define SNMP_OID_PREFIX      ".1.3.6.1.4.1.99999"   // свой префикс при необходимости
#define SNMP_NO_DATA         (-1270)                // -127.0 °C × 10