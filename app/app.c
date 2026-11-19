/* SPDX-License-Identifier: MPL-2.0 */
/* HARMONICA: a 10-hole diatonic harmonica on the FM-1. The keyboard is the harmonica: white keys 1 to 10 are holes
 * 1 to 10. A key blows its hole; with the DRAW key held (white key 11) it draws it; white keys 12 to 14, held, bend
 * the note a half, a whole, a whole and a half step, as far as the hole allows. Neighbouring keys together are a
 * chord: one mouth, one breath, 4 neighbouring holes at most. The lights show it: the holes dim, what sounds bright,
 * DRAW and the bend in use bright while they are, dark at rest. Each setting has one control: PRESET the harmonica's key, ALGORITHM the tuning, SELECT
 * the lick (PLAY loops it until PLAY again), EDIT the sound, which KNOB 1-4 shape, MASTER the volume; ARP held closes
 * the hands (wah), SEQ held is the hand's tremolo. GLO opens SETUP, HOME the harmonica (and shows the battery for 3 s). Part of the unity build
 * (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"
#include "harmo.h"

/* ---------------------------------------------------------- the harmonicas --- */
/* Lee Oskar's tunings: each key by its label and the MIDI note of hole 1 blow; label_off 7 = labelled by the
 * 2nd-position key (hole 2 draw); the four positions as Lee Oskar's charts give them, from the labelled key */
typedef struct {
    const char *label;
    uint8_t hole1;
} hkey_t;
typedef struct {
    const char *mode;
    uint8_t off, minor;
} hpos_t;
typedef struct {
    const char *name, *shortname;
    uint8_t label_off, def_pos;
    uint8_t blow[10], draw[10];
    const hkey_t *keys;
    uint8_t nkeys;
    hpos_t pos[4];
} tuning_t;

static const hkey_t K_MAJOR[] = {{"LOW C", 48}, {"LOW D", 50}, {"LOW Eb", 51}, {"LOW E", 52}, {"LOW F", 53},
                                 {"G", 55}, {"Ab", 56}, {"A", 57}, {"Bb", 58}, {"B", 59}, {"C", 60}, {"Db", 61},
                                 {"D", 62}, {"Eb", 63}, {"E", 64}, {"F", 65}, {"F#", 66}, {"HI G", 67}};
static const hkey_t K_MELODY[] = {{"C", 53}, {"D", 55}, {"E", 57}, {"F", 58}, {"G", 60}, {"A", 62}, {"Bb", 63},
                                  {"B", 64}, {"HI C", 65}};
static const hkey_t K_NATMIN[] = {{"Dm", 55}, {"Ebm", 56}, {"Em", 57}, {"Fm", 58}, {"F#m", 59}, {"Gm", 60},
                                  {"Abm", 61}, {"Am", 62}, {"Bbm", 63}, {"Bm", 64}, {"Cm", 65}, {"C#m", 66}};
static const hkey_t K_HARMIN[] = {{"LOW Em", 52}, {"Gm", 55}, {"Abm", 56}, {"Am", 57}, {"Bbm", 58}, {"Bm", 59},
                                  {"Cm", 60}, {"C#m", 61}, {"Dm", 62}, {"Ebm", 63}, {"Em", 64}, {"Fm", 65},
                                  {"F#m", 66}};
#define NK(a) (uint8_t)(sizeof(a) / sizeof(a[0]))

static const tuning_t TUN[] = {
    {"MAJOR DIATONIC", "MAJ", 0, 1, {0, 4, 7, 12, 16, 19, 24, 28, 31, 36}, {2, 7, 11, 14, 17, 21, 23, 26, 29, 33},
     K_MAJOR, NK(K_MAJOR), {{"major", 0, 0}, {"dominant", 7, 0}, {"dorian", 2, 1}, {"minor", 9, 1}}},
    {"MELODY MAKER", "MM", 7, 1, {0, 4, 9, 12, 16, 19, 24, 28, 31, 36}, {2, 7, 11, 14, 18, 21, 23, 26, 30, 33},
     K_MELODY, NK(K_MELODY), {{"dorian", 2, 1}, {"major", 0, 0}, {"dominant", 7, 0}, {"minor", 9, 1}}},
    {"NATURAL MINOR", "NAT", 7, 1, {0, 3, 7, 12, 15, 19, 24, 27, 31, 36}, {2, 7, 10, 14, 17, 21, 22, 26, 29, 33},
     K_NATMIN, NK(K_NATMIN), {{"dorian", 5, 1}, {"minor", 0, 1}, {"major", 3, 0}, {"dominant", 10, 0}}},
    {"HARMONIC MINOR", "HAR", 0, 0, {0, 3, 7, 12, 15, 19, 24, 27, 31, 36}, {2, 7, 11, 14, 17, 20, 23, 26, 29, 32},
     K_HARMIN, NK(K_HARMIN), {{"minor", 0, 1}, {"major", 3, 0}, {"dominant", 10, 0}, {"dorian", 5, 1}}},
};
#define NTUN 4u
enum { T_MAJOR, T_MELODY, T_NATMIN, T_HARMIN };
/* each tuning's colour, as Lee Oskar's labels: a band along the bottom of the screen, and the licks' names */
static const uint16_t TUN_RGB[NTUN] = {RGB(214, 76, 52), RGB(90, 186, 240), RGB(146, 196, 66), RGB(240, 222, 64)};

/* the two notations, KNOB 1 on the SETUP page: Latin (Do Re Mi) or letters (C D E) */
static const char *const NOTE_LATIN[12] = {"Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib",
                                           "Si"};
