/*
 * ng1_extras.c - Ninja Gaiden's additions to the NESRecomp cycle host
 * (cyc_host_extras.h). Copied from ContraRecomp/src/contra_extras.c (which grew from
 * NinjaGaiden2Recomp/src/ng2_extras.c). Notes below about the HUD, the train, the
 * Waterfall, Bill/Lance and the Act 1 boss still describe NG2 and Contra until
 * Ninja Gaiden is measured (Phase 5).
 *
 * Presentation and observation only: nothing here changes what the emulated
 * machine does. Features are added one at a time, each with an Off setting.
 *
 *   Edge crop     config.ini [Game] OverscanTop/Bottom/Left/Right (default 8
 *                 each), --overscan T,B,L,R for one run. Menu: Picture.
 *
 *   No flicker    [Game] NoSpriteLimit (default 1): the NES draws at most 8
 *                 sprites per line and the game takes turns when more share
 *                 a line. This redraws the picture from the background and
 *                 every sprite in OAM, every frame (the mask comes from OAM,
 *                 never from comparing colours). [Game] SpriteOrder: where
 *                 two overlap, 0 the game's OAM order (it rotates every
 *                 frame), 1 (default) lower on screen in front, ties by X,
 *                 tile and attributes, never by slot.
 *
 *   Smooth motion [Game] PresentRate (0 = 60, or 120/180/240): extra
 *                 pictures between the NES's frames. Every scroll band (the
 *                 HUD, the playfield, each layer of the train) glides on its
 *                 own from its previous position to its current one (drawn
 *                 up to 1/60 s behind the game, like DD2 and Blades of
 *                 Steel). Steps above 8 px (a jump or a new screen) are not
 *                 glided; cinema scenes never are. The gap a band leaves at
 *                 its edge (at most 8 px) is under the edge crop.
 *                 [Game] CharGlide (default 1): characters glide too. Each
 *                 frame every sprite is matched to the same tile in the
 *                 previous frame, touching sprites are grouped into
 *                 characters, and a character glides only when 70% of its
 *                 matched pieces agree on one movement. Unsure ones stay
 *                 where the game put them on screen. 0: every sprite stays
 *                 where the game put it on screen.
 *                 Vertical scrolling glides the same way (Waterfall).
 *                 [Game] SteadyShots (default 1): Contra draws every
 *                 player shot only every other frame; a small sprite seen
 *                 blinking that way is drawn in the frames it is missing,
 *                 at its last position plus its own motion (see SteadyShot).
 *                 [Game] EmuThread (default 1): with PresentRate above 60,
 *                 the next NES frame runs on a second core while the
 *                 in-between pictures are shown (the engine's
 *                 present_from_snapshot, as Blades of Steel). Without it
 *                 (DD2 v1's way) the pacing log showed NG2's frames take
 *                 6.7 ms (median) to 10-12 ms, the pictures can only come
 *                 after that, and about a third of them were dropped:
 *                 180 gave ~110 pictures a second, unevenly spaced. With it,
 *                 in-between pictures read only what prepare() copied.
 *
 *   Screen shake  [Game] ScreenShake: 0 (default) holds the scenery still
 *                 when the game shakes it (the Act 1 boss: the playfield
 *                 jumps 4 px every other frame for ~1 s; sprites are not
 *                 shaken), 1 as the game. Sideways shakes only. The first
 *                 shaken frame shows (it is only known as a shake when the
 *                 next frame undoes it).
 *
 *   Picture size  [Game] PresentScale (default 4): the picture is always
 *                 drawn at this many times the NES size (constant, so the
 *                 window never changes size when features change).
 *
 *   --scroll-log FILE  One line per frame: the screen's scroll BANDS. A band
 *                      is a run of lines that continue one picture (same X,
 *                      next row); a new band starts at every mid-frame scroll
 *                      split: the HUD at the top, and the train stage's
 *                      layers if it has them. Each band is "line:x,y" (the
 *                      first line, its scroll X 0-511 and Y 0-479), or
 *                      "line:off" where the background is switched off.
 *                      Then sprites on screen and the busiest line's count.
 *   --oam-log FILE     The frame's 64 OAM entries (Y tile attr X, hex).
 *   --ppu-log FILE     Per frame: the 32 palette bytes (flashes, lightning),
 *                      a checksum of each 1 KB CHR window $0000-$1FFF as the
 *                      frame ended (which banks animate), and every line
 *                      where the background/sprite pattern table, 8x16
 *                      sprites or $2001 changed from the line above.
 *                      The scroll log also checks the redrawn picture: on
 *                      lines with 8 sprites or fewer, drawn in the game's
 *                      order, it must match the NES's own picture exactly
 *                      ("check" = mismatched pixels / lines compared). A
 *                      mismatch means the sprites' CHR changed mid-frame.
 */
#include "cyc_host_extras.h"
#include "cyc_core.h"
#include "cyc_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

enum {
    NES_W = 256,
    NES_H = 240,
    SCROLL_H = 480,      /* background Y, 0-479 (lower nametables from 240) */
    MAX_BANDS = 32,      /* written per frame; more are counted, not listed */
    CROP_MAX = 64,
    SCALE_MAX = 6,
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- settings ---- */
typedef struct { int top, bottom, left, right; } Crop;
#define NG1_DEFAULT_CROP { 8, 8, 8, 8 }
static Crop s_crop_saved = NG1_DEFAULT_CROP, s_crop = NG1_DEFAULT_CROP;
static int  s_unlimit_saved = 1, s_unlimit = 1;
static int  s_order_saved = 1, s_order = 1;
static int  s_scale_saved = 4, s_scale = 4;
static int  s_rate_saved, s_present_rate;          /* 0: 60 pictures a second */
static int  s_glide_saved = 1, s_glide = 1;
static int  s_thread_saved = 1, s_thread = 1;
static int  s_shake_saved = 0, s_shake = 0;       /* ScreenShake: 1 as the game, 0 (default) steady */     /* EmuThread: next frame on a 2nd core during in-between pictures */
static CycHostExtras s_extras;                     /* defined at the end */

static FILE *s_scroll_log, *s_oam_log, *s_ppu_log;
static long  s_frame;

/* Line y continues the picture of line y - 1: background on for both, same
 * X, next row (a nametable's row 239 wraps to its own row 0). */
static bool continues(int y)
{
    if (!(cyc_render_line_mask(y) & 0x08) || !(cyc_render_line_mask(y - 1) & 0x08)) return false;
    if (cyc_render_line_scroll_x(y) != cyc_render_line_scroll_x(y - 1)) return false;
    int a = cyc_render_line_scroll_y(y - 1), b = cyc_render_line_scroll_y(y);
    if (b == (a + 1) % SCROLL_H) return true;
    return a % 240 == 239 && b == a - 239;
}

static int busiest_line(void)
{
    const uint8_t *oam = cyc_render_oam();
    int most = 0;
    for (int py = 0; py < NES_H; ++py) {
        int rows = cyc_render_line_sprite16(py) ? 16 : 8, n = 0;
        for (int slot = 0; slot < 64; ++slot) {
            int r = py - (oam[slot * 4] + 1);
            if (oam[slot * 4] < 0xEF && r >= 0 && r < rows) ++n;
        }
        if (n > most) most = n;
    }
    return most;
}


/* ---- the redrawn picture ----
 * The machine's own picture, the background alone (the engine's capture,
 * with every mid-frame split and raster effect as the game drew it) and
 * every sprite from OAM on top, without the 8-per-line limit. */
static uint32_t s_native[NES_W * NES_H];
static uint32_t s_bg[NES_W * NES_H];
static uint8_t  s_bgop[NES_W * NES_H];
static uint8_t  s_at[NES_W * NES_H];      /* sprite pixel: 1 in front of the background, 2 behind */
static uint32_t s_spr[NES_W * NES_H];
static uint32_t s_comp[NES_W * NES_H];
static uint32_t s_big[NES_W * SCALE_MAX * NES_H * SCALE_MAX];
static int      s_slots[64], s_nslots;
static long     s_prepared = -1;
static bool     s_have_comp;

/* Lower on screen in front. The game rotates its sprite list every frame,
 * so ties are broken by X, tile and attributes, never by slot: the same
 * overlap draws the same way every frame. */
static bool in_front(const uint8_t *a, const uint8_t *b)
{
    if (a[0] != b[0]) return a[0] > b[0];
    if (a[3] != b[3]) return a[3] < b[3];
    if (a[1] != b[1]) return a[1] < b[1];
    return a[2] < b[2];
}

static void sprite_order(int order)
{
    const uint8_t *oam = cyc_render_oam();
    s_nslots = 0;
    for (int slot = 0; slot < 64; ++slot)
        if (oam[slot * 4] < 0xEF) s_slots[s_nslots++] = slot;
    if (order != 1) return;
    for (int i = 1; i < s_nslots; ++i) {
        int v = s_slots[i], j = i;
        while (j > 0 && in_front(oam + v * 4, oam + s_slots[j - 1] * 4)) { s_slots[j] = s_slots[j - 1]; --j; }
        s_slots[j] = v;
    }
}

static uint16_t sprite_pattern(const uint8_t *o, bool tall, uint16_t table, int row)
{
    return tall ? (uint16_t)(((o[1] & 1) ? 0x1000 : 0) + (o[1] & 0xFE) * 16 + (row >= 8 ? 16 : 0))
                : (uint16_t)(table + o[1] * 16);
}

/* Every sprite, no 8-per-line limit, in s_slots order: s_spr gets the top
 * opaque sprite pixel's colour, s_at 1 (in front) or 2 (behind the
 * background). Like the NES, the first opaque sprite takes the pixel even
 * when it is behind the background. */
static void sprite_layer(void)
{
    memset(s_at, 0, sizeof(s_at));
    const uint8_t *oam = cyc_render_oam();
    uint32_t col[16];
    for (int i = 0; i < 16; ++i) col[i] = cyc_render_color(16 + i);
    for (int py = 0; py < NES_H; ++py) {
        uint8_t mask = cyc_render_line_mask(py);
        if (!(mask & 0x10)) continue;
        bool tall = cyc_render_line_sprite16(py);
        int rows = tall ? 16 : 8;
        uint16_t table = cyc_render_line_sprite_table(py);
        uint8_t *at = s_at + py * NES_W;
        uint32_t *out = s_spr + py * NES_W;
        for (int k = 0; k < s_nslots; ++k) {
            const uint8_t *o = oam + s_slots[k] * 4;
            int r = py - (o[0] + 1);
            if (r < 0 || r >= rows) continue;
            unsigned attr = o[2];
            int row = (attr & 0x80) ? rows - 1 - r : r;
            uint16_t pattern = sprite_pattern(o, tall, table, row);
            uint8_t lo = cyc_render_chr((uint16_t)(pattern + (row & 7)));
            uint8_t hi = cyc_render_chr((uint16_t)(pattern + (row & 7) + 8));
            if (!(lo | hi)) continue;
            for (int c = 0; c < 8; ++c) {
                int px = o[3] + c;
                if (px >= NES_W) break;
                if (at[px] || (px < 8 && !(mask & 0x04))) continue;
                int bit = (attr & 0x40) ? c : 7 - c;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!v) continue;
                at[px] = (attr & 0x20) ? 2 : 1;
                out[px] = col[(attr & 3) * 4 + v];
            }
        }
    }
}

