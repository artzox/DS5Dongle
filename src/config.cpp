//
// Created by awalol on 2026/5/4.
//

#include "config.h"

#include <cmath>
#include <cstring>

#include "state_mgr.h"
#include "utils.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"

constexpr uint32_t CONFIG_MAGIC = 0x66ccff00;
constexpr uint16_t CONFIG_VERSION = 18;
// btstack's TLV flash bank (BT link keys + this project's pairing blacklist tag)
// occupies the LAST TWO flash sectors by pico-sdk default
// (PICO_FLASH_BANK_STORAGE_OFFSET) - and config + profile slots used to sit in
// those exact sectors. Every TLV write (link-key churn on controller
// sleep/wake/re-pair) could clobber them; the first visible casualty was profile
// slot 0 reading "empty" after a sleep/wake cycle (the bank header lands at its
// sector start). Config and slots now live in the two sectors BELOW the bank,
// with one-shot boot migration from the legacy locations.
constexpr uint32_t CONFIG_FLASH_OFFSET        = PICO_FLASH_SIZE_BYTES - 3 * FLASH_SECTOR_SIZE;
constexpr uint32_t LEGACY_CONFIG_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
static Config config{};
bool is_dse = false;

// 编译期保护
// 判断Config结构体是否能放进flash 256bytes
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);
// 配置区起始地址必须按 flash sector 对齐。
static_assert(CONFIG_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);

uint32_t calc_config_crc(const Config &con) {
    return crc32(reinterpret_cast<const uint8_t *>(&con.body), sizeof(Config_body));
}

const Config *flash_config() {
    return reinterpret_cast<const Config *>(XIP_BASE + CONFIG_FLASH_OFFSET);
}

