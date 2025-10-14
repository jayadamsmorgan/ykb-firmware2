#ifndef KB_HANDLE_COMMON_H_
#define KB_HANDLE_COMMON_H_

#include <lib/keyboard/kb_settings.h>

#include <zephyr/device.h>

#include <stdint.h>

// Fill out currently pressed keys in 'curr_down' bitmap
// Fill out current ADC values in 'values'
//
// Returns false if key polling rate did not pass yet or on error
bool get_kscan_bitmap(kb_settings_t *settings, const struct device *const kscan,
                      uint16_t *values, uint32_t *curr_down);

// Invoke 'on_event' for current backlight mode if possible
void handle_bl_on_event(uint8_t key_index, kb_settings_t *settings,
                        bool pressed, uint16_t *values);

// Clear out the HID report with zeros
void clear_hid_report();

// Fill out HID report with currently pressed keys based on 'mappings'
void build_hid_report_from_bitmap(kb_key_rules_t *mappings,
                                  kb_settings_t *settings, uint32_t *curr_down);

// Send HID report where possible
void handle_hid_report();

// Callback type for on_press/on_release
typedef void (*key_state_changed_cb)(uint8_t idx, kb_settings_t *settings);

// Go through the bitmap and invoke on_press/on_release callbacks
void edge_detection(kb_settings_t *settings, uint32_t *prev_down,
                    uint32_t *curr_down, size_t bm_size,
                    key_state_changed_cb on_press,
                    key_state_changed_cb on_release);

// Default behaviour for on_press:
//  - handle fn keystrokes
//  - handle layer switches
//
// Should be called from on_press passed to 'edge_detection' if needed
void on_press_default(kb_key_rules_t *mappings, uint16_t key_index,
                      kb_settings_t *settings);

// Default behaviour for on_release:
//  - handle fn keystrokes
//  - handle layer switches
//
// Should be called from on_release passed to 'edge_detection' if needed
void on_release_default(kb_key_rules_t *mappings, uint16_t key_index,
                        kb_settings_t *settings);

#endif // KB_HANDLE_COMMON_H_