/* Background + every sprite into s_comp, sprites in the given order
 * (0 the game's, 1 lower in front). False if the background capture is
 * not ready yet (the first frame, or right after loading a state). */
static bool compose(int order)
{
    if (!cyc_render_background(s_bg)) return false;
    memcpy(s_bgop, cyc_frame_bg_opaque(), sizeof(s_bgop));
    sprite_order(order);
    sprite_layer();
    for (int i = 0; i < NES_W * NES_H; ++i)
        s_comp[i] = (s_at[i] == 1 || (s_at[i] == 2 && !s_bgop[i])) ? s_spr[i] : s_bg[i];
    return true;
}

static bool grab_native(void)
{
    int w, h;
    const uint32_t *live = cyc_render_present(&w, &h);
    if (!live || w != NES_W || h != NES_H) return false;
    memcpy(s_native, live, sizeof(s_native));
    return true;
}

/* Scroll bands on the screen (see --scroll-log). Gameplay has 1-5 (HUD,
 * playfield, the train's layers); cinema scenes use a line-by-line wave or
 * letterbox effect with dozens, and change their sprites mid-frame, so the
 * redraw leaves them exactly as the NES drew them. */
enum { CINEMA_BANDS = 9 };
static int count_bands(void)
{
    int bands = 0;
    for (int y = 0; y < NES_H; ++y) {
        bool on = cyc_render_line_mask(y) & 0x08;
        if (y == 0 || (on ? !continues(y) : (cyc_render_line_mask(y - 1) & 0x08) != 0)) ++bands;
    }
    return bands;
}


/* ---- smooth motion: scroll bands and character glide ---- */
enum { MAX_PF_BANDS = 8, SMOOTH_MAX_STEP = 8, GL_REACH = 10, GL_AGREE = 70, SPLIT_HOLD = 30, GL_MIN_PIXELS = 20 };
/* A band, and its screen-shake state: the Act 1 boss shakes the playfield
 * (not the sprites) 4 px sideways every other frame for about a second
 * (0,4,0,4... below the HUD). A step that undoes the previous one (2-8 px)
 * starts a shake; while the band only flips between its steady X (sx) and
 * the shaken one, sx stays. ScreenShake 0 draws the band at sx. */
typedef struct { int start, x; bool on; int sx, other, lastd, quiet; bool shaking; int y; } Band;
static Band   s_band[MAX_PF_BANDS], s_band_prev[MAX_PF_BANDS];
static int    s_nband, s_nband_prev;           /* 0: nothing to glide (cinema, or the first frame) */
static int    s_band_dx[MAX_PF_BANDS];
/* Each band's eased motion. The game's camera steps 1,2,1,2 px (1.5 px a
 * frame), the train's sky 1,0,1,0: drawn as is, the speed changes 30 times
 * a second, which fine patterns (the fences) show as shimmer. The picture
 * instead moves at the band's average speed over the last 4 frames, pulled
 * gently (15% a frame) toward one step behind the game, never past the
 * game's own position, and at most 8 px behind (the edge crop). Measured on
 * the recorded play: the speed's frame-to-frame change drops from ~0.75 to
 * ~0.13 px, at most ~3 px behind. */
enum { EASE_N = 4 };
typedef struct { double x, d0, v; double hist[EASE_N]; bool valid; } BandMotion;
static BandMotion s_bm[MAX_PF_BANDS], s_bm_prev[MAX_PF_BANDS];
/* Up and down, the same way (Contra's Waterfall: the camera climbs after
 * the player with the game's own slowing steps 3,3,2,2,2,1,1,0,1,0 px and
 * wraps at 240; drawn as is, the scenery moved at 60 Hz while everything
 * else glided). + = the camera moved down (the picture moves up). */
static BandMotion s_bmy[MAX_PF_BANDS], s_bmy_prev[MAX_PF_BANDS];
static int        s_band_dy[MAX_PF_BANDS];
static const double EASE_GAIN = 0.15;

static void ease_band(BandMotion *m, double d, bool jump)
{
    if (jump || !m->valid) {
        m->x = 0;
        m->d0 = jump ? 0 : -d;                     /* new: one step behind, at speed */
        for (int i = 0; i < EASE_N; ++i) m->hist[i] = jump ? 0 : d;
        m->valid = true;
        if (jump) { m->v = 0; return; }
    } else {
        m->d0 += m->v;                             /* where last frame's pictures ended */
    }
    m->x += d;
    for (int i = 0; i < EASE_N - 1; ++i) m->hist[i] = m->hist[i + 1];
    m->hist[EASE_N - 1] = d;
    double avg = 0;
    for (int i = 0; i < EASE_N; ++i) avg += m->hist[i];
    avg /= EASE_N;
    double e = m->x - m->d0;                       /* how far the picture is behind the game */
    double v = avg + EASE_GAIN * (e - avg);
    if (e >= 0) v = v < 0 ? 0 : v > e ? e : v;    /* never past the game's position */
    else        v = v > 0 ? 0 : v < e ? e : v;
    if (e - v > SMOOTH_MAX_STEP) v = e - SMOOTH_MAX_STEP;   /* never more than 8 px behind */
    if (e - v < -SMOOTH_MAX_STEP) v = e + SMOOTH_MAX_STEP;
    m->v = v;
}

/* How far band b's picture is shifted (native px, + = drawn further right)
 * at this picture's phase. */
static double band_offset(int b, double phase)
{
    if (b < 0) return 0;
    const BandMotion *m = &s_bm[b];
    if (!m->valid) return 0;
    double off = m->x - (m->d0 + m->v * phase);
    return off > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : off < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : off;
}
/* The same up/down (+ = drawn further down). */
static double band_offset_y(int b, double phase)
{
    if (b < 0) return 0;
    const BandMotion *m = &s_bmy[b];
    if (!m->valid) return 0;
    double off = m->x - (m->d0 + m->v * phase);
    return off > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : off < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : off;
}
static int s_cam_band = -1;
static int step_x(int d);
/* ScreenShake 0: how far band b is shaken off its steady position. */
static int shake_off(int b)
{
    if (s_shake || b < 0 || !s_band[b].on) return 0;
    return step_x(s_band[b].x - s_band[b].sx);
}
static bool shake_active(void)
{
    for (int b = 0; b < s_nband; ++b) if (shake_off(b)) return true;
    return false;
}
static int8_t s_line_band[NES_H];               /* band of each line, -1 none */
static int    s_cam_dx;                         /* the playfield's (largest band's) step */
static int    s_cam_dy;                         /* the same, up/down */
static double s_phase;                          /* how far into the frame this picture is, 0-1 */

