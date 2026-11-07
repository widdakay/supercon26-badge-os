// BBFT v2, shared by widdakay/hackaday-2026-badge's badges and ftm_node.
// ESP32 uses little-endian fields, matching Python's explicit '<' struct formats.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error BBFT packed messages require a little-endian target
#endif

enum { BBFT_CHANNEL = 6, BBFT_VERSION = 2, BBFT_BEACON = 1, BBFT_RANGE = 2, BBFT_SYNC = 4 };
enum { BBFT_HISTORY = 3, BBFT_PEER_US = 10000000, BBFT_RESULT_US = 30000000 };

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t version, type;
} bbft_header_t;

typedef struct __attribute__((packed)) {
    bbft_header_t hdr;
    uint32_t seq;
    char name[16];
} bbft_beacon_t;

typedef struct __attribute__((packed)) {
    uint8_t status, entries;
    int8_t rssi;
    uint32_t dist_cm, rtt_raw_ns, rtt_est_ns;
    uint32_t rtt_med_ps, rtt_mean_ps, rtt_min_ps, rtt_max_ps;
} bbft_result_t;

typedef struct __attribute__((packed)) {
    bbft_header_t hdr;
    uint8_t to[6]; // sender AP MAC is the initiator; 'to' is the responder AP MAC
    bbft_result_t range;
} bbft_range_t;

_Static_assert(sizeof(bbft_beacon_t) == 26, "BBFT beacon: <4sBBI16s");
_Static_assert(sizeof(bbft_range_t) == 43, "BBFT range: <4sBB6sBBbIIIIIII");

static inline bbft_header_t bbft_header(uint8_t type)
{
    return (bbft_header_t){.magic = {'B', 'B', 'F', 'T'}, .version = BBFT_VERSION, .type = type};
}

// Return only supported, complete message types. Type 3 is retired (NAMES).
static inline int bbft_type(const void *data, size_t len)
{
    if (len < sizeof(bbft_header_t)) return 0;
    const uint8_t *p = data;
    if (memcmp(p, "BBFT", 4) || p[4] != BBFT_VERSION) return 0;
    size_t need = p[5] == BBFT_BEACON ? sizeof(bbft_beacon_t) :
                  p[5] == BBFT_RANGE ? sizeof(bbft_range_t) : p[5] == BBFT_SYNC ? 14 : 0;
    return need && len >= need ? p[5] : 0;
}

typedef struct {
    double cm[BBFT_HISTORY]; // uncalibrated: apply the initiator's bias only when reading
    int64_t at[BBFT_HISTORY];
    uint8_t n;
} bbft_history_t;

static inline void bbft_record(bbft_history_t *h, const bbft_result_t *r, int64_t now)
{
    if (r->status != 0) return; // FTM_STATUS_SUCCESS; a failure does not erase a good reading
    memmove(h->cm + 1, h->cm, (BBFT_HISTORY - 1) * sizeof h->cm[0]);
    memmove(h->at + 1, h->at, (BBFT_HISTORY - 1) * sizeof h->at[0]);
    h->cm[0] = r->rtt_mean_ps ? r->rtt_mean_ps * 0.0149896229 : r->dist_cm;
    h->at[0] = now;
    if (h->n < BBFT_HISTORY) h->n++;
}

static inline int32_t bbft_distance(const bbft_history_t *h, double bias_cm, int64_t now)
{
    double v[BBFT_HISTORY];
    int n = 0;
    for (int i = 0; i < h->n; i++) {
        if (now - h->at[i] >= BBFT_RESULT_US) continue;
        double cm = h->cm[i] - bias_cm;
        if (cm < 5) cm = 5;
        int j = n++;
        for (; j > 0 && v[j - 1] > cm; j--) v[j] = v[j - 1];
        v[j] = cm;
    }
    if (!n) return -1;
    double cm = n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
    return cm >= INT32_MAX - 0.5 ? INT32_MAX : (int32_t)(cm + 0.5);
}
