#ifndef WAKE_REPLY_ASSETS_H
#define WAKE_REPLY_ASSETS_H
#include <stddef.h>
#include <stdint.h>
typedef struct { size_t offset; size_t size; } wake_reply_asset_t;
extern const unsigned char wake_reply_raw_data[];
#define WAKE_REPLY_ASSETS_COUNT 8
static const wake_reply_asset_t wake_reply_assets[WAKE_REPLY_ASSETS_COUNT] = {
    { 0, 20574 },
    { 20574, 15846 },
    { 36420, 13650 },
    { 50070, 26670 },
    { 76740, 27314 },
    { 104054, 17490 },
    { 121544, 20004 },
    { 141548, 18182 },
};
#endif /* WAKE_REPLY_ASSETS_H */