static int wrap_step(int d, int span) { d %= span; if (d < 0) d += span; return d >= span / 2 ? d - span : d; }
static int step_x(int d)   /* across 512, or 256 for a band that wraps there: the shorter */
{
    int a = wrap_step(d, 512), b = wrap_step(d, 256);
    return abs(b) < abs(a) ? b : a;
}

/* Screen shake, per screen line (a band can exist only while shaken: the
 * boss's playfield lines up with the HUD at rest, so its split appears only
 * on the shaken frames). Each line's X is followed every frame; a step that
 * undoes the previous one (2-8 px) starts a shake, and the line's steady X
 * stays where the line returned to while it only flips between the two. */
static int     s_line_px[NES_H], s_line_d[NES_H], s_line_sx[NES_H], s_line_sx_prev[NES_H];
static int     s_line_other[NES_H], s_line_quiet[NES_H];
static bool    s_line_shaking[NES_H], s_line_entering[NES_H], s_line_have;

static void shake_lines(void)
{
    for (int y = 0; y < NES_H; ++y) {
        int x = cyc_render_line_scroll_x(y);
        s_line_sx_prev[y] = s_line_sx[y];
        s_line_entering[y] = false;
        if (!s_line_have) { s_line_px[y] = s_line_sx[y] = s_line_sx_prev[y] = x; s_line_d[y] = 0; s_line_shaking[y] = false; continue; }
        int rd = step_x(x - s_line_px[y]);
        if (s_line_shaking[y]) {
            if (x == s_line_other[y]) s_line_quiet[y] = 0;
            else if (x == s_line_sx[y]) { if (++s_line_quiet[y] > 60) s_line_shaking[y] = false; }
            else { s_line_shaking[y] = false; s_line_sx[y] = x; }
        } else if (s_line_d[y] && rd == -s_line_d[y] && abs(rd) >= 2 && abs(rd) <= SMOOTH_MAX_STEP) {
            s_line_shaking[y] = s_line_entering[y] = true;
            s_line_other[y] = s_line_px[y];
            s_line_sx[y] = x;                      /* where it came back to */
            s_line_quiet[y] = 0;
        } else {
            s_line_sx[y] = x;
        }
        s_line_d[y] = rd;
        s_line_px[y] = x;
    }
    s_line_have = true;
}

enum { HUD_SPLIT_LINE = 56 };
static bool s_hud_split_seen;

static void bands_frame(void)
{
    shake_lines();
    memcpy(s_band_prev, s_band, sizeof(s_band));
    memcpy(s_bm_prev, s_bm, sizeof(s_bm));
    memset(s_bm, 0, sizeof(s_bm));
    memcpy(s_bmy_prev, s_bmy, sizeof(s_bmy));
    memset(s_bmy, 0, sizeof(s_bmy));
    s_cam_dy = 0;
    s_cam_band = -1;
    s_nband_prev = s_nband;
    s_nband = 0;
    s_cam_dx = 0;
    memset(s_line_band, -1, sizeof(s_line_band));
    /* A split the game really makes (the HUD at line 48, the train's
     * layers) can look continuous for a frame when both sides happen to
     * have the same scroll (the camera passing X 0 lines the playfield up
     * with the HUD: every 512 px of running, 237 times in the recorded
     * play). Merged, the playfield's smoothing restarted: a jump. So a
     * split seen in the last SPLIT_HOLD frames is kept. */
    static uint8_t split_age[NES_H];
    static bool split_init;
    if (!split_init) { memset(split_age, 255, sizeof(split_age)); split_init = true; }
    int starts[NES_H], n = 0;
    for (int y = 0; y < NES_H; ++y) {
        bool on = cyc_render_line_mask(y) & 0x08;
        bool real = y == 0 || (on ? !continues(y) : (cyc_render_line_mask(y - 1) & 0x08) != 0);
        if (real) split_age[y] = 0;
        else if (split_age[y] < 255) ++split_age[y];
        /* Ninja Gaiden's HUD always ends at line 56 (sprite 0 hit). At rest
         * at the stage's left edge the playfield lines up with it for
         * seconds (stage starts, respawns, backing up to X 0: 56 times in
         * the recorded play over 30 frames); kept for good once seen, so the
         * camera's smoothing is already running when it starts to move. */
        if (real && y == HUD_SPLIT_LINE) s_hud_split_seen = true;
        if (real || (on && (split_age[y] <= SPLIT_HOLD || (y == HUD_SPLIT_LINE && s_hud_split_seen))))
            starts[n++] = y;
    }
    if (n > MAX_PF_BANDS || n >= CINEMA_BANDS) return;
    int biggest = -1, most = 0;
    for (int b = 0; b < n; ++b) {
        int y0 = starts[b], y1 = b + 1 < n ? starts[b + 1] : NES_H;
        bool on = cyc_render_line_mask(y0) & 0x08;
        int x = on ? cyc_render_line_scroll_x(y0) : 0;
        int yv = on ? cyc_render_line_scroll_y(y0) : 0;
        s_band[b] = (Band){ y0, x, on, s_line_sx[y0], 0, 0, 0, s_line_entering[y0], yv };
        s_band_dx[b] = 0;
        s_band_dy[b] = 0;
        for (int j = 0; j < s_nband_prev && on; ++j)
            if (s_band_prev[j].on && s_band_prev[j].start == y0) {
                /* the smoothing follows the steady position; sprites do not shake */
                int d = step_x(s_line_sx[y0] - s_line_sx_prev[y0]);
                bool jump = s_line_entering[y0] || abs(d) > SMOOTH_MAX_STEP;
                s_band_dx[b] = jump ? 0 : d;
                s_bm[b] = s_bm_prev[j];
                ease_band(&s_bm[b], s_band_dx[b], jump);
                /* up/down: the band's top line's scroll Y, across 240 */
                int dyv = wrap_step(yv - s_band_prev[j].y, 240);
                bool jumpy = jump || abs(dyv) > SMOOTH_MAX_STEP;
                s_band_dy[b] = jumpy ? 0 : dyv;
                s_bmy[b] = s_bmy_prev[j];
                ease_band(&s_bmy[b], s_band_dy[b], jumpy);
            }
        for (int y = y0; y < y1; ++y) s_line_band[y] = (int8_t)b;
        if (on && y1 - y0 > most) { most = y1 - y0; biggest = b; }
    }
    s_nband = n;
    if (biggest >= 0) { s_cam_dx = s_band_dx[biggest]; s_cam_dy = s_band_dy[biggest]; }
    s_cam_band = biggest;
}

/* Last frame's sprites (and ghosts: sprites missing for 1-2 frames, kept at
 * their predicted position, so a blinking Ryu or a flickering star is found
 * again with its motion). */
enum { GL_MAX = 128, GL_GHOST_FRAMES = 2 };
/* small: a particle (< GL_MIN_PIXELS pixels: a bullet, a spark).
 * blinker: a small sprite that has been found again after a frame missing
 * (Contra draws every player shot only every other frame: in the recorded
 * Stage 1 the rifle bullet was in OAM on alternate frames 10,356 times and
 * on two frames in a row never). */
typedef struct { int y, x; uint8_t tile, attr, age, small, blinker; } GlSprite;
static GlSprite s_gl_prev[GL_MAX];
/* Each character's eased world motion (like the bands), across and up/down:
 * the soldiers walk in uneven 1,0,1,1,0 px steps, Ryu runs 1,2,1,2, falling
 * items and jumps step unevenly too; drawn as is they hop. The state
 * follows the character from frame to frame through the sprite matches;
 * every piece of a character shares it. */
static BandMotion s_gl_prev_m[GL_MAX], s_gl_prev_my[GL_MAX];   /* parallel to s_gl_prev */
static BandMotion s_gm[64], s_gmy[64];                           /* per slot, this frame */
/* Steady shots (SteadyShots, default 1): a blinker missing this frame is
 * drawn where it would be (its last position plus its own motion), gliding
 * like the others, instead of vanishing for 1/60 s. At 240 pictures a
 * second the game's blink showed each shot for 4 pictures, hid it for 4 and
 * brought it back 6 px further on: one shot seen as a smeared pair. */
enum { SHOT_MAX = 32 };
typedef struct { int y, x; uint8_t tile, attr; BandMotion m, my; } SteadyShot;
static SteadyShot s_shot[SHOT_MAX];
static int        s_nshot;
static int  s_shots_saved = 1, s_shots = 1;
static int      s_gl_prev_n, s_gliding;
static int8_t   s_gdx[64], s_gdy[64];    /* per slot: its character's movement in the world this frame */
static bool     s_gl_ok[64];