void config_valid() {
    // valid config and set default value
    if (config.magic != CONFIG_MAGIC) {
        config.magic = CONFIG_MAGIC;
        printf("[Config] Config Magic Header is invalid\n");
    }
    if (config.size != sizeof(Config_body)) {
        config.size = sizeof(Config_body);
        printf("[Config] Config Body size is invalid\n");
    }
    auto body = &config.body;
    if (std::isnan(body->haptics_gain) || body->haptics_gain < 1.0f || body->haptics_gain > 2.0f) {
        body->haptics_gain = 1.0f;
        printf("[Config] Haptics Gain value is invalid\n");
    }
    if (body->speaker_volume < 0 || body->speaker_volume > 127) {
        body->speaker_volume = 100;
        printf("[Config] Speaker Volume is invalid\n");
    }
    if (body->headset_volume < 0 || body->headset_volume > 127) {
        body->headset_volume = 100;
        printf("[Config] Headset Volume is invalid\n");
    }
    if (body->sync_spk_headset_volume > 1) {
        body->sync_spk_headset_volume = 1;
        printf("[Config] sync_spk_headset_volume is invalid\n");
    }
    if (body->speaker_gain < 0 || body->speaker_gain > 7) {
        body->speaker_gain = 2;
        printf("[Config] speaker_gain is invalid\n");
    }
    if (body->inactive_time < 5 || body->inactive_time > 60) {
        body->inactive_time = 30;
        printf("[Config] Inactive time is invalid\n");
    }
    if (body->disable_inactive_disconnect > 1) {
        body->disable_inactive_disconnect = 0;
        printf("[Config] disable_auto_disconnect is invalid\n");
    }
    if (body->disable_pico_led > 1) {
        body->disable_pico_led = 0;
        printf("[Config] disable_pico_led is invalid\n");
    }
    if (body->polling_rate_mode > 2) {
        body->polling_rate_mode = 0;
        printf("[Config] polling_rate_mode is invalid\n");
    }
    if (body->audio_buffer_length < 16 || body->audio_buffer_length > 128) {
        body->audio_buffer_length = 64;
        printf("[Config] haptics_buffer_length is invalid\n");
    }
    if (body->controller_mode > 2) {
        body->controller_mode = 2;
        printf("[Config] controller_mode is invalid\n");
    }
    if (body->config_version != CONFIG_VERSION) {
        body->config_version = CONFIG_VERSION;
        printf("[Config] Warning: Config may breaking change\n");
    }
    if (body->lock_volume > 1) {
        body->lock_volume = 0;
        printf("[Config] lock_volume is invalid\n");
    }
    if (body->disable_usb_sn > 1) {
        body->disable_usb_sn = 0;
        printf("[Config] Warning: disable_usb_sn is invalid\n");
    }
    if (body->ps_shortcut_enabled > 1) {
        body->ps_shortcut_enabled = 0;
        printf("[Config] ps_shortcut_enabled is invalid\n");
    }
    if (body->disable_mic > 1) {
        body->disable_mic = 0;
        printf("[Config] disable_mic is invalid\n");
    }
    if (body->disable_speaker > 1) {
        body->disable_speaker = 0;
        printf("[Config] disable_speaker is invalid\n");
    }
    if (body->auto_haptics_enable > 2) body->auto_haptics_enable = 0;
    if (body->auto_haptics_gain > 200) body->auto_haptics_gain = 100; // 0 is a VALID value (silence); >200 covers fresh-flash 0xFF
    if (body->auto_haptics_lowpass_hz < 20 || body->auto_haptics_lowpass_hz > 400) body->auto_haptics_lowpass_hz = 60;
    if (body->auto_mute_replace > 1) body->auto_mute_replace = 1;
    if (body->auto_mute_mix > 1) body->auto_mute_mix = 0;
    if (body->auto_haptics_gate > 100) body->auto_haptics_gate = 20;
    if (body->auto_haptics_slope != 6 && body->auto_haptics_slope != 12 && body->auto_haptics_slope != 24) body->auto_haptics_slope = 12;
    if (body->lightbar_off > 1) body->lightbar_off = 0;
    if (body->auto_haptics_smooth > 100) body->auto_haptics_smooth = 40;
    if (body->bt_flush_timeout > 0x07FF) body->bt_flush_timeout = 0; // 0=off, max per BT spec
    if (body->bt_qos_latency_us > 50000) body->bt_qos_latency_us = 0; // 0=off
    if (body->rumble_haptic_strength > 100) body->rumble_haptic_strength = 50;
    if (body->effect_leak_volume > 100) body->effect_leak_volume = 0; // 0=off
    if (body->effect_leak_hp_hz < 100 || body->effect_leak_hp_hz > 5000) body->effect_leak_hp_hz = 800;
    if (body->effect_leak_sensitivity > 100) body->effect_leak_sensitivity = 50;
    if (body->effect_leak_decay > 100) body->effect_leak_decay = 40;
    if (body->effect_leak_attack > 100) body->effect_leak_attack = 50;
    if (body->effect_leak_output_hp_hz < 50 || body->effect_leak_output_hp_hz > 2000) body->effect_leak_output_hp_hz = 200;
    // Rumble-to-trigger defaults (feature OFF by default).
    if (body->at_pushback > 100) body->at_pushback = 0;     // 0=off (fresh flash 0xFF lands here)
    if (body->at_pushback_src > 2) body->at_pushback_src = 2;
    if (body->at_pushback_freq < 10 || body->at_pushback_freq > 200) body->at_pushback_freq = 35; // fresh flash 0xFF lands here
    // Leak band-pass window + hold defaults (fresh flash 0xFF/0xFFFF lands here).
    if (body->effect_leak_lp_hz < 500 || body->effect_leak_lp_hz > 12000) body->effect_leak_lp_hz = 8000; // default raised in 1.5.2: 3500 gutted perceived loudness (tiny speaker is loudest 3-8 kHz)
    if (body->effect_leak_hold > 100) body->effect_leak_hold = 20; // x5 = 100 ms default
    if (body->at_kick_style > 1) body->at_kick_style = 0;          // R2 kick: vibration thump
    if (body->at_l2_mode > 3) body->at_l2_mode = 0;                 // 0=off,1=R2-gated,2=always,3=R1-shoulder-gated
    if (body->at_l2_strength > 100) body->at_l2_strength = 70;
    if (body->at_l2_threshold < 1) body->at_l2_threshold = 30;      // ~12% pull arms it
    if (body->at_l2_start_pos > 9) body->at_l2_start_pos = 0;
    if (body->at_l2_pushback > 100) body->at_l2_pushback = 0;       // 0 = no kick on L2
    if (body->at_l2_pushback_freq < 10 || body->at_l2_pushback_freq > 200) body->at_l2_pushback_freq = 35;
    if (body->at_l2_kick_style > 1) body->at_l2_kick_style = 0;
    // Frequency split: 0=off. Out-of-range values CLAMP to the nearest bound
    // instead of silently disabling the feature (a user entering 500 previously
    // got split=off with no feedback and concluded the gains did nothing).
    // 0xFFFF (fresh flash) still lands on off via the upper clamp exception.
    if (body->ah_xover_hz != 0) {
        if (body->ah_xover_hz == 0xFFFF) body->ah_xover_hz = 0;          // fresh flash
        else if (body->ah_xover_hz < 30)  body->ah_xover_hz = 30;
        else if (body->ah_xover_hz > 200) body->ah_xover_hz = 200;
    }
    if (body->ah_low_gain > 100) body->ah_low_gain = 100;
    if (body->ah_high_gain > 100) body->ah_high_gain = 100;
    // Trigger resistance shapes (fresh flash 0xFF lands on defaults)
    if (body->at_shape > 3) body->at_shape = 0;    // 3 = weapon break (v1.12.0)
    if (body->at_strength_b > 100) body->at_strength_b = 70;
    if (body->at_detent_pos > 9) body->at_detent_pos = 5;
    if (body->at_l2_shape > 3) body->at_l2_shape = 0;
    if (body->at_l2_strength_b > 100) body->at_l2_strength_b = 70;
    if (body->at_l2_detent_pos > 9) body->at_l2_detent_pos = 5;
    if (body->at_deadzone > 9) body->at_deadzone = 0;
    if (body->at_l2_deadzone > 9) body->at_l2_deadzone = 0;
    if (body->mix_native_level > 100) body->mix_native_level = 100; // 0 is valid (mute passthrough)
    if (body->effect_leak_max_burst > 100) body->effect_leak_max_burst = 0; // 0=off; covers fresh-flash 0xFF
    // Custom captured-effect action (v1.14.0): validate scalar controls; state
    // bytes are raw (any value valid). Fresh-flash 0xFF -> safe defaults.
    if (body->ce_r2_enable > 1) body->ce_r2_enable = 0;
    if (body->ce_r2_condition > 2) body->ce_r2_condition = 0;
    if (body->ce_r2_thresh > 9) body->ce_r2_thresh = 0; // default: re-arm only at full release
    if (body->ce_r2_rate < 1 || body->ce_r2_rate > 100) body->ce_r2_rate = 40;
    if (body->ce_r2_state_count > 5) body->ce_r2_state_count = 0;
    if (body->ce_l2_enable > 1) body->ce_l2_enable = 0;
    if (body->ce_l2_condition > 2) body->ce_l2_condition = 0;
    if (body->ce_l2_thresh > 9) body->ce_l2_thresh = 0;
    if (body->ce_l2_rate < 1 || body->ce_l2_rate > 100) body->ce_l2_rate = 40;
    if (body->ce_l2_state_count > 5) body->ce_l2_state_count = 0;
    if (body->r2t_mode > 3) body->r2t_mode = 0;            // 0=off
    if (body->r2t_on_press > 1) body->r2t_on_press = 0;    // 0=always
    if (body->r2t_strength > 100) body->r2t_strength = 100; // full strength
    if (body->r2t_frequency < 1) body->r2t_frequency = 60;  // ~mid tactile buzz
    // Adaptive triggers Stage 1 defaults (OFF by default).
    if (body->at_mode > 3) body->at_mode = 0;   // 0=off,1=L2-gated,2=always,3=L1-shoulder-gated
    if (body->at_strength > 100) body->at_strength = 70;
    if (body->at_threshold < 1) body->at_threshold = 30;   // ~12% pull arms it
    if (body->at_start_pos > 9) body->at_start_pos = 0;    // resist from the start of travel
    // Gyro aiming defaults (OFF by default).
    if (body->gyro_mode > 7) body->gyro_mode = 0; // 5=R2, 6=L1, 7=R1 gates (v1.11.0)
    if (body->gyro_sens < 1 || body->gyro_sens > 100) body->gyro_sens = 50;
    if (body->gyro_axis > 1) body->gyro_axis = 0;
    if (body->gyro_invert > 3) body->gyro_invert = 0;
    // Native-haptics smoothing: 0 is invalid so a fresh config defaults to Light (2).
    if (body->haptics_aa < 1 || body->haptics_aa > 3) body->haptics_aa = 2;
    if (body->synth_force > 1) body->synth_force = 0;
    if (body->enable_wake > 1) {
        body->enable_wake = 0;
        printf("[Config] enable_wake is invalid\n");
    }
}

