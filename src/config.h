#pragma once
#include "Arduino.h"

//========== СЕТЕВЫЕ НАСТРОЙКИ ==========
#define ETH_PHY_TYPE        ETH_PHY_W5500
#define ETH_PHY_ADDR        1
#define ETH_PHY_CS          5
#define ETH_PHY_IRQ         -1
#define ETH_PHY_RST         4
#define ETH_PHY_SPI_FREQ_MHZ 16

#define CHECK_INTERVAL_NETWORK  15000
#define DHCP_TIMEOUT_MS         20000
#define DHCP_RETRY_INTERVAL_MS  300000
#define LINK_DOWN_REINIT_MS     60000

#define ETH_SPI_SCK         18
#define ETH_SPI_MISO        19
#define ETH_SPI_MOSI        23

#define MYIPADDR 192,168,1,177
#define MYIPMASK 255,255,255,0
#define MYDNS 192,168,1,1
#define MYGW 192,168,1,1

//DISPLAY
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define I2C_ADDRESS 0x3C
#define OLED_VISIBLE_LINES 3
#define SCROLL_INTERVAL 3000

// PINS
#define ONE_WIRE_BUS_1 26
#define ONE_WIRE_BUS_2 25
#define ONE_WIRE_BUS_3 33
#define ONE_WIRE_BUS_4 32

#define TEMPERATURE_PRECISION 12
#define MAX_SENSORS_PER_BUS 5
#define MAX_TOTAL_SENSORS 15

// SETTINGS / AUTH
#define MAX_USERS            5
#define MAX_USERNAME_LEN     16
#define MAX_PASSWORD_LEN     24
#define MAX_SENSOR_NAME_LEN  20
#define MAX_SNMP_COMM_LEN    24
#define MAX_SNMP_OID_LEN     40
#define SESSION_TIMEOUT_MS   1800000UL

// SNMP defaults
#define SNMP_COMMUNITY_READ  "public"
#define SNMP_COMMUNITY_WRITE "private"
#define SNMP_OID_PREFIX      ".1.3.6.1.4.1.99999"
#define SNMP_NO_DATA         (-1270)

// Trap specific codes
#define TRAP_SPECIFIC_HIGH_TEMP  1
#define TRAP_SPECIFIC_MISSING    2