static const char *const NOTE_ABC[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const ORD[4] = {"1st", "2nd", "3rd", "4th"};

/* ------------------------------------------------------- settings and pages --- */
enum { PR_ATTACK, PR_INDEX, PR_GRAIN, PR_NASAL, PR_BREATH, PR_VIB, PR_RATE, PR_TREM, PR_DRIVE, PR_WAHLO, PR_GLIDE,
       PR_VOL, PR_SOURCE };                         /* harmo.h hm_params_t, in its order */
enum { U_MS, U_F1, U_DB, U_PCT, U_CT, U_HZ1, U_HZ, U_SRC };
static const struct {
    const char *label;
    float lo, hi, step;
    uint8_t unit;
} PRM[HM_NPARAMS] = {
    {"Attack", 5.0f, 120.0f, 5.0f, U_MS},   {"Tone", 0.3f, 4.0f, 0.1f, U_F1},
    {"Grain", 0.0f, 2.5f, 0.1f, U_F1},       {"Nasal", 0.0f, 14.0f, 0.5f, U_DB},
    {"Breath", 0.0f, 0.4f, 0.02f, U_PCT},   {"Vibrato", 0.0f, 30.0f, 1.0f, U_CT},
    {"Vibrato rate", 2.0f, 8.0f, 0.1f, U_HZ1}, {"Tremolo depth", 0.0f, 1.0f, 0.05f, U_PCT},
    {"Drive", 0.0f, 1.0f, 0.05f, U_PCT}, {"Wah depth", 300.0f, 2000.0f, 50.0f, U_HZ},
    {"Bend speed", 10.0f, 200.0f, 5.0f, U_MS}, {"Volume", 0.0f, 1.0f, 0.05f, U_PCT},
    {"Source", 0.0f, 1.0f, 1.0f, U_SRC},
};
/* EDIT picks the sound: the first three play the recorded harmonica, the last the FM reed. KNOB 1-4 shape four of
 * its values on the home page; the others are the sound's own */
#define NPRESETS 4u
static const char *const PRESET_NAME[NPRESETS] = {"BLUES", "FOLK", "CHICAGO", "FM"};
static const hm_params_t PRESET[NPRESETS] = {
    {8.0f, 2.6f, 1.1f, 4.0f, 0.06f, 6.0f, 5.5f, 0.5f, 0.0f, 650.0f, 45.0f, 0.8f, 0.0f},
    {25.0f, 1.8f, 0.4f, 2.0f, 0.05f, 3.0f, 5.0f, 0.35f, 0.0f, 900.0f, 60.0f, 0.8f, 0.0f},
    {6.0f, 3.4f, 1.7f, 9.0f, 0.04f, 12.0f, 6.2f, 0.6f, 0.55f, 500.0f, 35.0f, 0.7f, 0.0f},
    {20.0f, 2.6f, 1.1f, 8.0f, 0.18f, 8.0f, 5.5f, 0.5f, 0.0f, 650.0f, 45.0f, 0.8f, 1.0f},
};

#define SET_MAGIC 0x36524148u            /* "HAR6": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t tuning, key, preset, page;   /* preset: NPRESETS = changed by hand */
    uint8_t notation;                    /* 0 Latin (Do Re Mi), 1 letters (C D E), the default */
    uint8_t lick;                        /* the lick PLAY plays */
    uint8_t lights;                      /* the resting keys' lights: 1 low, 2 high */
    uint8_t rsv[1];
    hm_params_t p;
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

enum { PG_HOME, PG_SETUP, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"HARMONICA", "SETUP"};
static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_GLO};
/* what KNOB 1-4 set on each page: a parameter of the sound (PR_*), or one of these */
enum { KN_NOTE = 100, KN_LIGHT, KN_NONE = 255 };
static const uint8_t PAGE_KNOB[NPAGES][4] = {
    {PR_INDEX, PR_BREATH, PR_VIB, PR_DRIVE},        /* the harmonica: its sound */
    {KN_NOTE, KN_LIGHT, KN_NONE, KN_NONE},          /* SETUP */
};

static const tuning_t *tun(void) { return &TUN[set.tuning]; }
static const hkey_t *hkey(void) { return &tun()->keys[set.key]; }
static float *param(uint32_t i) { return (float *)&set.p + i; }

/* the note of hole h (0..9), blown (draw 0) or drawn (draw 1) */
static int32_t hole_note(uint32_t draw, uint32_t h)
{
    return (int32_t)hkey()->hole1 + (int32_t)(draw ? tun()->draw[h] : tun()->blow[h]);
}
/* how far a hole's note bends: only the higher reed of the pair, to a half step above the other */
static int32_t bend_max(uint32_t draw, uint32_t h)
{
    int32_t b = tun()->blow[h], d = tun()->draw[h], m = draw ? d - b - 1 : b - d - 1;
    return m > 0 ? m : 0;
}
static const char *note_name(int32_t n) { return (set.notation ? NOTE_ABC : NOTE_LATIN)[n % 12]; }

/* the harmonica's key in the chosen notation: "LOW Eb" or "LOW Mib", "F#m" or "Fa#m" */
static void key_label(char *t)
{
    static const char *const LATIN[7] = {"La", "Si", "Do", "Re", "Mi", "Fa", "Sol"};   /* A .. G */
    const char *s = hkey()->label;
    char *d = t;
    if (!set.notation) {
        while (*s && (*s < 'A' || *s > 'G' || s[1] == 'O' || s[1] == 'I'))   /* "LOW ", "HI " as they are */
            *d++ = *s++;
        if (*s >= 'A' && *s <= 'G') {
            const char *l = LATIN[*s++ - 'A'];
            while (*l)
                *d++ = *l++;
        }
    }
    while (*s)
        *d++ = *s++;
    *d = 0;
}

/* ------------------------------------------------------------------ text --- */
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void tenths_text(float v, char *b)   /* one decimal */
{
    int32_t t = (int32_t)(v * 10.0f + (v < 0.0f ? -0.5f : 0.5f));
    char d[4] = {'.', 0, 0, 0};
    int_text(t / 10, b);
    d[1] = (char)('0' + (t < 0 ? -t : t) % 10);
    text_cat(b, d);
}

