//
// Created by awalol on 2026/5/15.
//

#ifndef DS5_BRIDGE_STATE_MGR_H
#define DS5_BRIDGE_STATE_MGR_H
#include <cstdint>

void state_init();
void synth_watchdog();
void state_set(uint8_t *data, uint8_t size);
bool state_update(const uint8_t *data, uint8_t size);
void set_volume(uint8_t value);
void set_volume(uint8_t speaker, uint8_t headset);

#endif //DS5_BRIDGE_STATE_MGR_H