static int gl_find(int *parent, int i) { while (parent[i] != i) i = parent[i] = parent[parent[i]]; return i; }
/* Two pieces of one character: touching, in the same palette. Ryu
 * (purple), his shadow clones (orange) and a lantern (red) can overlap but
 * are never one character. Measured: a sprite keeps its palette from frame
 * to frame (all tile matches in 6 stages, but 76 of 12,000 in the red
 * sunset stage). */
static bool touching(const uint8_t *a, const uint8_t *b, int h)
{
    return ((a[2] ^ b[2]) & 3) == 0 && abs((int)a[3] - (int)b[3]) <= 8 && abs((int)a[0] - (int)b[0]) <= h;
}

/* How far apart the found pieces' steps of character r are (the larger of
 * the X and Y ranges, px). */
enum { GL_CHAIN_SPREAD = 4 };
static int gl_find(int *parent, int i);
static int chain_spread(int *parent, int r, int nv, const bool *has, const int *mx, const int *my)
{
    int x0 = 1 << 20, x1 = -(1 << 20), y0 = 1 << 20, y1 = -(1 << 20);
    for (int i = 0; i < nv; ++i) {
        if (gl_find(parent, i) != r || !has[i]) continue;
        if (mx[i] < x0) x0 = mx[i];
        if (mx[i] > x1) x1 = mx[i];
        if (my[i] < y0) y0 = my[i];
        if (my[i] > y1) y1 = my[i];
    }
    return x1 < x0 ? 0 : (x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0);
}

static double motion_speed(const BandMotion *m)
{
    double a = 0;
    for (int i = 0; i < EASE_N; ++i) a += m->hist[i];
    return a / EASE_N;
}

/* Blades of Steel's player glide, matched in world coordinates (screen
 * position plus the playfield's step). */
static void glide_frame(bool on, int dx, int dy)
{
    const uint8_t *oam = cyc_render_oam();
    memset(s_gdx, 0, sizeof(s_gdx));
    memset(s_gdy, 0, sizeof(s_gdy));
    memset(s_gl_ok, 0, sizeof(s_gl_ok));
    memset(s_gm, 0, sizeof(s_gm));
    memset(s_gmy, 0, sizeof(s_gmy));
    s_gliding = 0;
    bool used[GL_MAX] = { false };
    bool s_solid[64];                /* per visible sprite: not a particle */
    int  s_from[64];                 /* per visible sprite: its match in s_gl_prev, or -1 */
    for (int i = 0; i < 64; ++i) { s_solid[i] = true; s_from[i] = -1; }
    int vis[64], nv = 0;
    for (int slot = 0; slot < 64; ++slot) if (oam[slot * 4] < 0xEF) vis[nv++] = slot;
    if (on) {
        int mx[64], my[64], from[64];
        bool has[64];
        for (int i = 0; i < nv; ++i) {
            const uint8_t *o = oam + vis[i] * 4;
            int bd = 1 << 30;
            has[i] = false;
            from[i] = -1;
            for (int j = 0; j < s_gl_prev_n; ++j) {
                const GlSprite *p = &s_gl_prev[j];
                if (p->tile != o[1] || ((p->attr ^ o[2]) & 0xC3)) continue;   /* same tile, flips, palette */
                int ex = o[3] - p->x + dx, ey = o[0] - p->y + dy;   /* in the world */
                if (abs(ex) > GL_REACH || abs(ey) > GL_REACH) continue;
                int d = abs(ex) + abs(ey) + p->age * 4;      /* prefer last frame's over a ghost */
                if (d < bd) {
                    bd = d; has[i] = true; from[i] = j;
                    /* a ghost with no motion yet stayed where it was seen:
                     * the step is spread over the frames it was missing */
                    int span = p->age && !s_gl_prev_m[j].valid ? p->age + 1 : 1;
                    mx[i] = ex / span;
                    my[i] = ey / span;
                }
            }
        }
        int parent[64];
        int h = cyc_render_line_sprite16(120) ? 16 : 8;
        /* Particles (snow, sparks: a sprite with only a few pixels) never
         * join a character: a snowflake drifting past a lantern made the
         * lantern take the snow's motion. They glide on their own. */
        bool solid[64];
        for (int i = 0; i < nv; ++i) {
            const uint8_t *o = oam + vis[i] * 4;
            int ln = clampi(o[0] + 1, 0, NES_H - 1), px = 0;
            bool tall = cyc_render_line_sprite16(ln);
            uint16_t table = cyc_render_line_sprite_table(ln);
            for (int r = 0; r < (tall ? 16 : 8); ++r) {
                uint16_t pat = sprite_pattern(o, tall, table, r);
                for (unsigned bits = cyc_render_chr((uint16_t)(pat + (r & 7))) | cyc_render_chr((uint16_t)(pat + (r & 7) + 8)); bits; bits &= bits - 1) ++px;   /* portable popcount (MSVC) */
            }
            solid[i] = px >= GL_MIN_PIXELS;
            s_solid[i] = solid[i];
            s_from[i] = from[i];
        }
        for (int i = 0; i < nv; ++i) parent[i] = i;
        for (int i = 0; i < nv; ++i)
            for (int j = i + 1; j < nv; ++j)
                if (solid[i] && solid[j] && touching(oam + vis[i] * 4, oam + vis[j] * 4, h))
                    parent[gl_find(parent, i)] = gl_find(parent, j);
        for (int r = 0; r < nv; ++r) {
            if (gl_find(parent, r) != r) continue;
            int m = 0, top = -1, topn = 0, pieces = 0;
            for (int i = 0; i < nv; ++i) pieces += gl_find(parent, i) == r;
            for (int i = 0; i < nv; ++i) {
                if (gl_find(parent, i) != r || !has[i]) continue;
                ++m;
                int k = 0;
                for (int j = 0; j < nv; ++j)
                    if (gl_find(parent, j) == r && has[j] && mx[j] == mx[i] && my[j] == my[i]) ++k;
                if (k > topn) { topn = k; top = i; }
            }
            BandMotion bm = { 0 }, bmy = { 0 };
            double sx, sy;
            /* Trust the matched pieces only when they are most of the
             * character. Contra: Bill's running poses change his body's
             * tiles but keep the legs' tile while it shifts 6 px back under
             * him; that one piece alone said "Bill stepped 5 px back" and
             * he lurched on every running cycle. Fewer found: follow by
             * position (below), keeping the character's own speed. */
            if (m && topn * 100 >= m * GL_AGREE && m * 2 > pieces) {
                /* the character's motion state: from the agreeing pieces' last frame */
                for (int i = 0; i < nv && !bm.valid; ++i)
                    if (gl_find(parent, i) == r && has[i] && mx[i] == mx[top] && my[i] == my[top] && s_gl_prev_m[from[i]].valid) {
                        bm = s_gl_prev_m[from[i]];
                        bmy = s_gl_prev_my[from[i]];
                    }
                for (int i = 0; i < nv; ++i) if (gl_find(parent, i) == r && has[i]) used[from[i]] = true;
                sx = mx[top];
                sy = my[top];
            } else if (m * 2 > pieces && chain_spread(parent, r, nv, has, mx, my) <= GL_CHAIN_SPREAD) {
                /* Most pieces found, moving by slightly different amounts: a
                 * flexing chain, not a pose change (Contra's Waterfall boss:
                 * each arm segment wobbles back 1,1,1 then on 3-4 px, the
                 * segments out of step; one shared motion fitted none of
                 * them, so they were drawn as the game put them, snapping
                 * every frame). Each found piece glides on its own motion. */
                for (int i = 0; i < nv; ++i) {
                    if (gl_find(parent, i) != r || !has[i]) continue;
                    used[from[i]] = true;
                    BandMotion pm = s_gl_prev_m[from[i]], pmy = s_gl_prev_my[from[i]];
                    ease_band(&pm, mx[i], false);
                    ease_band(&pmy, my[i], false);
                    s_gdx[vis[i]] = (int8_t)mx[i];
                    s_gdy[vis[i]] = (int8_t)my[i];
                    s_gl_ok[vis[i]] = true;
                    s_gm[vis[i]] = pm;
                    s_gmy[vis[i]] = pmy;
                    s_gliding += mx[i] != 0 || my[i] != 0;
                }
                continue;
            } else {
                /* No agreement: the character changed its picture (a pose,
                 * the lanterns' flame, a spinning star). Follow it by
                 * position: the nearest sprite of last frame, any tile, near
                 * each piece; if they carry a motion state, it keeps gliding
                 * at that speed through the change. */
                int pieces = 0, found = 0;
                for (int i = 0; i < nv; ++i) {
                    if (gl_find(parent, i) != r) continue;
                    ++pieces;
                    const uint8_t *o = oam + vis[i] * 4;
                    int bd = 1 << 30, bj = -1;
                    for (int j = 0; j < s_gl_prev_n; ++j) {
                        const GlSprite *pv = &s_gl_prev[j];
                        if ((pv->attr ^ o[2]) & 3) continue;          /* same palette: not a neighbour's piece */
                        int ex = o[3] - pv->x + dx, ey = o[0] - pv->y + dy;
                        if (abs(ex) > GL_REACH || abs(ey) > GL_REACH) continue;
                        int d = abs(ex) + abs(ey) + pv->age * 4;
                        if (d < bd) { bd = d; bj = j; }
                    }
                    if (bj >= 0 && s_gl_prev_m[bj].valid) {
                        ++found;
                        used[bj] = true;
                        if (!bm.valid) { bm = s_gl_prev_m[bj]; bmy = s_gl_prev_my[bj]; }
                    }
                }
                if (!bm.valid || found * 2 < pieces) continue;
                sx = motion_speed(&bm);
                sy = bmy.valid ? motion_speed(&bmy) : 0;
            }
            ease_band(&bm, sx, false);
            ease_band(&bmy, sy, false);
            for (int i = 0; i < nv; ++i) {
                if (gl_find(parent, i) != r) continue;
                s_gdx[vis[i]] = (int8_t)lround(sx);
                s_gdy[vis[i]] = (int8_t)lround(sy);
                s_gl_ok[vis[i]] = true;
                s_gm[vis[i]] = bm;
                s_gmy[vis[i]] = bmy;
                s_gliding += sx != 0 || sy != 0;
            }
        }
    }
    /* Next frame's list: this frame's sprites, then the ghosts. */
    GlSprite nxt[GL_MAX];
    BandMotion nm[GL_MAX], nmy[GL_MAX];
    int n = 0;
    for (int i = 0; i < nv; ++i) {
        const uint8_t *o = oam + vis[i] * 4;
        nm[n] = s_gm[vis[i]];
        nmy[n] = s_gmy[vis[i]];
        uint8_t small = on && !s_solid[i], blink = 0;
        if (on && small && s_from[i] >= 0)
            blink = s_gl_prev[s_from[i]].age > 0 || s_gl_prev[s_from[i]].blinker;
        nxt[n++] = (GlSprite){ o[0], o[3], o[1], o[2], 0, small, blink };
    }
    for (int j = 0; on && j < s_gl_prev_n && n < GL_MAX; ++j) {
        const GlSprite *p = &s_gl_prev[j];
        if (used[j] || p->age >= GL_GHOST_FRAMES || (!s_gl_prev_m[j].valid && !p->small)) continue;
        GlSprite g = *p;
        g.age++;
        g.x += (int)lround(motion_speed(&s_gl_prev_m[j])) - dx;   /* where it would be on screen now */
        g.y += (s_gl_prev_my[j].valid ? (int)lround(motion_speed(&s_gl_prev_my[j])) : 0) - dy;
        nm[n] = s_gl_prev_m[j];
        nm[n].d0 += nm[n].v;                                     /* its pictures went on gliding */
        nm[n].x += motion_speed(&nm[n]);
        nmy[n] = s_gl_prev_my[j];
        if (nmy[n].valid) { nmy[n].d0 += nmy[n].v; nmy[n].x += motion_speed(&nmy[n]); }
        nxt[n++] = g;
    }
    /* Steady shots: blinkers missing this frame (ghosts just made), with a
     * motion, at most 8 px behind (the glide's limit) */
    s_nshot = 0;
    for (int k = nv; on && k < n && s_nshot < SHOT_MAX; ++k)
        if (nxt[k].age == 1 && nxt[k].small && nxt[k].blinker && nm[k].valid &&
            nxt[k].y < 0xEF && nxt[k].x > -8 && nxt[k].x < NES_W)
            s_shot[s_nshot++] = (SteadyShot){ nxt[k].y, nxt[k].x, nxt[k].tile, nxt[k].attr, nm[k], nmy[k] };
    memcpy(s_gl_prev, nxt, sizeof(GlSprite) * (size_t)n);
    memcpy(s_gl_prev_m, nm, sizeof(BandMotion) * (size_t)n);
    memcpy(s_gl_prev_my, nmy, sizeof(BandMotion) * (size_t)n);
    s_gl_prev_n = n;
}