static void migrate_legacy_slots(); // defined after the slot machinery below

// One-shot migration from the legacy (btstack-bank-colliding) locations. Runs at
// boot before BT init, so the bank cannot be mid-write while we read. Strict CRC
// validation: anything the TLV bank already clobbered is skipped (unrecoverable).
static void migrate_legacy_storage() {
    // Config: if the NEW location holds no valid config but the legacy one does,
    // adopt it.
    if (flash_config()->magic != CONFIG_MAGIC) {
        Config legacy{};
        memcpy(&legacy, reinterpret_cast<const void *>(XIP_BASE + LEGACY_CONFIG_FLASH_OFFSET), sizeof(Config));
        if (legacy.magic == CONFIG_MAGIC && legacy.crc32 == calc_config_crc(legacy)) {
            config = legacy;
            config_valid();
            if (config_save()) printf("[Config] migrated config from legacy sector\n");
        }
    }
    // Slot migration lives after the slot machinery is defined.
    migrate_legacy_slots();
}

void config_load() {
    memcpy(&config, flash_config(), sizeof(Config));
    migrate_legacy_storage();
    // migration may have replaced `config`; reload from the (new) authoritative sector
    memcpy(&config, flash_config(), sizeof(Config));

    config_valid();
}

// Runs with core1 parked (flash_safe_execute) and core0 interrupts disabled, so
// neither core touches XIP flash while the sector is erased/programmed. Without
// the core1 park this races the audio core and corrupts audio (buzzing).
static void config_save_flash_op(void *param) {
    const uint8_t *page = static_cast<const uint8_t *>(param);
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CONFIG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}