static void param_text(uint32_t i, char *b)
{
    float v = *param(i);
    b[0] = 0;
    switch (PRM[i].unit) {
    case U_MS: int_text((int32_t)(v + 0.5f), b); text_cat(b, " ms"); break;
    case U_F1: tenths_text(v, b); break;
    case U_DB: b[0] = '+'; b[1] = 0; tenths_text(v, b + 1); text_cat(b, " dB"); break;
    case U_PCT: int_text((int32_t)(v * 100.0f + 0.5f), b); text_cat(b, " %"); break;
    case U_CT: int_text((int32_t)(v + 0.5f), b); text_cat(b, " ct"); break;
    case U_HZ1: tenths_text(v, b); text_cat(b, " Hz"); break;
    case U_SRC: text_cat(b, v > 0.5f ? "FM" : "Samples"); break;
    default: int_text((int32_t)(v + 0.5f), b); text_cat(b, " Hz"); break;
    }
}

/* ------------------------------------------------------------------ audio --- */
static float gain_now;                   /* glides to MASTER: no step at a block's edge */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float buf[APP_BLOCK];
    float gain = (float)master_q12 * (1.0f / 4096.0f) * 8300000.0f;   /* MASTER, read once a block */
    uint32_t i, done = 0;
    while (done < n) {
        uint32_t m = n - done < APP_BLOCK ? n - done : APP_BLOCK;
        hm_render(buf, m);
        for (i = 0; i < m; i++) {
            int32_t s;
            gain_now += (gain - gain_now) * 0.004f;
            s = (int32_t)(buf[i] * gain_now);
            lr[2u * (done + i)] = s;
            lr[2u * (done + i) + 1u] = s;
        }
        done += m;
    }
}

void app_panic(void) { hm_panic(); }

/* ------------------------------------------------------------------- play --- */
static struct {
    uint32_t sounding;                   /* bit id: hole id sounding (blow 0..9, draw 10..19) */
    uint32_t last;                       /* what sounded last: the screen keeps it, dimmed, between notes */
    int32_t note[20];                    /* each hole's note as it last sounded, bent */
    uint8_t bend[20];                    /* and its bend, half steps down */
    uint8_t wah, trem;
    int32_t last_note;
} play = {.last_note = -1};

static struct {
    char msg[2][24];                     /* a short message over the header */
    uint16_t msg_rgb;                    /* its second part's colour (0: the theme's) */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint32_t knob_until;                 /* a knob just turned: its name and value fill the screen until then (0: no) */
    uint8_t knob;                        /* that knob: a parameter (PR_*), or OV_KEY, OV_TUNING, OV_LICK */
    uint32_t batt_until;                 /* HOME just pressed: the battery shows, top right, until then (0: no) */
    int8_t batt;                         /* the battery (plat_battery's: 0..3 bars, 4 on USB power, -1 not read yet) */
    uint8_t redraw;
} ui;
enum { OV_KEY = 200, OV_TUNING, OV_LICK };

static void knob_show(uint32_t k)        /* its name and value, large, for a moment */
{
    ui.knob = (uint8_t)k;
    ui.knob_until = plat_ms() + 1500u;
    ui.redraw = 1;
}

static void midi_send(uint32_t cin, uint32_t st, uint32_t d1, uint32_t d2)
{
    plat_midi_send(cin | st << 8 | d1 << 16 | d2 << 24, TO_USB);
}

/* hole id sounds, bent (it starts on its own pitch and glides down); already sounding, it moves to this bend */
static void hole_on(uint32_t id, uint32_t bend)
{
    int32_t own = hole_note(id / 10u, id % 10u), note = own - (int32_t)bend;
    if (play.sounding >> id & 1u) {
        midi_send(0x08u, 0x80u, (uint32_t)play.note[id], 0u);
    } else {
        play.sounding |= 1u << id;
        hm_note_on(id, own);
    }
    hm_bend(id, (float)bend);
    play.note[id] = note;
    play.bend[id] = (uint8_t)bend;
    play.last = play.sounding;
    play.last_note = note;
    midi_send(0x09u, 0x90u, (uint32_t)note, 100u);
    ui.redraw = 1;
}

static void hole_off(uint32_t id)
{
    if (!(play.sounding >> id & 1u))
        return;
    play.sounding &= ~(1u << id);
    if (play.sounding)
        play.last = play.sounding;
    hm_note_off(id);
    midi_send(0x08u, 0x80u, (uint32_t)play.note[id], 0u);
    ui.redraw = 1;
}

static void holes_off(void)              /* before the notes move (tuning, key), and before a lick */
{
    uint32_t id;
    for (id = 0; id < 20u; id++)
        hole_off(id);
    play.last = 0;                       /* the screen forgets the last note too */
}

/* The keys: white keys 1-10 the holes, white key 11 DRAW, white keys 12-14 the bends (a half, a whole, a whole and a
 * half step). The other keys do nothing. */
static const uint8_t WK[16] = PLAT_WHITE_KEYS;
#define K_DRAW WK[10]
#define K_BEND(n) WK[10u + (n)]          /* n half steps, 1..3 */
static int8_t key_role[NKEYS];           /* 0..9 a hole, 10 DRAW, 11..13 a bend of 1..3 half steps, -1 nothing */
static uint32_t mouth;                   /* bit h: hole h is in the mouth (its key held, within reach) */
static uint32_t bend_keys;               /* the bend keys held, bit n for n half steps */
static uint8_t breath;                   /* 0 blow, 1 draw (DRAW held) */

static void keys_map(void)
{
    uint32_t i;
    for (i = 0; i < NKEYS; i++)
        key_role[i] = -1;
    for (i = 0; i < 14u; i++)
        key_role[WK[i]] = (int8_t)i;
}

static uint32_t bend_now(void) { return bend_keys & 8u ? 3u : bend_keys & 4u ? 2u : bend_keys & 2u ? 1u : 0u; }

/* what the mouth, the breath and the bend keys ask for, sounded: each hole in the mouth blown or drawn, bent as far
 * as the bend keys ask and the hole allows */