static void smooth_reset(void)
{
    s_nband = s_nband_prev = 0;
    s_line_have = false;
    memset(s_bm, 0, sizeof(s_bm));
    s_cam_band = -1;
    s_gl_prev_n = 0;
    s_nshot = 0;
    memset(s_bmy, 0, sizeof(s_bmy));
    s_cam_dy = 0;
    memset(s_gl_prev_m, 0, sizeof(s_gl_prev_m));
    memset(s_gl_prev_my, 0, sizeof(s_gl_prev_my));
    s_cam_dx = 0;
    s_phase = 0;
}

/* The frame's sprites and the PPU state they need, copied when the picture
 * is prepared, for the in-between pictures. */
static uint8_t  s_snap_oam[256], s_snap_chr[0x2000];
static uint8_t  s_snap_mask[NES_H], s_snap_tall[NES_H];
static uint16_t s_snap_table[NES_H];
static uint32_t s_snap_col[16];
static uint8_t  s_big_op[NES_W * SCALE_MAX * NES_H * SCALE_MAX];
static uint8_t  s_taken[NES_W * SCALE_MAX * NES_H * SCALE_MAX];

static void snapshot_sprites(void)
{
    memcpy(s_snap_oam, cyc_render_oam(), sizeof(s_snap_oam));
    for (int a = 0; a < 0x2000; ++a) s_snap_chr[a] = cyc_render_chr((uint16_t)a);
    for (int y = 0; y < NES_H; ++y) {
        s_snap_mask[y] = cyc_render_line_mask(y);
        s_snap_tall[y] = cyc_render_line_sprite16(y);
        s_snap_table[y] = cyc_render_line_sprite_table(y);
    }
    for (int i = 0; i < 16; ++i) s_snap_col[i] = cyc_render_color(16 + i);
}

static int floordiv(int a, int k) { return a >= 0 ? a / k : -((-a + k - 1) / k); }

/* An in-between picture (or the frame's own, phase 0): every band shifted
 * back by the part of its step still to come, then every sprite over it. */
