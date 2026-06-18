//
// Created by awalol on 2026/5/4.
//

#ifndef DS5_BRIDGE_CMD_H
#define DS5_BRIDGE_CMD_H

#include <stdint.h>

void pico_cmd_set(uint8_t report_id, uint8_t const *buffer,uint16_t bufsize);

#endif //DS5_BRIDGE_CMD_H