bool config_save() {
    config.crc32 = calc_config_crc(config);
    alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xff, sizeof(page));
    memcpy(page, &config, sizeof(Config));

    const int rc = flash_safe_execute(config_save_flash_op, page, 1000);
    if (rc != PICO_OK) {
        printf("[Config] config_save flash_safe_execute failed: %d\n", rc);
        return false;
    }

    Config verify{};
    memcpy(&verify, flash_config(), sizeof(verify));
    const auto verify_crc32 = calc_config_crc(verify);
    if (verify_crc32 == config.crc32) {
        printf("[Config] Config write flash verify success\n");
        return true;
    }
    printf("[Config] Config write flash verify failed\n");
    return false;
}

Config_body& get_config() {
    return config.body;
}

void set_config(const uint8_t *new_config, const uint16_t len) {
    const auto copy_len = len < sizeof(Config_body) ? len : sizeof(Config_body);
    memcpy(&config.body, new_config, copy_len);
    config_valid();
    if (config.body.disable_pico_led) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }else {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    }
    set_volume(config.body.speaker_volume,config.body.headset_volume);
    set_gain(config.body.speaker_gain);
}

// ============================ Profile slots ================================
// One flash sector (4 KB), 8 records of 512 bytes each. Updating any slot
// rewrites the whole sector from a RAM image (flash erase granularity), using
// the same core1-parked flash_safe_execute path as config_save.
constexpr uint32_t SLOTS_FLASH_OFFSET        = PICO_FLASH_SIZE_BYTES - 4 * FLASH_SECTOR_SIZE;
constexpr uint32_t LEGACY_SLOTS_FLASH_OFFSET = PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE;
static_assert(SLOTS_FLASH_OFFSET % FLASH_SECTOR_SIZE == 0);
constexpr uint32_t SLOT_STRIDE = FLASH_SECTOR_SIZE / SLOTS_PER_SECTOR; // 512
static_assert(SLOT_COUNT % SLOTS_PER_SECTOR == 0, "whole sectors only");
// Slots span multiple sectors (v1.9.0: 16 slots = 2 sectors). Sector 0 stays at
// the original -4 location so pre-existing slots 1-8 are preserved IN PLACE with
// no migration; additional sectors grow DOWNWARD (-5, -6, ...), away from the
// config sector (-3) and the btstack bank (-2/-1). Each sector is still erased
// and rewritten independently, so saving slot 12 never touches slots 1-8.
static uint32_t slot_sector_offset(uint8_t sector) {
    return PICO_FLASH_SIZE_BYTES - (4u + sector) * FLASH_SECTOR_SIZE;
}
constexpr uint32_t SLOT_MAGIC_V1 = 0x53355344; // "DS5S" — legacy: fixed-size body, layout-fragile
constexpr uint32_t SLOT_MAGIC_V2 = 0x54355344; // "DS5T" — v2: explicit body_len, survives body growth