static void draw_smooth(uint32_t *out, int ow, int oh, int k, Crop c)
{
    double q = 1.0 - s_phase;
    int prev_src = -1, prev_ko = 0;
    for (int y = 0; y < oh; ++y) {
        uint32_t *o = out + (size_t)y * (size_t)ow;
        uint8_t *bo = s_big_op + (size_t)y * (size_t)ow;
        int yo = c.top + y / k;                    /* the screen line this row is on */
        int b = s_line_band[yo];
        /* up/down: the row shows the band's line koy/k above it (trailing
         * the camera, at most 8 px: under the edge crop), kept inside the band */
        int koy = s_present_rate > 60 && b >= 0 ? (int)lround(band_offset_y(b, s_phase) * k) : 0;
        int yn = floordiv(c.top * k + y - koy, k);
        if (b >= 0) {
            int y1 = b + 1 < s_nband ? s_band[b + 1].start : NES_H;
            yn = clampi(yn, s_band[b].start, y1 - 1);
        }
        yn = clampi(yn, 0, NES_H - 1);
        double off = (s_present_rate > 60 ? band_offset(b, s_phase) : 0) + shake_off(b);
        int ko = (int)lround(off * k);
        if (y && yn == prev_src && ko == prev_ko) {   /* the same as the row above */
            memcpy(o, o - ow, (size_t)ow * sizeof(*o));
            memcpy(bo, bo - ow, (size_t)ow);
            continue;
        }
        prev_src = yn;
        prev_ko = ko;
        const uint32_t *bg = s_bg + yn * NES_W;
        const uint8_t *op = s_bgop + yn * NES_W;
        int g = c.left * k - ko;
        int sx = floordiv(g, k), rem = g - sx * k;
        for (int x = 0; x < ow; ++x) {
            int cx = sx < 0 ? 0 : sx >= NES_W ? NES_W - 1 : sx;
            o[x] = bg[cx];
            bo[x] = op[cx];
            if (++rem == k) { rem = 0; ++sx; }
        }
    }
    memset(s_taken, 0, (size_t)ow * (size_t)oh);
    for (int i = 0; i < s_nslots; ++i) {
        int slot = s_slots[i];
        const uint8_t *o = s_snap_oam + slot * 4;
        int ytop = o[0] + 1;
        int ln = clampi(ytop, 0, NES_H - 1);
        if (!(s_snap_mask[ln] & 0x10)) continue;
        bool tall = s_snap_tall[ln];
        uint16_t table = s_snap_table[ln];
        int rows = tall ? 16 : 8;
        double gx = 0, gy = 0;
        if (s_glide && s_gl_ok[slot] && s_present_rate > 60) {
            /* Its world movement, shown behind at the same eased pace as the
             * camera: Ryu running with the camera (same step) stays exactly
             * where the game puts him on screen, a still enemy moves with
             * the scenery, others in between. */
            const BandMotion *m = &s_gm[slot];
            double own = m->valid ? m->x - (m->d0 + m->v * s_phase) : s_gdx[slot] * q;
            own = own > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : own < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : own;
            gx = band_offset(s_cam_band, s_phase) - own;
            const BandMotion *my = &s_gmy[slot];
            double owny = my->valid ? my->x - (my->d0 + my->v * s_phase) : s_gdy[slot] * q;
            owny = owny > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : owny < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : owny;
            gy = band_offset_y(s_cam_band, s_phase) - owny;
        }
        int x0 = o[3] * k + (int)lround(gx * k) - c.left * k;
        int y0 = ytop * k + (int)lround(gy * k) - c.top * k;
        unsigned attr = o[2];
        for (int r = 0; r < rows; ++r) {
            int row = (attr & 0x80) ? rows - 1 - r : r;
            uint16_t pattern = sprite_pattern(o, tall, table, row);
            uint8_t lo = s_snap_chr[(pattern + (row & 7)) & 0x1FFF];
            uint8_t hi = s_snap_chr[(pattern + (row & 7) + 8) & 0x1FFF];
            if (!(lo | hi)) continue;
            for (int cc = 0; cc < 8; ++cc) {
                int bit = (attr & 0x40) ? cc : 7 - cc;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!v) continue;
                uint32_t colour = s_snap_col[(attr & 3) * 4 + v];
                for (int yy = y0 + r * k; yy < y0 + (r + 1) * k; ++yy) {
                    if (yy < 0 || yy >= oh) continue;
                    for (int xx = x0 + cc * k; xx < x0 + (cc + 1) * k; ++xx) {
                        if (xx < 0 || xx >= ow) continue;
                        size_t at = (size_t)yy * (size_t)ow + (size_t)xx;
                        if (s_taken[at]) continue;
                        s_taken[at] = 1;
                        if ((attr & 0x20) && s_big_op[at]) continue;
                        out[at] = colour;
                    }
                }
            }
        }
    }
    /* steady shots: behind every sprite of the frame (drawn last, s_taken) */
    for (int i = 0; s_shots && s_glide && s_present_rate > 60 && i < s_nshot; ++i) {
        const SteadyShot *sh = &s_shot[i];
        uint8_t ob[4] = { (uint8_t)sh->y, sh->tile, sh->attr, 0 };
        const uint8_t *o = ob;
        int ytop = sh->y + 1;
        int ln = clampi(ytop, 0, NES_H - 1);
        if (!(s_snap_mask[ln] & 0x10)) continue;
        bool tall = s_snap_tall[ln];
        uint16_t table = s_snap_table[ln];
        int rows = tall ? 16 : 8;
        double own = sh->m.x - (sh->m.d0 + sh->m.v * s_phase);
        own = own > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : own < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : own;
        double gx = band_offset(s_cam_band, s_phase) - own, gy = band_offset_y(s_cam_band, s_phase);
        if (sh->my.valid) {
            double owny = sh->my.x - (sh->my.d0 + sh->my.v * s_phase);
            gy -= owny > SMOOTH_MAX_STEP ? SMOOTH_MAX_STEP : owny < -SMOOTH_MAX_STEP ? -SMOOTH_MAX_STEP : owny;
        }
        int x0 = sh->x * k + (int)lround(gx * k) - c.left * k;
        int y0 = ytop * k + (int)lround(gy * k) - c.top * k;
        unsigned attr = o[2];
        for (int r = 0; r < rows; ++r) {
            int row = (attr & 0x80) ? rows - 1 - r : r;
            uint16_t pattern = sprite_pattern(o, tall, table, row);
            uint8_t lo = s_snap_chr[(pattern + (row & 7)) & 0x1FFF];
            uint8_t hi = s_snap_chr[(pattern + (row & 7) + 8) & 0x1FFF];
            if (!(lo | hi)) continue;
            for (int cc = 0; cc < 8; ++cc) {
                int bit = (attr & 0x40) ? cc : 7 - cc;
                int v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
                if (!v) continue;
                uint32_t colour = s_snap_col[(attr & 3) * 4 + v];
                for (int yy = y0 + r * k; yy < y0 + (r + 1) * k; ++yy) {
                    if (yy < 0 || yy >= oh) continue;
                    for (int xx = x0 + cc * k; xx < x0 + (cc + 1) * k; ++xx) {
                        if (xx < 0 || xx >= ow) continue;
                        size_t at = (size_t)yy * (size_t)ow + (size_t)xx;
                        if (s_taken[at]) continue;
                        s_taken[at] = 1;
                        if ((attr & 0x20) && s_big_op[at]) continue;
                        out[at] = colour;
                    }
                }
            }
        }
    }
}

static void present_phase(void *ctx, double phase)
{
    (void)ctx;
    s_phase = phase;
}

enum { MIDCHR_HOLD = 30 };
static int s_midchr_hold;   /* frames left showing the NES's own picture (mid-frame CHR) */

static int sprite_check(int *lines);

static void prepare(void)
{
    s_prepared = s_frame;
    s_have_comp = false;
    if (!grab_native()) return;
    if (count_bands() >= CINEMA_BANDS) return;           /* cinema: as the NES drew it */
    bool smooth = (s_present_rate > 60 && s_nband > 0) || shake_active();
    /* Ninja Gaiden's cinema scenes are letterboxed (few bands) but switch the
     * sprites' CHR bank mid-frame (picture sprites, then the text's font): a
     * redraw from the frame-end CHR shows font tiles over the picture. The
     * self-check (the game's order must reproduce the NES picture on lines
     * with 8 sprites or fewer) catches it; 0 mismatches on every Stage 1-1
     * frame of the route. On a mismatch, show the NES's picture for 30 frames. */
    if (s_unlimit || smooth) {
        int lines, bad = sprite_check(&lines);
        if (bad > 0) s_midchr_hold = MIDCHR_HOLD;
        else if (s_midchr_hold > 0) --s_midchr_hold;
        if (s_midchr_hold > 0) return;
    }
    if (s_unlimit || smooth) s_have_comp = compose(s_unlimit ? s_order : 0);
    if (s_have_comp && smooth) snapshot_sprites();
}

static int s_plain;   /* --plain 1: the engine's own picture, as before step 1 */

static const uint32_t *present(void *ctx, int *width, int *height)
{
    (void)ctx;
    if (s_plain) return NULL;
    if (s_prepared != s_frame) prepare();
    if (s_prepared != s_frame) return NULL;
    const uint32_t *src = s_have_comp ? s_comp : s_native;
    int k = s_scale;
    Crop c = s_crop;
    int ow = (NES_W - c.left - c.right) * k, oh = (NES_H - c.top - c.bottom) * k;
    if (ow <= 0 || oh <= 0) return NULL;
    if (s_have_comp && ((s_present_rate > 60 && s_nband > 0) || shake_active())) {
        draw_smooth(s_big, ow, oh, k, c);
        *width = ow;
        *height = oh;
        return s_big;
    }
    for (int y = 0; y < oh; ++y) {
        const uint32_t *row = src + (c.top + y / k) * NES_W + c.left;
        uint32_t *out = s_big + (size_t)y * (size_t)ow;
        for (int x = 0; x < ow; ++x) out[x] = row[x / k];
    }
    *width = ow;
    *height = oh;
    return s_big;
}

/* The redrawn picture in the game's own order must equal the NES's picture
 * on every line with 8 sprites or fewer. Returns the mismatched pixels;
 * *lines gets the number of lines compared. */
static int sprite_check(int *lines)
{
    *lines = 0;
    if (!grab_native() || !compose(0)) return -1;
    const uint8_t *oam = cyc_render_oam();
    int bad = 0;
    for (int py = 0; py < NES_H; ++py) {
        int rows = cyc_render_line_sprite16(py) ? 16 : 8, n = 0;
        for (int slot = 0; slot < 64; ++slot) {
            int r = py - (oam[slot * 4] + 1);
            if (oam[slot * 4] < 0xEF && r >= 0 && r < rows) ++n;
        }
        if (n > 8) continue;
        ++*lines;
        for (int x = 0; x < NES_W; ++x)
            bad += (s_comp[py * NES_W + x] & 0xFFFFFFu) != (s_native[py * NES_W + x] & 0xFFFFFFu);
    }
    s_prepared = -1;   /* s_comp now holds the check's picture */
    return bad;
}

