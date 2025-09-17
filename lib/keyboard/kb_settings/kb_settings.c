#include <lib/keyboard/kb_settings.h>

#include <zephyr/toolchain.h>

#include <stdint.h>
#include <string.h>

// clang-format off
#define EXPAND(x) x
#define CONCAT(n1, n2, n3) <EXPAND(n1)EXPAND(n2)EXPAND(n3)>
// Include the right mapping based on the board
#include CONCAT(lib/keyboard/mappings/, CONFIG_BOARD, .h)
// clang-format on

BUILD_ASSERT(sizeof mappings != 0, "Mappings are empty.");
BUILD_ASSERT((sizeof mappings / sizeof mappings[0]) % CONFIG_KB_KEY_COUNT == 0,
             "Mappings do not align with key count");
BUILD_ASSERT((sizeof mappings / sizeof mappings[0]) / CONFIG_KB_KEY_COUNT <=
                 CONFIG_KB_MAX_LAYERS_SUPPORTED,
             "Mappings layers exceeds maximum layers supported.");

static kb_settings_t settings = {0};

int kb_settings_init() {
    memcpy(settings.mappings, mappings, sizeof(mappings));
    return 0;
}
