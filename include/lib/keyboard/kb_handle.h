#ifndef LIB_KB_HANDLE_H_
#define LIB_KB_HANDLE_H_

#define KB_WORD_BITS 32u

#define KB_BITMAP_WORDS_FROM_KEY_COUNT(COUNT)                                  \
    ((COUNT + KB_WORD_BITS - 1) / KB_WORD_BITS)

#define KB_BITMAP_BYTECNT_FROM_KEY_COUNT(COUNT) ((COUNT + 7) / 8)

// Size of uint32_t bitmap for current keyboard
#define KB_BITMAP_WORDS KB_BITMAP_WORDS_FROM_KEY_COUNT(CONFIG_KB_KEY_COUNT)

#define KB_BITMAP_BYTECNT KB_BITMAP_BYTECNT_FROM_KEY_COUNT(CONFIG_KB_KEY_COUNT)

#if CONFIG_BT_INTER_KB_COMM_MASTER

// Size of uint32_t bitmap for slave keyboard
#define KB_BITMAP_WORDS_SLAVE                                                  \
    KB_BITMAP_WORDS_FROM_KEY_COUNT(CONFIG_KB_KEY_COUNT_SLAVE)

// Minimal amount of bytes which can hold slave keys bitmap
#define KB_BITMAP_SLAVE_BYTECNT ((CONFIG_KB_KEY_COUNT_SLAVE + 7) / 8)

#endif // CONFIG_BT_INTER_KB_COMM_MASTER

void kb_handle();

#endif // LIB_KB_HANDLE_H_
