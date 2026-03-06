#pragma once
#include <hap.h>

/**
 * Create and register the thermostat accessory with HAP.
 * Returns the accessory handle, or NULL on failure.
 */
hap_acc_t *hk_thermostat_create(void);

/**
 * Called periodically from the main loop to sync HomeKit state
 * with the controller cache (pushes notifications to connected clients).
 */
void hk_thermostat_sync(void);
