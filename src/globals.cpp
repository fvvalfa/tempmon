#include "globals.h"
#include <config.h>
int totalSensors=0;
BusConfig buses[BUS_COUNT] = {};

volatile bool need_update_oled = false;

volatile bool needInitSNMP = false;
volatile bool needdeInitSNMP = false;
bool needScroll = false;

