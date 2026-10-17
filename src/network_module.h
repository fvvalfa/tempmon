#include <ETH.h>
#include "settings.h"
#include "globals.h"

static network_event_handle_t eth_event_handle = 0;
void resetNetwork();
static void applyStaticIP();
static void tryRestartDHCP();
void onEthEvent(arduino_event_id_t event, arduino_event_info_t info);
bool initNetwork(bool doHwReset = true);
void checkNetwork();