// v2 record: body_len makes the CRC span self-describing, so future firmware with
// a LARGER Config_body can still validate and read records written by older
// firmware (the missing tail bytes simply default via config_valid()). This is
// what keeps profile slots alive across firmware upgrades from now on.
struct __attribute__((packed)) SlotRecordV2 {
    uint32_t magic;      // SLOT_MAGIC_V2
    uint16_t body_len;   // sizeof(Config_body) at the time of writing
    uint8_t  name[SLOT_NAME_LEN];
    Config_body body;    // body_len bytes of it are meaningful
    uint32_t crc32;      // over name + body_len bytes of body, stored right after them
};
static_assert(sizeof(SlotRecordV2) <= SLOT_STRIDE, "slot record must fit its stride");

// Legacy v1 record (pre-1.4.0): magic, name, body (no length), crc right after.
// Its validity check hardcoded sizeof(Config_body), so any firmware whose body
// size differed silently saw "empty" slots. A previous fix listed the historical
// body sizes relative to sizeof(Config_body) - which silently went stale every
// time the config grew (it already had, by 1.5.0). Instead: brute-force the body
// length. A v1 record is name + body + crc32(name+body); we scan every plausible
// body length and accept the one whose CRC matches. Runs only for records bearing
// the v1 magic (which no firmware writes since 1.3.2), so in practice only during
// boot-time legacy migration - the scan cost (<500 CRC checks over <=0.5 KB) is a
// few milliseconds once, and it recovers records from ANY historical layout,
// forever, with zero maintenance.

static const uint8_t *flash_slot_raw_at(uint32_t base_offset, uint8_t idx) {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + base_offset + idx * SLOT_STRIDE);
}
static const uint8_t *flash_slot_raw(uint8_t idx) {
    return flash_slot_raw_at(SLOTS_FLASH_OFFSET, idx);
}

// Unified reader: returns true and fills name/body if the slot holds a valid
// record in ANY known format. Missing tail bytes (older, smaller bodies) are
// 0xFF-filled so config_valid() lands them on defaults.
static bool slot_read_ptr(const uint8_t *p, uint8_t name_out[SLOT_NAME_LEN], Config_body &body_out) {
    uint32_t magic;
    memcpy(&magic, p, 4);
    if (magic == SLOT_MAGIC_V2) {
        uint16_t blen;
        memcpy(&blen, p + 4, 2);
        if (blen == 0) return false;                       // degenerate record
        if (blen > sizeof(Config_body)) {
            // Written by NEWER firmware with a bigger body? Accept the prefix we
            // understand (fields only ever append), CRC over the stored span.
            if (blen > SLOT_STRIDE - 4 - 2 - SLOT_NAME_LEN - 4) return false;
        }
        const uint8_t *name = p + 4 + 2;
        const uint8_t *body = name + SLOT_NAME_LEN;
        uint32_t stored;
        memcpy(&stored, body + blen, 4);
        if (stored != crc32(name, (uint32_t)SLOT_NAME_LEN + blen)) return false;
        memcpy(name_out, name, SLOT_NAME_LEN);
        memset(&body_out, 0xFF, sizeof(Config_body));
        memcpy(&body_out, body, (blen < sizeof(Config_body)) ? blen : sizeof(Config_body));
        return true;
    }
    if (magic == SLOT_MAGIC_V1) {
        const uint8_t *name = p + 4;
        const uint8_t *body = name + SLOT_NAME_LEN;
        constexpr uint16_t BLEN_MAX = (uint16_t)(SLOT_STRIDE - 4 - SLOT_NAME_LEN - 4);
        for (uint16_t blen = 16; blen <= BLEN_MAX; ++blen) {
            uint32_t stored;
            memcpy(&stored, body + blen, 4);
            if (stored == crc32(name, (uint32_t)SLOT_NAME_LEN + blen)) {
                memcpy(name_out, name, SLOT_NAME_LEN);
                memset(&body_out, 0xFF, sizeof(Config_body));
                memcpy(&body_out, body, blen);
                return true;
            }
        }
    }
    return false;
}