static void write_scroll(long f)
{
    int bands = 0;
    char buf[MAX_BANDS * 20 + 16];
    size_t len = 0;
    buf[0] = 0;
    for (int y = 0; y < NES_H; ++y) {
        bool on = cyc_render_line_mask(y) & 0x08;
        bool start;
        if (y == 0) start = true;
        else if (on) start = !continues(y);
        else start = (cyc_render_line_mask(y - 1) & 0x08) != 0;   /* off after on */
        if (!start) continue;
        if (bands++ >= MAX_BANDS) continue;
        if (on)
            len += (size_t)snprintf(buf + len, sizeof buf - len, " %d:%d,%d", y,
                                    cyc_render_line_scroll_x(y), cyc_render_line_scroll_y(y));
        else
            len += (size_t)snprintf(buf + len, sizeof buf - len, " %d:off", y);
    }
    const uint8_t *oam = cyc_render_oam();
    int n = 0;
    for (int i = 0; i < 64; ++i) n += oam[i * 4] < 0xEF;
    int lines, bad = sprite_check(&lines);
    char steps[MAX_PF_BANDS * 5 + 8] = "";
    size_t sl = 0;
    for (int b = 0; b < s_nband; ++b) sl += (size_t)snprintf(steps + sl, sizeof steps - sl, "%s%d", b ? "," : "", s_band_dx[b]);
    fprintf(s_scroll_log, "%6ld  %2d  %2d  %2d %s  check %d/%d  steps %s  cam %d  glide %d\n", f, n, busiest_line(), bands, buf,
            bad, lines, sl ? steps : "-", s_cam_dx, s_gliding);
}

static void write_ppu(long f)
{
    fprintf(s_ppu_log, "%ld pal ", f);
    for (int i = 0; i < 32; ++i) fprintf(s_ppu_log, "%02x", cyc_render_palette(i));
    fprintf(s_ppu_log, " chr");
    for (int k = 0; k < 8; ++k) {
        uint32_t h = 2166136261u;                       /* FNV-1a */
        for (int i = 0; i < 1024; ++i) h = (h ^ cyc_render_chr((uint16_t)(k * 1024 + i))) * 16777619u;
        fprintf(s_ppu_log, " %08x", (unsigned)h);
    }
    fprintf(s_ppu_log, " lines");
    for (int y = 0; y < NES_H; ++y) {
        uint16_t bg = cyc_render_line_bg_table(y), sp = cyc_render_line_sprite_table(y);
        bool tall = cyc_render_line_sprite16(y);
        uint8_t m = cyc_render_line_mask(y);
        if (y > 0 && bg == cyc_render_line_bg_table(y - 1) && sp == cyc_render_line_sprite_table(y - 1)
            && tall == cyc_render_line_sprite16(y - 1) && m == cyc_render_line_mask(y - 1))
            continue;
        fprintf(s_ppu_log, " %d:%c%c%c%02x", y, bg ? 'B' : 'b', sp ? 'S' : 's', tall ? 'T' : 't', m);
    }
    fputc('\n', s_ppu_log);
}

static bool parse_crop(const char *v, Crop *out)
{
    int t, b, l, r;
    char tail;
    if (!v || sscanf(v, "%d,%d,%d,%d%c", &t, &b, &l, &r, &tail) != 4) return false;
    if (t < 0 || b < 0 || l < 0 || r < 0 || t > CROP_MAX || b > CROP_MAX || l > CROP_MAX || r > CROP_MAX)
        return false;
    out->top = t; out->bottom = b; out->left = l; out->right = r;
    return true;
}

static void load_setting(void *ctx, const char *key, const char *value)
{
    (void)ctx;
    int v = atoi(value);
    if      (!strcmp(key, "OverscanTop"))    s_crop_saved.top = clampi(v, 0, CROP_MAX);
    else if (!strcmp(key, "OverscanBottom")) s_crop_saved.bottom = clampi(v, 0, CROP_MAX);
    else if (!strcmp(key, "OverscanLeft"))   s_crop_saved.left = clampi(v, 0, CROP_MAX);
    else if (!strcmp(key, "OverscanRight"))  s_crop_saved.right = clampi(v, 0, CROP_MAX);
    else if (!strcmp(key, "NoSpriteLimit"))  s_unlimit = s_unlimit_saved = v != 0;
    else if (!strcmp(key, "SpriteOrder"))    s_order = s_order_saved = clampi(v, 0, 1);
    else if (!strcmp(key, "PresentScale"))   s_scale = s_scale_saved = clampi(v, 1, SCALE_MAX);
    else if (!strcmp(key, "PresentRate"))    s_present_rate = s_rate_saved = clampi(v, 0, 1000);
    else if (!strcmp(key, "CharGlide"))      s_glide = s_glide_saved = clampi(v, 0, 1);
    else if (!strcmp(key, "SteadyShots"))    s_shots = s_shots_saved = clampi(v, 0, 1);
    else if (!strcmp(key, "EmuThread"))      s_thread = s_thread_saved = clampi(v, 0, 1);
    else if (!strcmp(key, "ScreenShake"))    s_shake = s_shake_saved = clampi(v, 0, 1);
    s_crop = s_crop_saved;
}

static void save_settings(void *ctx, FILE *f)
{
    (void)ctx;
    fprintf(f, "OverscanTop = %d\n", s_crop_saved.top);
    fprintf(f, "OverscanBottom = %d\n", s_crop_saved.bottom);
    fprintf(f, "OverscanLeft = %d\n", s_crop_saved.left);
    fprintf(f, "OverscanRight = %d\n", s_crop_saved.right);
    fprintf(f, "NoSpriteLimit = %d\n", s_unlimit_saved);
    fprintf(f, "SpriteOrder = %d\n", s_order_saved);
    fprintf(f, "PresentScale = %d\n", s_scale_saved);
    fprintf(f, "PresentRate = %d\n", s_rate_saved);
    fprintf(f, "CharGlide = %d\n", s_glide_saved);
    fprintf(f, "SteadyShots = %d\n", s_shots_saved);
    fprintf(f, "EmuThread = %d\n", s_thread_saved);
    fprintf(f, "ScreenShake = %d\n", s_shake_saved);
}

static void power_on(void *ctx)
{
    (void)ctx;
    s_frame = 0;
    s_prepared = -1;
    smooth_reset();
    if (s_scroll_log) { fprintf(s_scroll_log, "# power on\n"); fflush(s_scroll_log); }
}

static void state_loaded(void *ctx)
{
    (void)ctx;
    s_prepared = -1;
    smooth_reset();
    if (s_scroll_log) { fprintf(s_scroll_log, "# state loaded at frame %ld\n", s_frame); fflush(s_scroll_log); }
}

static void frame_end(void *ctx)
{
    (void)ctx;
    long f = s_frame++;
    s_phase = 0;
    s_extras.present_rate = (unsigned)s_present_rate;
    bands_frame();
    glide_frame(s_nband > 0, s_cam_dx, s_cam_dy);
    static bool capture_on;
    if (!capture_on) {
        cyc_render_capture_background(true);   /* whole from the next frame */
        capture_on = true;
    }
    if (s_scroll_log) { write_scroll(f); fflush(s_scroll_log); }
    s_extras.present_from_snapshot = s_thread && s_present_rate > 60;
    if (s_present_rate > 60) prepare();   /* now, while the machine is idle: in-between
                                             pictures may be drawn while the next frame runs */
    if (s_ppu_log) write_ppu(f);
    if (s_oam_log) {
        const uint8_t *oam = cyc_render_oam();
        fprintf(s_oam_log, "%ld", f);
        for (int i = 0; i < 64; ++i)
            fprintf(s_oam_log, " %02x%02x%02x%02x", oam[i * 4], oam[i * 4 + 1], oam[i * 4 + 2], oam[i * 4 + 3]);
        fputc('\n', s_oam_log);
    }
}

/* ---- options ---- */
static const CycHostOption s_options[] = {
    { "--scroll-log", true, "FILE: the screen's scroll bands and sprite counts every frame" },
    { "--oam-log",    true, "FILE: the frame's 64 sprite entries (Y tile attr X, hex) every frame" },
    { "--ppu-log",    true, "FILE: palette, CHR window checksums and per-line PPU changes every frame" },
    { "--overscan",        true, "T,B,L,R: crop this many pixels for this run" },
    { "--no-sprite-limit", true, "0|1: draw every sprite (no flicker) for this run" },
    { "--sprite-order",    true, "0|1: overlaps in the game's order / lower on screen in front" },
    { "--present-scale",   true, "K: picture scale 1-6 (default 4)" },
    { "--present-rate",    true, "N: pictures per second, e.g. 120 or 180 (0: one per frame)" },
    { "--screen-shake",    true, "0|1: hold the screen steady when the game shakes it / shake as the game" },
    { "--emu-thread",      true, "0|1: next frame on a second core during in-between pictures" },
    { "--char-glide",      true, "0|1: characters glide between frames (with --present-rate)" },
    { "--steady-shots",    true, "0|1: shots the game blinks are drawn every frame (with --char-glide)" },
    { "--plain",           true, "0|1: 1 shows the engine's own picture (no crop, no redraw), for comparing" },
};

static bool parse_int(const char *value, int lo, int hi, int *out)
{
    char *end;
    long v = strtol(value, &end, 10);
    if (!*value || *end || v < lo || v > hi) return false;
    *out = (int)v;
    return true;
}

