#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "globals.h"

extern Adafruit_SSD1306 display;
void updateOled(BusConfig (&bus)[BUS_COUNT]);
void calculateScrollParams(BusConfig (&bus)[BUS_COUNT]) ;
void tickScroll();