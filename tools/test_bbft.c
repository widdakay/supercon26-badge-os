// Host tests: packet fixtures are also decoded with upstream's Python formats.
#include <assert.h>
#include <stdio.h>
#include "../main/bbft.h"

int main(void)
{
    bbft_beacon_t b = {.hdr = bbft_header(BBFT_BEACON), .seq = 0x12345678};
    memcpy(b.name, "0123456789abcdef", 16); // full-width name, no terminator on wire
    assert(bbft_type(&b, sizeof b) == BBFT_BEACON);
    for (size_t n = 0; n < sizeof b; n++) assert(!bbft_type(&b, n));
    b.hdr.version = 1;
    assert(!bbft_type(&b, sizeof b));
    b.hdr.version = 2;
    memcpy(b.hdr.magic, "BOSR", 4);
    assert(!bbft_type(&b, sizeof b));
    b.hdr = bbft_header(BBFT_BEACON);

    bbft_range_t r = {.hdr = bbft_header(BBFT_RANGE), .to = {1, 2, 3, 4, 5, 6},
        .range = {.status = 0, .entries = 16, .rssi = -71, .dist_cm = 450,
                  .rtt_raw_ns = 30, .rtt_est_ns = 29, .rtt_med_ps = 30000,
                  .rtt_mean_ps = 30001, .rtt_min_ps = 25000, .rtt_max_ps = 35000}};
    assert(bbft_type(&r, sizeof r) == BBFT_RANGE);
    for (size_t n = 0; n < sizeof r; n++) assert(!bbft_type(&r, n));
    uint8_t sync[14] = {'B', 'B', 'F', 'T', 2, BBFT_SYNC};
    assert(bbft_type(sync, sizeof sync) == BBFT_SYNC);
    assert(!bbft_type(sync, 13));
    sync[5] = 3;
    assert(!bbft_type(sync, sizeof sync));

    bbft_history_t h = {0};
    assert(bbft_distance(&h, 190, 1) == -1);
    bbft_record(&h, &r.range, 1);
    assert(bbft_distance(&h, 0, 1) == 450);
    assert(bbft_distance(&h, 190, 1) == 260);
    // Fall back to dist_cm when per-frame mean is absent, and median across sessions.
    r.range.rtt_mean_ps = 0;
    r.range.dist_cm = 1000;
    bbft_record(&h, &r.range, 10000001);
    assert(bbft_distance(&h, 0, 10000001) == 725);
    r.range.dist_cm = 600;
    bbft_record(&h, &r.range, 20000001);
    assert(bbft_distance(&h, 0, 20000001) == 600);
    r.range.status = 1;
    bbft_record(&h, &r.range, 21000001);
    assert(h.n == 3 && h.at[0] == 20000001);
    assert(bbft_distance(&h, 0, 30000001) == 800); // oldest sample has expired
    assert(bbft_distance(&h, 0, 40000001) == 600);
    assert(bbft_distance(&h, 0, 50000001) == -1);
    assert(bbft_distance(&h, 1000, 20000001) == 5); // calibration floor
    h = (bbft_history_t){0};
    r.range.status = 0;
    r.range.dist_cm = UINT32_MAX;
    bbft_record(&h, &r.range, 1);
    assert(bbft_distance(&h, 0, 1) == INT32_MAX);
    r.range.dist_cm = 40000;
    bbft_record(&h, &r.range, 2);
    bbft_record(&h, &r.range, 3);
    assert(bbft_distance(&h, 0, 3) == 40000); // no old int16 wraparound

    // Binary fixtures on stdout, assertions and sanitizers must stay silent.
    r.range = (bbft_result_t){.status = 0, .entries = 16, .rssi = -71, .dist_cm = 450,
        .rtt_raw_ns = 30, .rtt_est_ns = 29, .rtt_med_ps = 30000,
        .rtt_mean_ps = 30001, .rtt_min_ps = 25000, .rtt_max_ps = 35000};
    assert(fwrite(&b, sizeof b, 1, stdout) == 1);
    assert(fwrite(&r, sizeof r, 1, stdout) == 1);
    return 0;
}