static bool option(void *ctx, const char *name, const char *value)
{
    (void)ctx;
    if (!strcmp(name, "--overscan"))        return parse_crop(value, &s_crop);
    if (!strcmp(name, "--no-sprite-limit")) return parse_int(value, 0, 1, &s_unlimit);
    if (!strcmp(name, "--sprite-order"))    return parse_int(value, 0, 1, &s_order);
    if (!strcmp(name, "--present-rate")) {
        if (!parse_int(value, 0, 1000, &s_present_rate)) return false;
        s_extras.present_rate = (unsigned)s_present_rate;
        return true;
    }
    if (!strcmp(name, "--screen-shake"))    return parse_int(value, 0, 1, &s_shake);
    if (!strcmp(name, "--emu-thread"))      return parse_int(value, 0, 1, &s_thread);
    if (!strcmp(name, "--char-glide"))      return parse_int(value, 0, 1, &s_glide);
    if (!strcmp(name, "--steady-shots"))    return parse_int(value, 0, 1, &s_shots);
    if (!strcmp(name, "--plain"))           return parse_int(value, 0, 1, &s_plain);
    if (!strcmp(name, "--present-scale"))   return parse_int(value, 1, SCALE_MAX, &s_scale);
    FILE **fp = !strcmp(name, "--scroll-log") ? &s_scroll_log
              : !strcmp(name, "--oam-log")    ? &s_oam_log
              : !strcmp(name, "--ppu-log")    ? &s_ppu_log : NULL;
    if (!fp) return false;
    if (*fp) fclose(*fp);
    *fp = fopen(value, "w");
    if (!*fp) { perror(value); return false; }
    if (fp == &s_scroll_log)
        fprintf(*fp, "# frame  sprites  busiestLine  bands  line:scrollX,scrollY ... (line:off = background off)\n");
    else if (fp == &s_ppu_log)
        fprintf(*fp, "# frame pal <32 bytes> chr <8 x 1KB checksums> lines <y:[Bb][Ss][Tt]mask> (B/S = $1000 table, T = 8x16)\n");
    return true;
}

/* ---- the in-game menu (recomp-ui runtime menu) ----
 * A change applies at once and is saved to config.ini [Game]. */
#ifdef CYC_WITH_RECOMP_UI
#include "recomp_runtime_ui.h"

static const char *const RATE_CHOICES[] = { "60 (off)", "120", "180", "240" };
static const int RATE_VALUES[] = { 0, 120, 180, 240 };
static const char *const ORDER_CHOICES[] = { "Game's order", "Lower on screen in front" };
static const char *const CROP_CHOICES[] = { "Off", "Standard", "More", "Custom (config.ini)" };
static const Crop CROP_PRESETS[] = { { 0, 0, 0, 0 }, { 8, 8, 8, 8 }, { 12, 12, 12, 12 } };

static const RecompRuntimeUiItem s_menu[] = {
    { "ng1.rate", "Smooth motion", "Pictures per second",
      "Extra pictures between the NES's 60 frames: the scenery and "
      "the characters glide a fraction of a pixel in each. 240 or 180 suit a 240 Hz FreeSync/G-Sync monitor.",
      RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, RATE_CHOICES, 4, RATE_VALUES },
    { "ng1.glide", "Smooth motion", "Character glide",
      "With 120/180/240 pictures per second, Ryu and the enemies glide between the game's positions "
      "instead of hopping. Off: they stay where the game put them on screen.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "ng1.shots", "Smooth motion", "Steady shots",
      "The game shows every shot only every other frame (a see-through blink on a CRT). On: a shot "
      "missing for a frame is drawn where it would be, so it flies smoothly instead of looking doubled. "
      "Needs Character glide.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "ng1.thread", "Smooth motion", "Second core",
      "Runs the next NES frame on a second CPU core while the in-between pictures are shown, so they "
      "are evenly spaced and none are skipped. Off: one core (pictures can only come after each frame).",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "ng1.shake", "Picture", "Screen shake",
      "Some bosses shake the whole screen when they land or are hit. Off: the scenery holds still "
      "(the characters are never shaken by the game). On: as the game.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "ng1.unlimit", "Picture", "No sprite flicker",
      "Draws every sprite every frame instead of the NES's 8 per line, so Ryu, the enemies "
      "and the bosses stop flickering.",
      RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, NULL, 0, NULL },
    { "ng1.order", "Picture", "Overlapping sprites",
      "Which sprite is in front where two overlap (with No sprite flicker on).",
      RECOMP_RUNTIME_UI_CHOICE, 0, 1, 1, ORDER_CHOICES, 2, NULL },
    { "ng1.crop", "Picture", "Edge crop",
      "Hides the screen edges where the NES draws scrolling garbage. Standard: 8 pixels.",
      RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, CROP_CHOICES, 4, NULL },
};

static bool key_is(const RecompRuntimeUiItem *it, const char *k) { return it->key && !strcmp(it->key, k); }

static int crop_preset(void)
{
    for (int i = 0; i < 3; ++i) {
        const Crop *c = &CROP_PRESETS[i];
        if (s_crop.top == c->top && s_crop.bottom == c->bottom && s_crop.left == c->left && s_crop.right == c->right)
            return i;
    }
    return 3;
}

static int menu_get(void *ctx, const RecompRuntimeUiItem *it, int *out)
{
    (void)ctx;
    if (key_is(it, "ng1.unlimit")) *out = s_unlimit;
    else if (key_is(it, "ng1.rate")) {
        *out = 0;
        for (int i = 0; i < 4; ++i) if (RATE_VALUES[i] == s_present_rate) *out = RATE_VALUES[i];
    }
    else if (key_is(it, "ng1.glide")) *out = s_glide;
    else if (key_is(it, "ng1.shots")) *out = s_shots;
    else if (key_is(it, "ng1.thread")) *out = s_thread;
    else if (key_is(it, "ng1.shake")) *out = s_shake;
    else if (key_is(it, "ng1.order")) *out = s_order;
    else if (key_is(it, "ng1.crop")) *out = crop_preset();
    else return 0;
    return 1;
}

static int menu_set(void *ctx, const RecompRuntimeUiItem *it, int v)
{
    (void)ctx;
    if (key_is(it, "ng1.unlimit")) { s_unlimit = s_unlimit_saved = v != 0; s_prepared = -1; }
    else if (key_is(it, "ng1.rate")) {
        s_present_rate = s_rate_saved = clampi(v, 0, 1000);
        s_extras.present_rate = (unsigned)s_present_rate;
        s_prepared = -1;
    }
    else if (key_is(it, "ng1.glide")) s_glide = s_glide_saved = v != 0;
    else if (key_is(it, "ng1.shots")) s_shots = s_shots_saved = v != 0;
    else if (key_is(it, "ng1.thread")) s_thread = s_thread_saved = v != 0;
    else if (key_is(it, "ng1.shake")) { s_shake = s_shake_saved = v != 0; s_prepared = -1; }
    else if (key_is(it, "ng1.order")) { s_order = s_order_saved = clampi(v, 0, 1); s_prepared = -1; }
    else if (key_is(it, "ng1.crop")) {
        if (v < 0 || v > 2) return 0;              /* Custom: edit config.ini */
        s_crop = s_crop_saved = CROP_PRESETS[v];
    }
    else return 0;
    return 1;
}

static int menu_enabled(void *ctx, const RecompRuntimeUiItem *it)
{
    (void)ctx;
    if (key_is(it, "ng1.order")) return s_unlimit;
    if (key_is(it, "ng1.glide") || key_is(it, "ng1.thread")) return s_present_rate > 60;
    if (key_is(it, "ng1.shots")) return s_present_rate > 60 && s_glide;
    return 1;
}

static const RecompRuntimeUiCallbacks s_menu_callbacks = {
    .get_value = menu_get, .set_value = menu_set, .is_enabled = menu_enabled,
};
#define NG1_MENU_ITEMS     s_menu
#define NG1_MENU_COUNT     (sizeof(s_menu) / sizeof(s_menu[0]))
#define NG1_MENU_CALLBACKS (&s_menu_callbacks)
#else
#define NG1_MENU_ITEMS     NULL
#define NG1_MENU_COUNT     0
#define NG1_MENU_CALLBACKS NULL
#endif

static CycHostExtras s_extras = {
    .present         = present,
    .present_phase   = present_phase,
    .load_setting    = load_setting,
    .save_settings   = save_settings,
    .power_on        = power_on,
    .frame_end       = frame_end,
    .options         = s_options,
    .option_count    = sizeof(s_options) / sizeof(s_options[0]),
    .option          = option,
    .state_loaded    = state_loaded,
    .menu_items      = NG1_MENU_ITEMS,
    .menu_item_count = NG1_MENU_COUNT,
    .menu_callbacks  = NG1_MENU_CALLBACKS,
};

const CycHostExtras *cyc_host_extras(void) { return &s_extras; }
