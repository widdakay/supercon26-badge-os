// Radar: BBFT v2 badges and C3 nodes nearby, by name/AP MAC id and distance (OS ranging, Wi-Fi FTM).
// With 2+ measured badges (3 with this one) it also draws a 2D map centred on this badge, fitted to every shared distance.
// No compass: the map's rotation and mirror are arbitrary (kept steady between frames).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "badge.h"

#define NODES 9        // me + up to 8 badges
#define MAP_X 384      // map area: x MAP_X..W, y TOP..H
#define MAP_R 270      // px from the map centre to the outer ring
#define SNAP 6         // px grid for dots: small wobble in the fit does not redraw the panel

typedef struct { uint8_t mac[6]; float x, y; } node_t;

static node_t nd[NODES];   // [0] = me at (0, 0); positions in metres, kept between frames
static int nn;
static float dist[NODES][NODES]; // metres, < 0 unknown

// What a badge is called on screen: its username if known (mine from Settings), else its 4-hex id. The MAC stays the key.
static void id_of(const uint8_t *mac, char *s)
{
    uint8_t me[6];
    ranging_me(me);
    if (memcmp(mac, me, 6) ? !ranging_name(mac, s) : cfg_get("name", s, 13) <= 0) sprintf(s, "%02X%02X", mac[4], mac[5]);
}

static int find(const node_t *v, int n, const uint8_t *mac)
{
    for (int i = 0; i < n; i++)
        if (!memcmp(v[i].mac, mac, 6)) return i;
    return -1;
}

// Fit positions to the known distances, me pinned at the origin. Each badge moves to the average of where each
// known distance alone would put it (a stress-majorization step); starting from last frame keeps the map steady.
// ponytail: plain fixed-iteration relaxation, fine for 9 nodes; weight edges by age if the map gets jumpy.
static void fit(void)
{
    for (int it = 0; it < 200; it++)
        for (int i = 1; i < nn; i++) {
            float sx = 0, sy = 0;
            int k = 0;
            for (int j = 0; j < nn; j++) {
                if (j == i || dist[i][j] < 0) continue;
                float dx = nd[i].x - nd[j].x, dy = nd[i].y - nd[j].y, l = hypotf(dx, dy);
                if (l < 1e-3f) dx = 0.01f * (i - j), dy = 0.01f, l = hypotf(dx, dy); // coincident: nudge apart
                sx += nd[j].x + dist[i][j] * dx / l, sy += nd[j].y + dist[i][j] * dy / l, k++;
            }
            if (k) nd[i].x = sx / k, nd[i].y = sy / k;
        }
}

static float stress(void) // sum of squared distance errors, m^2
{
    float e = 0;
    for (int i = 0; i < nn; i++)
        for (int j = i + 1; j < nn; j++)
            if (dist[i][j] >= 0) e += powf(hypotf(nd[i].x - nd[j].x, nd[i].y - nd[j].y) - dist[i][j], 2);
    return e;
}

// The fit can fold into a wrong local minimum, so also try fresh starts (each badge on its ring around me, angles
// shuffled per try) and keep the best. Last frame's layout wins unless a fresh one is clearly better: a steady map.
static void layout(const int *cm)
{
    node_t best[NODES];
    fit();
    float bs = stress();
    memcpy(best, nd, sizeof nd);
    for (int t = 1; t <= 8; t++) {
        for (int i = 1; i < nn; i++) {
            float a = (nd[i].mac[5] * 256 + nd[i].mac[4]) * 2 * (float)M_PI / 65536 + t * i * 2.4f;
            float d = cm[i] < 0 ? 1 : cm[i] / 100.0f;
            nd[i].x = d * cosf(a), nd[i].y = d * sinf(a);
        }
        fit();
        float e = stress();
        if (e < 0.8f * bs - 0.01f) bs = e, memcpy(best, nd, sizeof nd);
    }
    memcpy(nd, best, sizeof nd);
}

static void ring(int cx, int cy, int r) // dotted circle
{
    for (int a = 0; a < 360; a += 2) box(cx + r * cosf(a * (float)M_PI / 180), cy + r * sinf(a * (float)M_PI / 180), 2, 2, true);
}

static int snap(float v) { return (int)lroundf(v / SNAP) * SNAP; }

