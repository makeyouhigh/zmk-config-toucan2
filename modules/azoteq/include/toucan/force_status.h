/* SPDX-License-Identifier: MIT */
#pragma once
#include <toucan/force_levels.h>

/* Three display-only ABS events: two 13-bit values and a 5-bit generation.
 * All parts must arrive before publishing a snapshot. No mouse sync reports. */
#define TOUCAN_FORCE_STATUS_CODE 0x30
#define TOUCAN_FORCE_STATUS_PARTS 3
#define TOUCAN_FORCE_SYS_LAYER 7
_Static_assert(TOUCAN_FORCE_LOCK_MAX+TOUCAN_FORCE_MOVING_OFFSET<=8191 &&
               TOUCAN_FORCE_PRESS_MAX+TOUCAN_FORCE_MOVING_OFFSET<=8191 &&
               TOUCAN_FORCE_RELEASE_MAX<=8191,"Force settings exceed display packet width");
struct toucan_force_status_rx {
    uint16_t values[6];
    uint8_t generation, mask;
};
static inline int32_t toucan_force_status_pack(const struct toucan_force_levels *v,
                                               uint8_t generation, unsigned part) {
    const uint16_t values[]={v->lock,v->press,v->release,v->moving_lock,v->moving_press,0};
    return (int32_t)(((uint32_t)(generation & 31) << 26) |
                     ((uint32_t)values[part*2+1] << 13) | values[part*2]);
}
static inline bool toucan_force_status_receive(struct toucan_force_status_rx *rx,
    uint16_t code, int32_t value, struct toucan_force_levels *out) {
    if (code < TOUCAN_FORCE_STATUS_CODE || code >= TOUCAN_FORCE_STATUS_CODE+3 || value < 0) return false;
    uint8_t generation=(uint32_t)value >> 26;
    if (!generation) return false;
    if (generation != rx->generation) { rx->generation=generation; rx->mask=0; }
    unsigned part=code-TOUCAN_FORCE_STATUS_CODE;
    rx->values[part*2]=(uint32_t)value & 8191;
    rx->values[part*2+1]=((uint32_t)value >> 13) & 8191;
    rx->mask |= 1u << part;
    if (rx->mask != 7) return false;
    rx->mask=0;
    struct toucan_force_levels v={rx->values[0],rx->values[1],rx->values[2],rx->values[3],rx->values[4]};
    if (rx->values[5] || !toucan_force_levels_valid(&v)) return false;
    *out=v;
    return true;
}
void toucan_force_status_request(void);
void toucan_force_status_pending(void);
bool toucan_force_status_get(struct toucan_force_levels *out, uint32_t *revision);