static void mouth_sync(void)
{
    uint32_t id, d = bend_now();
    for (id = 0; id < 20u; id++) {
        uint32_t h = id % 10u;
        int32_t s = bend_max(id / 10u, h);
        if (s > (int32_t)d)
            s = (int32_t)d;
        if (!(mouth >> h & 1u) || id / 10u != breath)
            hole_off(id);
        else if (!(play.sounding >> id & 1u) || play.bend[id] != s)
            hole_on(id, (uint32_t)s);
    }
}

/* PLAY: the examples, played over and over until PLAY again; SELECT picks one. licks.tab holds them, as tabs:
 * tools/gen_licks.py makes licks.h. Each is written for one of the tunings, and its name shows in that tuning's
 * colour. Each step is one hole or a chord of up to four neighbours, or a rest, held ms; its first hole bends as far
 * as the step asks and the hole allows. The keys' lights show each step as a player would press it. */
typedef struct {
    uint8_t hole[4];                     /* id + 1 (blow 1..10, draw 11..20), 0: none (all four: a rest) */
    uint8_t bend;
    uint16_t ms;
} lstep_t;
#include "licks.h"
#define NLICKS (uint8_t)(sizeof LICKS / sizeof LICKS[0])
static struct {
    uint8_t on, step, sounding;
    uint8_t draw, bend;                  /* the sounding step's breath and bend */
    uint32_t next;
} demo;

static void demo_step(int on)            /* the step's holes on (bent) or off */
{
    const lstep_t *st = &LICKS[set.lick].steps[demo.step];
    uint32_t j;
    for (j = 0; j < 4u && st->hole[j]; j++) {
        uint32_t id = st->hole[j] - 1u;
        int32_t s = j ? 0 : bend_max(id / 10u, id % 10u);
        if (s > st->bend)
            s = st->bend;
        if (!on) {
            hole_off(id);
            continue;
        }
        hole_on(id, (uint32_t)s);
        if (!j) {
            demo.draw = id >= 10u;
            demo.bend = (uint8_t)s;
        }
    }
}

static void demo_run(uint32_t now)
{
    if (!demo.on || (int32_t)(now - demo.next) < 0)
        return;
    if (demo.sounding) {                 /* the step's time is up: a breath between steps */
        demo_step(0);
        demo.sounding = 0;
        demo.step++;
        demo.next = now + 30u;
        ui.redraw = 1;
        return;
    }
    if (demo.step >= LICKS[set.lick].n) {   /* the end: again from the start, after a breath, until PLAY */
        demo.step = 0;
        demo.next = now + 400u;
        ui.redraw = 1;
        return;
    }
    demo_step(1);
    demo.sounding = 1;
    demo.next = now + LICKS[set.lick].steps[demo.step].ms;
}

static void demo_stop(void)
{
    if (demo.on && demo.sounding)
        demo_step(0);
    demo.on = demo.sounding = 0;
    ui.redraw = 1;
}

static void demo_start(void)             /* the lick has the mouth: what the keys sound stops */
{
    holes_off();
    mouth = 0;
    demo.on = 1;
    demo.step = 0;
    demo.sounding = 0;
    demo.draw = demo.bend = 0;
    demo.next = plat_ms();
    ui.redraw = 1;
}

static int drawing(void) { return demo.on ? demo.draw : breath; }   /* the breath, as the screen and lights show it */

/* --------------------------------------------------------------- messages --- */
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.msg_rgb = 0;
    ui.redraw = 1;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

/* where the chosen lick is played, on this harmonica: "2nd position, in G" (shorter: "2nd pos, in G"); written for
 * another tuning, "for NATURAL MINOR" */
static void lick_info(char *t, int shorter)
{
    uint32_t lt = LICKS[set.lick].tuning, p = LICKS[set.lick].pos;
    t[0] = 0;
    if (lt != set.tuning) {
        text_cat(t, "for ");
        text_cat(t, TUN[lt].name);
    } else if (p) {
        const hpos_t *hp = &tun()->pos[p - 1u];
        text_cat(t, ORD[p - 1u]);
        text_cat(t, shorter ? " pos, in " : " position, in ");
        text_cat(t, note_name((int32_t)((uint32_t)hkey()->hole1 + tun()->label_off + hp->off)));
        if (hp->minor)
            text_cat(t, " m");
    }
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[128], p[24];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, tun()->name);
        text_cat(t, " ");
        key_label(p);
        text_cat(t, p);
        text_cat(t, ". LICK ");
        text_cat(t, LICKS[set.lick].name);
        text_cat(t, ". ");
        text_cat(t, PAGE_NAME[set.page]);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* ---------------------------------------------------------------- changes --- */
static void changed(void)
{
    set_dirty = 1;
    ui.redraw = 1;
}

static void tuning_step(int32_t d)
{
    const tuning_t *t;
    uint32_t i;
    knob_show(OV_TUNING);
    holes_off();
    set.tuning = (uint8_t)((set.tuning + NTUN + (uint32_t)(d > 0 ? 1 : NTUN - 1)) % NTUN);
    t = tun();
    set.key = 0;
    for (i = 0; i < t->nkeys; i++)       /* the key whose hole 1 blows middle C, as a start */
        if (t->keys[i].hole1 == 60u)
            set.key = (uint8_t)i;
    if (!demo.on)
        mouth_sync();                    /* held keys go on, on the new harmonica */
    changed();
    app_say(t->name, "");
}

static void key_step(int32_t d)
{
    int32_t k = clamp((int32_t)set.key + d, 0, (int32_t)tun()->nkeys - 1);
    char t[16];
    knob_show(OV_KEY);
    if (k == set.key)
        return;
    holes_off();
    set.key = (uint8_t)k;
    if (!demo.on)
        mouth_sync();
    changed();
    key_label(t);
    app_say("KEY", t);
}