// RAM image of the whole slots sector for read-modify-erase-write. Static so it
// doesn't live on a task stack; only touched from the USB command path.
alignas(4) static uint8_t slot_sector_image[FLASH_SECTOR_SIZE];

static uint32_t slot_op_offset = SLOTS_FLASH_OFFSET; // which sector the op targets
static void slots_flash_op(void *) {
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(slot_op_offset, FLASH_SECTOR_SIZE);
    flash_range_program(slot_op_offset, slot_sector_image, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupts);
}

// Linear-base reader: used ONLY for the legacy (pre-1.4.0) location during boot
// migration, where all 8 slots were contiguous in one sector.
static bool slot_read_at(uint32_t base_offset, uint8_t idx, uint8_t name_out[SLOT_NAME_LEN], Config_body &body_out) {
    return slot_read_ptr(flash_slot_raw_at(base_offset, idx), name_out, body_out);
}
// Sector-mapped reader for the live slot store.
static bool slot_read(uint8_t idx, uint8_t name_out[SLOT_NAME_LEN], Config_body &body_out) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(
        XIP_BASE + slot_sector_offset(idx / SLOTS_PER_SECTOR) + (idx % SLOTS_PER_SECTOR) * SLOT_STRIDE);
    return slot_read_ptr(p, name_out, body_out);
}

bool slot_save(uint8_t idx, const uint8_t *name, uint8_t name_len) {
    if (idx >= SLOT_COUNT) return false;
    const uint32_t sec_off = slot_sector_offset(idx / SLOTS_PER_SECTOR);
    const uint8_t  local   = idx % SLOTS_PER_SECTOR;
    memcpy(slot_sector_image, reinterpret_cast<const void *>(XIP_BASE + sec_off), FLASH_SECTOR_SIZE);
    SlotRecordV2 rec{};
    rec.magic = SLOT_MAGIC_V2;
    rec.body_len = (uint16_t)sizeof(Config_body);
    memset(rec.name, 0, SLOT_NAME_LEN);
    if (name && name_len) memcpy(rec.name, name, (name_len < SLOT_NAME_LEN) ? name_len : SLOT_NAME_LEN);
    rec.body = config.body;
    rec.crc32 = crc32(rec.name, (uint32_t)SLOT_NAME_LEN + sizeof(Config_body));
    memset(slot_sector_image + local * SLOT_STRIDE, 0xff, SLOT_STRIDE);
    memcpy(slot_sector_image + local * SLOT_STRIDE, &rec, sizeof(rec));
    slot_op_offset = sec_off;
    int rc = flash_safe_execute(slots_flash_op, nullptr, 500);
    if (rc != PICO_OK) {
        printf("[Config] slot_save flash_safe_execute failed: %d\n", rc);
        return false;
    }
    uint8_t nm[SLOT_NAME_LEN]; Config_body bd;
    return slot_read(idx, nm, bd);
}

