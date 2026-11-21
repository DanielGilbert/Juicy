/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 in a browser tab: the app (firmware/src/juicy.c) runs unchanged on it, live, with the sound and the
 * panel under the mouse and the computer's keyboard. platform.c includes this file instead of tests/host.c when
 * HOST_WEB is set with DINGHY_HOST. Built as freestanding WebAssembly (web/build.sh), run by web/worker.js in a worker:
 * app_main never returns, so the page talks to it through the two imports below and the memory behind web_io.
 * As on tests/host.c: one platform_poll() is one millisecond; storage is RAM, MIDI is not there. */
#include <stdint.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define HOST_NENC 7

__attribute__((import_module("env"), import_name("web_poll"))) void web_poll(void);    /* the page's turn: panel in, LEDs out, wait for real time */
__attribute__((import_module("env"), import_name("web_audio"))) void web_audio(const int16_t *frames, uint32_t n);   /* n stereo frames, L R L R */

#include "libc.c"

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static struct {                                 /* shared with web/worker.js (the same order there) */
    uint32_t notes, buttons;                    /* in: what is held now */
    int32_t enc[HOST_NENC];                     /* in: detents turned, added by the page, taken by the app */
    uint32_t leds[2], glow[2];                  /* out: bit id (0..13 buttons, 14..40 keys), low word first */
} web_io;
static uint32_t host_edges, host_rel, host_note_edges;
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_rel;
    host_rel = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_note_edges; host_note_edges = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t v = web_io.enc[e % HOST_NENC]; web_io.enc[e % HOST_NENC] = 0; return v; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void web_led(uint32_t *layer, uint32_t id, int on)
{
    if (id > 63u) return;
    if (on) layer[id >> 5] |= 1u << (id & 31u);
    else layer[id >> 5] &= ~(1u << (id & 31u));
}
static void fm1_led_key(uint32_t id, int on) { web_led(web_io.leds, id, on); }
static void fm1_led_key_glow(uint32_t id, int on) { web_led(web_io.glow, id, on); }
static void fm1_led_key_glow_bright(uint32_t id, int on) { if (on) web_led(web_io.glow, id, 1); }   /* (after fm1_led_key_glow: the page has one glow) */

/* the screen: 240 x 240 RGB565 (gfx.c sends the bytes swapped, as the ST7789 gets them) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* storage in RAM (lost when the tab reloads), store.h's sizes */
#include "store.h"
static uint8_t host_set[BLOB_MAX + 4], host_blob[NBLOB][BLOB_MAX + 4], host_slot[NSLOT][SLOT_MAX + 4];
static int host_load(uint8_t *a, uint32_t max, void *p, uint32_t n)   /* a[0] used, a[1..3] the length */
{
    if (!a[0] || n > max || (uint32_t)(a[1] | a[2] << 8 | a[3] << 16) != n) return -1;
    memcpy(p, a + 4, n);
    return 0;
}
static int host_save(uint8_t *a, uint32_t max, const void *p, uint32_t n)
{
    if (n > max) return -1;
    a[0] = 1; a[1] = (uint8_t)n; a[2] = (uint8_t)(n >> 8); a[3] = (uint8_t)(n >> 16);
    memcpy(a + 4, p, n);
    return 0;
}
static int settings_load(void *p, uint32_t n) { return host_load(host_set, BLOB_MAX, p, n); }
static int settings_save(const void *p, uint32_t n) { return host_save(host_set, BLOB_MAX, p, n); }
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? host_load(host_blob[i], BLOB_MAX, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? host_save(host_blob[i], BLOB_MAX, p, n) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? host_load(host_slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { return i < NSLOT ? host_save(host_slot[i], SLOT_MAX, p, n) : -1; }

static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2) { (void)st; (void)d1; (void)d2; return 0; }
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    static int16_t pcm[2u * HALF_FRAMES];
    fm1_ms++;
    web_poll();
    host_edges |= web_io.buttons & ~fm1_in.buttons;
    host_rel |= fm1_in.buttons & ~web_io.buttons;
    host_note_edges |= web_io.notes & ~fm1_in.notes;
    fm1_in.buttons = web_io.buttons;
    fm1_in.notes = web_io.notes;
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (the MASTER knob all the way up) */
            pcm[i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        web_audio(pcm, HALF_FRAMES);
    }
}

__attribute__((export_name("web_fb"))) uint16_t *web_fb(void) { return fb; }
__attribute__((export_name("web_io"))) void *web_io_ptr(void) { return &web_io; }
__attribute__((export_name("web_start"))) void web_start(void) { app_main(); }