static void lick_step(int32_t d)         /* the next lick (or the one before); playing, it starts */
{
    char t[12] = "LICK ";
    int playing = demo.on;
    knob_show(OV_LICK);
    demo_stop();
    set.lick = (uint8_t)((set.lick + NLICKS + (d > 0 ? 1 : NLICKS - 1)) % NLICKS);
    changed();
    int_text(set.lick + 1, t + 5);
    app_say(t, LICKS[set.lick].name);
    ui.msg_rgb = TUN_RGB[LICKS[set.lick].tuning];
    if (playing)
        demo_start();
}

static void param_step(uint32_t i, int32_t d)
{
    float *v = param(i), x = *v + (float)d * PRM[i].step;
    *v = fm_clampf(x, PRM[i].lo, PRM[i].hi);
    set.preset = NPRESETS;
    hm_params(&set.p);
    changed();
}

static void knob_turn(uint32_t k, int32_t d)
{
    if (k == KN_NOTE) {
        set.notation ^= 1u;
        changed();
    } else if (k == KN_LIGHT) {
        set.lights = (uint8_t)(set.lights == 1u ? 2u : 1u);
        changed();
    } else if (k != KN_NONE) {
        param_step(k, d);
        knob_show(k);
    }
}

static const char *knob_label(uint32_t k)
{
    return k == KN_NOTE ? "Note names" : k == KN_LIGHT ? "Key lights" : k == KN_NONE ? "" : PRM[k].label;
}

static void knob_text(uint32_t k, char *t)
{
    t[0] = 0;
    if (k == KN_NOTE)
        text_cat(t, set.notation ? "C D E" : "DO RE MI");
    else if (k == KN_LIGHT)
        text_cat(t, set.lights == 1u ? "LOW" : "HIGH");
    else if (k != KN_NONE)
        param_text(k, t);
}

/* ------------------------------------------------------------------ input --- */
/* One mouth, one breath: what sounds together is blown or drawn, and within 4 neighbouring holes */
#define MOUTH_REACH 3u                   /* from the lowest hole to the highest */

static void key_event(uint32_t k, int down)
{
    int32_t r = key_role[k];
    uint32_t h;
    if (r < 0)
        return;
    if (r < 10 && down) {                /* a hole: the mouth goes there. The lick stops; holes out of reach leave */
        demo_stop();
        ui.knob_until = 0;               /* back to the harmonica at once */
        for (h = 0; h < 10u; h++)
            if ((h > (uint32_t)r ? h - (uint32_t)r : (uint32_t)r - h) > MOUTH_REACH)
                mouth &= ~(1u << h);
        mouth |= 1u << r;
    } else if (r < 10)
        mouth &= ~(1u << r);
    else if (r == 10)
        breath = (uint8_t)down;          /* the breath turns: the holes in the mouth turn with it */
    else if (down)
        bend_keys |= 1u << (r - 10);
    else
        bend_keys &= ~(1u << (r - 10));
    if (!demo.on)
        mouth_sync();
    ui.redraw = 1;
}

static void button(uint32_t b)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (b == PAGE_BTN[i]) {
            set.page = (uint8_t)i;
            if (b == B_HOME)             /* HOME: the battery too, for 3 s */
                ui.batt_until = plat_ms() + 3000u;
            ui.redraw = 1;
            return;
        }
    if (b == B_EDIT) {                   /* the next sound */
        set.preset = (uint8_t)(set.preset >= NPRESETS - 1u ? 0u : set.preset + 1u);
        set.p = PRESET[set.preset];
        hm_params(&set.p);
        changed();
        app_say("SOUND", PRESET_NAME[set.preset]);
    } else if (b == B_PLAY) {
        if (demo.on)
            demo_stop();
        else {
            demo_start();
            app_say("PLAY", LICKS[set.lick].name);
            ui.msg_rgb = TUN_RGB[LICKS[set.lick].tuning];
        }
    } else if (b == B_SAVE) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
            set_dirty = 0;
            app_say("SAVED", "");
        } else {
            app_say("SAVE", "FAILED");
        }
    }
}

static uint32_t lit_keys, glow_keys;     /* the keys lit bright, and dim, at the last input */

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k, dim_keys = 0, lit_btn, id, b;
    int32_t e;
    for (k = 0; k < NKEYS; k++)
        if ((keys ^ ui.keys) >> k & 1u)
            key_event(k, (int)(keys >> k & 1u));
    for (k = 0; k < NB; k++)
        if ((btn >> k & 1u) && !(ui.btn >> k & 1u))
            button(k);
    if ((btn ^ ui.btn) >> B_ARP & 1u) {  /* held: the hands closed, the hand's tremolo */
        play.wah = (uint8_t)(btn >> B_ARP & 1u);
        hm_wah(play.wah);
        ui.redraw = 1;
    }
    if ((btn ^ ui.btn) >> B_SEQ & 1u) {
        play.trem = (uint8_t)(btn >> B_SEQ & 1u);
        hm_trem(play.trem);
        ui.redraw = 1;
    }
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_SELECT)
            lick_step(e > 0 ? 1 : -1);
        else if (k == EN_ALGO)
            tuning_step(e);
        else if (k == EN_PRESET)
            key_step(e > 0 ? 1 : -1);
        else
            knob_turn(PAGE_KNOB[set.page][k - EN_K1], e);
    }
    ui.keys = keys;
    ui.btn = btn;
    /* the lights: the holes dim, DRAW and the bends dark; bright, the holes that sound, DRAW while drawing and the
     * bend in use (the lick's too: its lights are the keys to press) */
    for (k = 0; k < 10u; k++)
        dim_keys |= 1u << WK[k];
    lit_keys = 0;
    for (id = 0; id < 20u; id++)
        if (play.sounding >> id & 1u)
            lit_keys |= 1u << WK[id % 10u];
    if (drawing())
        lit_keys |= 1u << K_DRAW;
    b = demo.on ? (demo.sounding ? demo.bend : 0u) : bend_now();
    if (b)
        lit_keys |= 1u << K_BEND(b);
    dim_keys &= ~lit_keys;
    glow_keys = dim_keys;
    lit_btn = btn | 1u << PAGE_BTN[set.page] | (demo.on ? 1u << B_PLAY : 0u) | (set_dirty ? 1u << B_SAVE : 0u);
    plat_lights(lit_btn, lit_keys, ~lit_btn & ((1u << NB) - 1u), dim_keys, set.lights);
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
#define C_BLOW RGB(232, 160, 64)
#define C_DRAW RGB(96, 176, 220)
#define C_BOX RGB(18, 22, 26)