uint8_t slot_activate(uint8_t idx, bool &needs_reenum, uint8_t &fail_stage) {
    needs_reenum = false;
    fail_stage = 0;
    if (idx >= SLOT_COUNT) { fail_stage = 1; return 0; }
    uint8_t nm[SLOT_NAME_LEN];
    static Config_body slot_body; // static: off the USB task stack
    if (!slot_read(idx, nm, slot_body)) { fail_stage = 2; return 0; }
    // Fields that change USB descriptors / enumeration-time behavior; if any
    // differ, the caller should issue the reconnect command (0x03) afterwards.
    const Config_body &o = config.body;
    const Config_body &n = slot_body;
    needs_reenum = (o.controller_mode      != n.controller_mode)      ||
                   (o.polling_rate_mode    != n.polling_rate_mode)    ||
                   (o.audio_buffer_length  != n.audio_buffer_length)  ||
                   (o.disable_mic          != n.disable_mic)          ||
                   (o.disable_speaker      != n.disable_speaker)      ||
                   (o.enable_wake          != n.enable_wake)          ||
                   (o.disable_usb_sn       != n.disable_usb_sn)       ||
                   (o.ps_shortcut_enabled  != n.ps_shortcut_enabled);
    config.body = slot_body;
    config_valid(); // clamp anything out of range (e.g. slot saved by older fw)
    // Persist. flash_safe_execute parks core1 with a 1 s timeout; at game
    // launch core1 is at its busiest (BT stream + haptics), which is exactly
    // when a lockout timeout can trip - and never during calm portal tests.
    // Retry once; if persistence still fails, the activation itself has
    // ALREADY happened (config.body is live) - report 2 so callers can treat
    // a launch-time activation as successful instead of "slot failed".
    if (config_save()) return 1;
    sleep_ms(60);
    if (config_save()) return 1;
    fail_stage = 3;
    return 2;
}

// Public: load a slot's full config body (for backup/export). Returns false if
// the slot is empty or unreadable. The body is validated the same way a live
// load is, so an exported profile is always a coherent config.
bool slot_load_body(uint8_t idx, Config_body &out) {
    if (idx >= SLOT_COUNT) return false;
    uint8_t nm[SLOT_NAME_LEN]{};
    return slot_read(idx, nm, out);
}

bool slot_info(uint8_t idx, uint8_t name_out[SLOT_NAME_LEN], uint8_t &valid, uint8_t &cfg_version) {
    if (idx >= SLOT_COUNT) return false;
    uint8_t nm[SLOT_NAME_LEN];
    static Config_body bd; // static: off the USB task stack
    if (slot_read(idx, nm, bd)) {
        valid = 1;
        cfg_version = bd.config_version;
        memcpy(name_out, nm, SLOT_NAME_LEN);
    } else {
        valid = 0;
        cfg_version = 0;
        memset(name_out, 0, SLOT_NAME_LEN);
    }
    return true;
}

// If the NEW slots sector has no valid records but the legacy (btstack-colliding)
// one has any - tolerant reader: v2 + all known v1 layouts - rebuild here. Records
// the TLV bank already clobbered fail CRC and are skipped.
static void migrate_legacy_slots() {
    bool new_has = false;
    uint8_t nm[SLOT_NAME_LEN]; static Config_body bd;
    for (uint8_t i = 0; i < SLOT_COUNT && !new_has; ++i)
        new_has = slot_read_at(SLOTS_FLASH_OFFSET, i, nm, bd);
    if (new_has) return;
    uint8_t migrated = 0;
    memset(slot_sector_image, 0xff, FLASH_SECTOR_SIZE);
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        if (!slot_read_at(LEGACY_SLOTS_FLASH_OFFSET, i, nm, bd)) continue;
        SlotRecordV2 rec{};
        rec.magic = SLOT_MAGIC_V2;
        rec.body_len = (uint16_t)sizeof(Config_body);
        memcpy(rec.name, nm, SLOT_NAME_LEN);
        rec.body = bd;
        rec.crc32 = crc32(rec.name, (uint32_t)SLOT_NAME_LEN + sizeof(Config_body));
        memcpy(slot_sector_image + i * SLOT_STRIDE, &rec, sizeof(rec));
        migrated++;
    }
    if (migrated) {
        slot_op_offset = slot_sector_offset(0);
        if (flash_safe_execute(slots_flash_op, nullptr, 1000) == PICO_OK)
            printf("[Config] migrated %u profile slot(s) from legacy sector\n", migrated);
    }
}

void set_config(const Config_body &new_config) {
    config.body = new_config;
    config_valid();
}
