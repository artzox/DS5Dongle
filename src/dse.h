//
// DualSense Edge profile support for the PS Accessories app.
//
// The Edge stores up to four custom controller profiles. The PS Accessories
// app reads them back through feature reports 0x70-0x7B, but the controller
// only populates those reports (a "snapshot") after it processes a SET 0x80
// unlock command, which takes ~3.5s. This module reproduces the native BT
// host's unlock handshake so the profiles appear on the first app open
// without delaying USB enumeration, and keeps the snapshot fresh after saves.
//

#ifndef DS5_BRIDGE_DSE_H
#define DS5_BRIDGE_DSE_H

#include <cstdint>

// Called from the BT control-channel handler when an Edge controller is
// detected (0xA3 0x70). Sends the 0x65 echo + CRC'd SET 0x80 unlock and arms
// the unlock sequence. USB can connect immediately afterwards; profile reads
// are gated by dse_profiles_ready() until the snapshot is prepared.
void dse_on_connect();

// Called from the BT control-channel handler for every received feature
// report (0xA3 ...). Lets the module observe HID HANDSHAKE responses.
void dse_on_control_packet(const uint8_t *packet, uint16_t size);

// Called when a profile slot write (SET 0x60-0x62) is forwarded, so the
// module can replay the controller's post-save snapshot-regeneration cycle.
void dse_on_profile_write(uint8_t reportId);

// True once the profile snapshot is ready to read. While false, the USB GET
// handler should NAK (return 0) reads of 0x70-0x7B so the app retries.
bool dse_profiles_ready();

// True for report ids that are DSE profile read-back reports (0x70-0x7B).
bool dse_is_profile_report(uint8_t reportId);

// Pumped from the main loop. Drives the paced prefetch sequencer and the
// post-save regeneration cycle.
void dse_task();

#endif // DS5_BRIDGE_DSE_H