void app_radar(void)
{
    static range_t r[64];
    static float sum[NODES][NODES];
    static int cnt[NODES][NODES];
    uint8_t me[6];
    ranging_me(me);
    ui_keys(NULL, NULL, "clear ghosts", "home");
    for (;;) {
        int n = ranging_get(r, 64);

        // Nodes: me, then every badge I hear, measured ones first, nearest first; known positions carried over.
        node_t old[NODES];
        int on = nn;
        memcpy(old, nd, sizeof nd);
        nn = 1;
        memcpy(nd[0].mac, me, 6), nd[0].x = nd[0].y = 0;
        int cm[NODES] = {0}, measured = 0;
        for (int i = 0; i < n && nn < NODES; i++) {
            if (memcmp(r[i].a, me, 6) || find(nd, nn, r[i].b) >= 0) continue;
            int k = nn++;
            memcpy(nd[k].mac, r[i].b, 6);
            cm[k] = r[i].cm;
            measured += r[i].cm >= 0;
            for (; k > 1 && (cm[k - 1] < 0 ? 1e9 : cm[k - 1]) > (cm[k] < 0 ? 1e9 : cm[k]); k--) { // insertion sort
                node_t t = nd[k]; nd[k] = nd[k - 1]; nd[k - 1] = t;
                int c = cm[k]; cm[k] = cm[k - 1]; cm[k - 1] = c;
            }
        }

        for (int i = 1; i < nn; i++) { // carry positions over; a new badge starts on its ring at a MAC angle
            int o = find(old, on, nd[i].mac);
            float a = (nd[i].mac[5] * 256 + nd[i].mac[4]) * 2 * (float)M_PI / 65536, d = cm[i] < 0 ? 1 : cm[i] / 100.0f;
            if (o >= 0) nd[i].x = old[o].x, nd[i].y = old[o].y;
            else nd[i].x = d * cosf(a), nd[i].y = d * sinf(a);
        }

        // Distances between any two listed badges: mine and shared, both directions averaged.
        memset(sum, 0, sizeof sum), memset(cnt, 0, sizeof cnt);
        for (int i = 0; i < n; i++) {
            int a = find(nd, nn, r[i].a), b = find(nd, nn, r[i].b);
            if (a < 0 || b < 0 || a == b || r[i].cm < 0) continue;
            sum[a][b] += r[i].cm / 100.0f, cnt[a][b]++;
            sum[b][a] += r[i].cm / 100.0f, cnt[b][a]++;
        }
        for (int i = 0; i < nn; i++)
            for (int j = 0; j < nn; j++) dist[i][j] = cnt[i][j] ? sum[i][j] / cnt[i][j] : -1;

        memset(fb, 0, sizeof fb);
        char s[32], id[13];
        id_of(me, id);
        snprintf(s, sizeof s, "me %s", id);
        text(24, TOP + 16, 3, s, true);
        for (int i = 1; i < nn; i++) {
            id_of(nd[i].mac, id);
            text(24, TOP + 8 + 64 * i, 3, id, true); // name up to 12 chars, distance on the line below
            if (cm[i] < 0) snprintf(s, sizeof s, "...");
            else snprintf(s, sizeof s, "%4.1f m", cm[i] / 100.0f);
            text(72, TOP + 36 + 64 * i, 3, s, true);
        }
        if (nn == 1) text(24, TOP + 96, 3, "Looking...", true);

        int cx = MAP_X + (W - MAP_X) / 2, cy = TOP + (H - TOP) / 2;
        if (measured >= 2) { // 3 badges with this one
            layout(cm);
            float far = 0;
            for (int i = 1; i < nn; i++)
                if (cm[i] >= 0) far = fmaxf(far, hypotf(nd[i].x, nd[i].y));
            static const float steps[] = {0.5f, 1, 2, 5, 10, 20, 50};
            float step = 50; // ring spacing in metres: 3 rings cover the farthest badge
            for (int i = 0; i < 7; i++)
                if (3 * steps[i] >= far) { step = steps[i]; break; }
            float px = MAP_R / (3 * step);
            for (int k = 1; k <= 3; k++) {
                ring(cx, cy, MAP_R * k / 3);
                snprintf(s, sizeof s, "%g m", step * k);
                text(cx + 4, cy - MAP_R * k / 3 - 18, 2, s, true);
            }
            box(cx - 9, cy - 9, 18, 18, true);
            for (int i = 1; i < nn; i++) {
                if (cm[i] < 0) continue;
                int x = cx + snap(nd[i].x * px), y = cy + snap(nd[i].y * px);
                box(x - 7, y - 7, 14, 14, true);
                id_of(nd[i].mac, id);
                text(x + 12, y - 8, 2, id, true);
            }
            text(MAP_X, H - 24, 2, "rotation and mirror unknown", true);
        } else {
            text(MAP_X + 24, cy - 12, 3, "Map: needs 3+ badges", true);
        }
        ui_frame(1); // no change since the last frame = no refresh

        int b = btn_get(1000);
        if (b == BTN_A) ui_wipe();
        if (b == BTN_B) return; // the OS keeps ranging on every screen
    }
}