/* the screen in two bands of 120 rows, each drawn from the whole layout (rows outside the band are clipped) */
static void band(int32_t top)
{
    cv_begin(240, 120, C_BLACK);
    cv_oy = -top;
    cv_y0 = 0;
    cv_y1 = 120;
}

static void frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t fill)
{
    cv_rect(x, y, w, h, C_LINE);
    cv_rect(x + 1, y + 1, w - 2, h - 2, fill);
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)   /* centred */
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

/* the battery, top right: its three bars, the outline red when it is empty; USB while a computer powers the FM-1;
 * nothing before its first reading */
static void battery_mark(void)
{
    int32_t i;
    uint16_t c = ui.batt ? C_GRAY : RGB(230, 60, 50);
    if (ui.batt == 4)
        cv_text(232 - text_w(&FONT_B, "USB"), 3, &FONT_B, "USB", C_GRAY);
    if (ui.batt < 0 || ui.batt > 3)
        return;
    cv_rect(199, 4, 31, 15, c);
    cv_rect(200, 5, 29, 13, C_BLACK);
    cv_rect(230, 8, 3, 7, c);
    for (i = 0; i < ui.batt; i++)
        cv_rect(202 + i * 9, 7, 7, 9, C_WHITE);
}

static void header(void)
{
    char t[24];
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(8, 3, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 3, &FONT_B, ui.msg[1], ui.msg_rgb ? ui.msg_rgb : C_HI);
    } else if (set.page == PG_HOME) {    /* the harmonica in hand: its key, its tuning */
        int32_t x;
        key_label(t);
        x = cv_text(8, 3, &FONT_B, t, C_WHITE);
        cv_text(x + 8, 5, &FONT_XS, tun()->name, C_HI);
    } else {
        cv_text(8, 3, &FONT_B, PAGE_NAME[set.page], C_WHITE);
        key_label(t);
        text_cat(t, " ");
        text_cat(t, tun()->shortname);
        cv_text(232 - text_w(&FONT_XS, t), 6, &FONT_XS, t, C_HI);
    }
    if (ui.batt_until)
        battery_mark();
    cv_rect(0, 23, 240, 1, C_LINE);
}

