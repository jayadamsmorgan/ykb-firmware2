#include <lib/keyboard/kb_settings.h>

#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>
#include <zephyr/toolchain.h>

#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(kb_settings, CONFIG_YKB_FIRMWARE_LOG_LEVEL);

// clang-format off
#define KB_EXPAND(x) x
#define KB_CONCAT(n1, n2, n3) <KB_EXPAND(n1)KB_EXPAND(n2)KB_EXPAND(n3)>
// Include the right mapping based on the board
#include KB_CONCAT(lib/keyboard/mappings/, CONFIG_BOARD, .h)
// clang-format on

#define DEF_MAP default_mappings
BUILD_ASSERT(sizeof DEF_MAP != 0, "Mappings are empty.");
BUILD_ASSERT((sizeof DEF_MAP / sizeof DEF_MAP[0]) % CONFIG_KB_KEY_COUNT == 0,
             "Mappings do not align with key count");
BUILD_ASSERT((sizeof DEF_MAP / sizeof DEF_MAP[0]) / CONFIG_KB_KEY_COUNT <=
                 CONFIG_KB_MAX_LAYERS_SUPPORTED,
             "Mapping's layers exceeds maximum layers supported.");

BUILD_ASSERT(CONFIG_KB_SETTINGS_DEFAULT_MINIMUM <
                 CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM,
             "Default value for key not pressed should be less than default "
             "value for key pressed fully.");

static kb_settings_t settings = {0};

#if CONFIG_EEPROM

static const struct device *const eeprom = DEVICE_DT_GET(DT_PATH(eeprom));

static uint16_t crc16(const uint8_t *data, size_t length) {

    uint16_t crc = 0xFFFF;

    if (!data || length == 0) {
        return crc;
    }

    const uint16_t polynomial = 0xA001;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

struct eeprom_kb_settings_pack {
    uint16_t crc16;
    kb_settings_t settings;
};

static int kb_settings_load_eeprom() {

    if (!device_is_ready(eeprom)) {
        LOG_ERR("Unable to read keyboard settings from EEPROM: EEPROM device "
                "is not ready");
        return -1;
    }

    struct eeprom_kb_settings_pack pack = {0};
    uint16_t blank_crc =
        crc16((const uint8_t *)&pack.settings, sizeof(kb_settings_t));

    int err =
        eeprom_read(eeprom, 0, &pack, sizeof(struct eeprom_kb_settings_pack));
    if (err) {
        LOG_ERR("Unable to read keyboard settings from EEPROM: EEPROM read "
                "error %d",
                err);
        return -2;
    }

    uint16_t crc =
        crc16((const uint8_t *)&pack.settings, sizeof(kb_settings_t));

    if (pack.crc16 == 0 && crc == blank_crc) {
        LOG_INF("First time boot detected.");
        return 1;
    }

    if (pack.crc16 != crc) {
        LOG_ERR("Unable to read keyboard settings from EEPROM: CRC16 do not "
                "match.");
        return -3;
    }

    memcpy(&settings, &pack.settings, sizeof(kb_settings_t));

    LOG_INF("Successfully loaded keyboard settings from EEPROM");

    return 0;
}

static void kb_settings_save_eeprom() {
    struct eeprom_kb_settings_pack pack = {
        .crc16 = crc16((const uint8_t *)&settings, sizeof(kb_settings_t)),
        .settings = settings,
    };
    int res =
        eeprom_write(eeprom, 0, &pack, sizeof(struct eeprom_kb_settings_pack));
    if (res) {
        LOG_ERR("Unable to write keyboard settings to EEPROM: %d", res);
    }
}

#endif // CONFIG_EEPROM

#if CONFIG_EEPROM
#define SETTINGS_LOAD() kb_settings_load_eeprom()
#define SETTINGS_SAVE() kb_settings_save_eeprom()
#else
#define SETTINGS_LOAD() 1
#define SETTINGS_SAVE()
#endif // CONFIG_EEPROM

static void kb_settings_load_default() {
    settings.key_polling_rate = CONFIG_KB_SETTINGS_DEFAULT_POLLING_RATE;
    for (size_t i = 0; i < CONFIG_KB_KEY_COUNT; ++i) {
        settings.key_thresholds[i] = CONFIG_KB_SETTINGS_DEFAULT_THRESHOLD;
        settings.minimums[i] = CONFIG_KB_SETTINGS_DEFAULT_MINIMUM;
        settings.maximums[i] = CONFIG_KB_SETTINGS_DEFAULT_MAXIMUM;
    }
    settings.layer_count = MAPPINGS_KB_LAYER_COUNT;
    settings.layer_index = 0;
    settings.mode = KB_MODE_NORMAL;
    memcpy(settings.mappings, DEF_MAP, sizeof(DEF_MAP));
}

int kb_settings_init() {

    int res;

    res = SETTINGS_LOAD();
    if (res) {
        kb_settings_load_default();
    }

    return 0;
}

kb_settings_t *kb_settings_get() {
    return &settings;
}

void kb_settings_save() {
    SETTINGS_SAVE();
}