/* a note's name in the large font, right-aligned (the font has capitals only: a flat's b is set smaller) */
static void big_note(int32_t xr, int32_t y, int32_t n, uint16_t c)
{
    const char *s = note_name(n);
    char u[8];
    uint32_t i;
    int flat = 0;
    int32_t x;
    for (i = 0; s[i] && i < 7u; i++) {
        if (s[i] == 'b' && i) {
            flat = 1;
            break;
        }
        u[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
    }
    u[i] = 0;
    x = xr - text_w(&FONT_L, u) - (flat ? text_w(&FONT_B, "b") + 1 : 0);
    x = cv_text(x, y, &FONT_L, u, c);
    if (flat)
        cv_text(x + 1, y + 2, &FONT_B, "b", c);
}

static void step_text(const lstep_t *st, char *t)   /* a lick's step as tablature: "+4", "-3''", "-1,2,3", "." a rest */
{
    uint32_t j;
    char n[4];
    t[0] = st->hole[0] ? st->hole[0] > 10u ? '-' : '+' : '.';
    t[1] = 0;
    for (j = 0; j < 4u && st->hole[j]; j++) {
        if (j)
            text_cat(t, ",");
        int_text((int32_t)((st->hole[j] - 1u) % 10u) + 1, n);
        text_cat(t, n);
    }
    for (j = 0; j < st->bend; j++)
        text_cat(t, "'");
}

static void draw_home(void)
{
    uint32_t h, i, d = (uint32_t)drawing(), m, n;
    int32_t x, w, j;
    char t[48], v[16];
    /* the harmonica: the blown notes above the holes, the drawn ones below; the breath in use in its colour */
    for (h = 0; h < 10u; h++) {
        int32_t bx = 2 + (int32_t)h * 236 / 10, nb = bend_max(d, h);
        int bl = (int)(play.sounding >> h & 1u), dr = (int)(play.sounding >> (10u + h) & 1u);
        uint16_t col = dr ? C_DRAW : C_BLOW;
        text_c(bx + 11, 27, &FONT_XS, note_name(bl ? play.note[h] : hole_note(0, h)),
               bl ? C_WHITE : d ? C_DIM : C_BLOW);
        if (bl || dr)
            cv_rect(bx, 44, 22, 30, col);
        else
            frame(bx, 44, 22, 30, C_BOX);
        int_text((int32_t)h + 1, t);
        text_c(bx + 11, 47, &FONT_B, t, bl || dr ? C_BLACK : C_WHITE);
        for (j = 0; j < nb; j++)          /* the bends this hole has on this breath, as dots; the ones in use filled */
            cv_rect(bx + 11 - (nb - 1) * 3 + j * 6 - 1, 68, 3, 3,
                    bl || dr ? (j < (int32_t)play.bend[d * 10u + h] ? C_WHITE : C_BLACK) : C_GRAY);
        text_c(bx + 11, 77, &FONT_XS, note_name(dr ? play.note[10u + h] : hole_note(1, h)),
               dr ? C_WHITE : d ? C_DRAW : C_DIM);
    }
    /* what sounds, large: the hole, the breath, the note; between notes the last one stays, dimmed */
    m = play.sounding ? play.sounding : play.last;
    if (m) {
        uint32_t dd = m >> 10 ? 1u : 0u, first = 0;
        uint16_t col = play.sounding ? (dd ? C_DRAW : C_BLOW) : C_DIM;
        t[0] = 0;
        for (h = 0, n = 0; h < 10u; h++)
            if (m >> (dd * 10u + h) & 1u) {
                if (!n++)
                    first = dd * 10u + h;
                else
                    text_cat(t, " ");
                int_text((int32_t)h + 1, v);
                text_cat(t, v);
            }
        text_cat(t, dd ? " DRAW" : " BLOW");
        if (n == 1u) {
            for (i = 0; i < play.bend[first]; i++)
                text_cat(t, "'");
            cv_text(8, 96, &FONT_L, t, col);
            big_note(232, 96, play.note[first], play.sounding ? C_WHITE : C_DIM);
        } else
            cv_text(8, 100, &FONT_M, t, col);   /* a chord: its notes are the ones lit above */
    }
    cv_rect(0, 131, 240, 1, C_LINE);
    /* the lick: its name in its tuning's colour and where it is played; then its tablature, the step that sounds
     * marked */
    x = cv_text(8, 135, &FONT_B, LICKS[set.lick].name, TUN_RGB[LICKS[set.lick].tuning]) + 8;
    lick_info(t, 0);
    if (x + text_w(&FONT_XS, t) > 234)
        lick_info(t, 1);
    cv_text(x, 137, &FONT_XS, t, C_GRAY);
    n = LICKS[set.lick].n;
    i = demo.on && demo.step ? demo.step - 1u : 0u;
    for (x = 8; i < n; i++) {
        const lstep_t *st = &LICKS[set.lick].steps[i];
        uint16_t col = st->hole[0] > 10u ? C_DRAW : C_BLOW;
        step_text(st, t);
        w = text_w(&FONT_XS, t);
        if (x + w > 234)
            break;
        if (demo.on && i == demo.step) {
            cv_rect(x - 3, 153, w + 6, 17, st->hole[0] ? col : C_DIM);
            cv_text(x, 154, &FONT_XS, t, C_BLACK);
        } else
            cv_text(x, 154, &FONT_XS, t, st->hole[0] ? col : C_DIM);
        x += w + 8;
    }
    /* the sound: EDIT picks it, KNOB 1-4 shape it; what the hands are doing */
    t[0] = 0;
    text_cat(t, "SOUND ");
    text_cat(t, set.preset < NPRESETS ? PRESET_NAME[set.preset] : "CUSTOM");
    if (play.wah)
        text_cat(t, "  WAH");
    if (play.trem)
        text_cat(t, "  TREM");
    cv_text(8, 174, &FONT_XS, t, C_AMB);
    cv_text(232 - text_w(&FONT_T, "EDIT"), 176, &FONT_T, "EDIT", C_DIM);
    for (i = 0; i < 4u; i++) {
        int32_t bx = 2 + (int32_t)i * 59;
        uint32_t kn = PAGE_KNOB[PG_HOME][i];
        frame(bx, 190, 56, 43, C_BOX);
        text_c(bx + 28, 191, &FONT_XS, knob_label(kn), C_GRAY);
        knob_text(kn, t);
        text_c(bx + 28, 204, &FONT_B, t, C_WHITE);
        v[0] = 'K';
        v[1] = (char)('1' + i);
        v[2] = 0;
        text_c(bx + 28, 220, &FONT_T, v, C_DIM);
    }
}

/* text centred in the large font, which has capitals only: its small letters (a flat's b, a minor key's m) are set in
 * the smaller font */
static void big_c(int32_t cx, int32_t y, const char *s, uint16_t c)
{
    char g[2] = {0, 0};
    int32_t w = 0, x;
    uint32_t i;
    for (i = 0; s[i]; i++) {
        g[0] = s[i];
        w += text_w(s[i] >= 'a' && s[i] <= 'z' ? &FONT_B : &FONT_L, g);
    }
    for (i = 0, x = cx - w / 2; s[i]; i++) {
        int sm = s[i] >= 'a' && s[i] <= 'z';
        g[0] = s[i];
        x = cv_text(x, y + (sm ? 2 : 0), sm ? &FONT_B : &FONT_L, g, c);
    }
}

static void upper(char *t)
{
    for (; *t; t++)
        if (*t >= 'a' && *t <= 'z')
            *t = (char)(*t - 32);
}

/* the harmonica's key for the large font: the note's name in capitals, its flat and its minor left small */
static void key_big(char *t)
{
    static const char *const LATIN[7] = {"LA", "SI", "DO", "RE", "MI", "FA", "SOL"};   /* A .. G */
    const char *s = hkey()->label;
    char *d = t;
    while (*s && (*s < 'A' || *s > 'G' || s[1] == 'O' || s[1] == 'I'))   /* "LOW ", "HI " as they are */
        *d++ = *s++;
    if (*s >= 'A' && *s <= 'G') {
        if (set.notation)
            *d++ = *s;
        else {
            const char *l = LATIN[*s - 'A'];
            while (*l)
                *d++ = *l++;
        }
        s++;
    }
    while (*s)
        *d++ = *s++;
    *d = 0;
}

/* a knob just turned on the home page: its name and value in the large font, and the knob's name */
static void draw_knob(void)
{
    uint32_t k = ui.knob, i;
    char t[32], v[32];
    const char *ctl = "";
    if (k == OV_KEY) {
        big_c(120, 50, "KEY", C_AMB);
        key_big(v);
        big_c(120, 104, v, C_WHITE);
        ctl = "PRESET";
    } else if (k == OV_TUNING) {          /* its two words, one a line, in its colour */
        big_c(120, 44, "TUNING", C_AMB);
        for (i = 0; tun()->name[i] && tun()->name[i] != ' '; i++)
            v[i] = tun()->name[i];
        v[i] = 0;
        big_c(120, 96, v, TUN_RGB[set.tuning]);
        if (tun()->name[i])
            big_c(120, 134, tun()->name + i + 1u, TUN_RGB[set.tuning]);
        ctl = "ALGORITHM";
    } else if (k == OV_LICK) {            /* its number, its name in its tuning's colour, where it is played */
        t[0] = 0;
        text_cat(t, "LICK ");
        int_text(set.lick + 1, v);
        text_cat(t, v);
        big_c(120, 50, t, C_AMB);
        big_c(120, 104, LICKS[set.lick].name, TUN_RGB[LICKS[set.lick].tuning]);
        lick_info(v, 0);
        text_c(120, 150, &FONT_B, v, C_GRAY);
        ctl = "SELECT";
    } else {
        int32_t f = (int32_t)((*param(k) - PRM[k].lo) / (PRM[k].hi - PRM[k].lo) * 208.0f + 0.5f);
        t[0] = 0;
        text_cat(t, PRM[k].label);
        upper(t);
        param_text(k, v);
        upper(v);
        big_c(120, 50, t, C_AMB);
        big_c(120, 104, v, C_WHITE);
        cv_rect(16, 160, 208, 12, C_DIM);
        cv_rect(16, 160, f, 12, C_HI);
        cv_rect(16 + (f > 2 ? f - 2 : 0), 157, 2, 18, C_WHITE);
        for (i = 0; i < 4u; i++)
            if (PAGE_KNOB[PG_HOME][i] == k) {
                static char n[8] = "KNOB ";
                n[5] = (char)('1' + i);
                n[6] = 0;
                ctl = n;
            }
    }
    text_c(120, 196, &FONT_B, ctl, C_GRAY);
}

static void draw_page(void)              /* SETUP: a value a row, name and value large */
{
    uint32_t i;
    char t[8], v[32];
    for (i = 0; i < 4u; i++) {
        uint32_t kn = PAGE_KNOB[set.page][i];
        int32_t y = 28 + (int32_t)i * 48, x;
        if (kn == KN_NONE)
            continue;
        t[0] = 'K';
        t[1] = (char)('1' + i);
        t[2] = 0;
        x = cv_text(8, y + 5, &FONT_XS, t, C_DIM) + 6;
        cv_text(x, y + 3, &FONT_B, knob_label(kn), C_AMB);
        knob_text(kn, v);
        cv_text(232 - text_w(&FONT_M, v), y, &FONT_M, v, C_WHITE);   /* capitals only: the values are words */
    }
}

static void draw(void)
{
    int32_t top;
    for (top = 0; top < 240; top += 120) {
        band(top);
        header();
        if (set.page == PG_HOME && ui.knob_until)
            draw_knob();
        else if (set.page == PG_HOME)
            draw_home();
        else
            draw_page();
        cv_rect(0, 235, 240, 5, TUN_RGB[set.tuning]);
        cv_blit(0, (uint32_t)top);
    }
}

void app_init(void)
{
    uint32_t i;
    int ok = plat_store_load(OBJ_SETTINGS, &set, sizeof set) == (int)sizeof set && set.magic == SET_MAGIC
             && set.tuning < NTUN && set.page < NPAGES && set.preset <= NPRESETS && set.notation <= 1u
             && set.lick < NLICKS && set.lights >= 1u && set.lights <= 2u;
    if (ok) {
        ok = set.key < TUN[set.tuning].nkeys;
        for (i = 0; ok && i < HM_NPARAMS; i++)
            ok = *param(i) >= PRM[i].lo && *param(i) <= PRM[i].hi;
    }
    if (!ok) {
        set.magic = SET_MAGIC;
        set.tuning = 0;
        set.key = 10;                    /* C */
        set.page = PG_HOME;
        set.notation = 1;
        set.lick = 0;
        set.lights = 1;
        set.preset = 0;
        set.p = PRESET[0];
    }
    keys_map();
    hm_init(44100.0f);
    hm_params(&set.p);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.batt = -1;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    demo_run(now);
    speak();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (plat_battery() != ui.batt) {
        ui.batt = (int8_t)plat_battery();
        ui.redraw = 1;
    }
    if (ui.batt_until && now >= ui.batt_until) {
        ui.batt_until = 0;
        ui.redraw = 1;
    }
    if (ui.knob_until && now >= ui.knob_until) {
        ui.knob_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    uint32_t id;
    if (!strcmp(what, "tuning"))
        *got = set.tuning;
    else if (!strcmp(what, "key"))
        *got = set.key;
    else if (!strcmp(what, "page"))
        *got = set.page;
    else if (!strcmp(what, "holes"))
        *got = (int)play.sounding;
    else if (!strcmp(what, "note"))
        *got = play.last_note;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "notation"))
        *got = set.notation;
    else if (!strcmp(what, "lick"))
        *got = set.lick;
    else if (!strcmp(what, "demo"))
        *got = demo.on;
    else if (!strcmp(what, "preset"))
        *got = set.preset;
    else if (!strcmp(what, "lights"))
        *got = set.lights;
    else if (!strcmp(what, "breath"))        /* 0 blow, 1 draw, as the screen and the lights show it */
        *got = drawing();
    else if (!strcmp(what, "battery"))       /* the battery is on the screen */
        *got = ui.batt_until != 0;
    else if (!strcmp(what, "dim"))           /* the keys lit dim */
        *got = (int)glow_keys;
    else if (!strcmp(what, "knob"))          /* a knob's name and value fill the screen */
        *got = ui.knob_until != 0;
    else if (!strcmp(what, "lit"))           /* the keys lit bright */
        *got = (int)lit_keys;
    else if (!strcmp(what, "bend")) {        /* the deepest bend on the sounding holes */
        *got = 0;
        for (id = 0; id < 20u; id++)
            if ((play.sounding >> id & 1u) && play.bend[id] > *got)
                *got = play.bend[id];
    } else
        return 0;
    return 1;
}
#endif
