/* SPDX-License-Identifier: MPL-2.0 */
/* HARMONICA: a 10-hole diatonic harmonica on the FM-1. The keys are a piano: each plays its own pitch from the hole
 * that has it, blown or drawn, as the harmonica plays it unbent; a note it has not is silent. The keys' lights (one
 * colour, two brightnesses) tell which: a blown note bright, a drawn one dim, the others dark. Bends are played as
 * a player makes them: the bend keys with HOLES keys, and the licks. Each setting has one control:
 * SELECT the lick (PLAY loops it until PLAY again), ALGORITHM the tuning, PRESET the harmonica's key, SEL the
 * position, OCT- / OCT+ the keyboard's octave, EDIT the sound, MASTER the volume; ARP held closes the hands (wah),
 * SEQ held is the hand's tremolo. ENV LFO FX GLO open the REED, HANDS, AMP and SETUP pages, whose values KNOB 1-4
 * set; HOME the harmonica, whose tiles show the position, octave, lick and sound, each with its control. Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and
 * announce.c are in scope. */
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
/* EDIT, the only place that changes the source: the first three play the recorded harmonica, the last the FM reed.
 * Volume and source are in the sound, not on a page (MASTER is the volume) */
#define NPRESETS 4u
static const char *const PRESET_NAME[NPRESETS] = {"BLUES", "FOLK", "CHICAGO", "FM"};
static const hm_params_t PRESET[NPRESETS] = {
    {8.0f, 2.6f, 1.1f, 4.0f, 0.06f, 6.0f, 5.5f, 0.5f, 0.0f, 650.0f, 45.0f, 0.8f, 0.0f},
    {25.0f, 1.8f, 0.4f, 2.0f, 0.05f, 3.0f, 5.0f, 0.35f, 0.0f, 900.0f, 60.0f, 0.8f, 0.0f},
    {6.0f, 3.4f, 1.7f, 9.0f, 0.04f, 12.0f, 6.2f, 0.6f, 0.55f, 500.0f, 35.0f, 0.7f, 0.0f},
    {20.0f, 2.6f, 1.1f, 8.0f, 0.18f, 8.0f, 5.5f, 0.5f, 0.0f, 650.0f, 45.0f, 0.8f, 1.0f},
};

#define SET_MAGIC 0x35524148u            /* "HAR5": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t tuning, key, pos, preset;    /* preset: NPRESETS = changed by hand */
    int8_t oct;                          /* the keyboard's octave, -2 .. +2: key 0 is F3 + 12 * oct */
    uint8_t page;
    uint8_t notation;                    /* 0 Latin (Do Re Mi), 1 letters (C D E), the default */
    uint8_t lick;                        /* the lick PLAY plays */
    uint8_t lights;                      /* the drawn notes' lights: 1 low, 2 high */
    uint8_t keymode;                     /* 0 PIANO (each key its note), 1 HOLES (white blow, black draw) */
    uint8_t mouth;                       /* holes the mouth covers: 1 alone, 2 with the next up, 3 with both sides, 4 with one below and two above */
    uint8_t rsv[1];
    hm_params_t p;
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

enum { PG_HOME, PG_REED, PG_HANDS, PG_AMP, PG_SETUP, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"HARMONICA", "REED", "HANDS", "AMP", "SETUP"};
static const uint8_t PAGE_BTN[NPAGES] = {B_HOME, B_ENV, B_LFO, B_FX, B_GLO};
/* what KNOB 1-4 set on each page, each value on one page only: a parameter (PR_*), or one of these. On the home
 * page they set nothing: its tiles show the settings that have their own controls */
enum { KN_NOTE = 100, KN_LIGHT, KN_KEYS, KN_MOUTH, KN_NONE = 255 };
static const uint8_t PAGE_KNOB[NPAGES][4] = {
    {KN_NONE, KN_NONE, KN_NONE, KN_NONE},
    {PR_ATTACK, PR_BREATH, PR_GRAIN, PR_GLIDE},     /* REED: how the reed speaks */
    {PR_VIB, PR_RATE, PR_TREM, PR_WAHLO},           /* HANDS: the throat and the hands (SEQ, ARP) */
    {PR_DRIVE, PR_NASAL, PR_INDEX, KN_NONE},        /* AMP: the colour of the sound */
    {KN_NOTE, KN_LIGHT, KN_KEYS, KN_MOUTH},         /* SETUP */
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
static uint32_t root_pc(void)            /* the pitch class the chosen position plays in */
{
    return ((uint32_t)hkey()->hole1 + tun()->label_off + tun()->pos[set.pos].off) % 12u;
}
static int32_t key_note(uint32_t k) { return 53 + (int32_t)k + 12 * set.oct; }   /* key 0 is F3 */
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

static void octave_text(char *t)
{
    t[0] = set.oct < 0 ? '-' : '+';
    t[1] = (char)('0' + (set.oct < 0 ? -set.oct : set.oct));
    t[2] = 0;
}

static void note_text(int32_t n, char *t)   /* "Sol4" */
{
    char o[8];
    t[0] = 0;
    text_cat(t, note_name(n));
    int_text(n / 12 - 1, o);
    text_cat(t, o);
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

/* the keyboard's octave that puts hole 1 among its first twelve keys */
static int8_t octave_for_key(void) { return (int8_t)(((int32_t)hkey()->hole1 - 53 + 24) / 12 - 2); }

/* ------------------------------------------------------------------- play --- */
/* Each note's hole: its own note first (a note on two holes, G on 2 draw and 3 blow, keeps the lower hole), else a
 * bend that reaches it. The keys play the holes' own notes only; the licks bend (a bent note starts on the hole's
 * own pitch and glides down). */
static struct {
    int8_t id;                           /* draw * 10 + hole, -1: no hole has it */
    uint8_t bend;                        /* half steps down */
} nmap[128];
static uint32_t keys_blow, keys_draw, keys_bent;   /* the keyboard's playable keys, for the lights */

static void map_build(void)
{
    uint32_t h, d, k;
    int32_t n, b;
    for (k = 0; k < 128u; k++)
        nmap[k].id = -1;
    for (h = 0; h < 10u; h++)
        for (d = 0; d < 2u; d++) {
            n = hole_note(d, h);
            if (n < 128 && nmap[n].id < 0) {
                nmap[n].id = (int8_t)(d * 10u + h);
                nmap[n].bend = 0;
            }
        }
    for (h = 0; h < 10u; h++)
        for (d = 0; d < 2u; d++)
            for (b = 1; b <= bend_max(d, h); b++) {
                n = hole_note(d, h) - b;
                if (n >= 0 && n < 128 && nmap[n].id < 0) {
                    nmap[n].id = (int8_t)(d * 10u + h);
                    nmap[n].bend = (uint8_t)b;
                }
            }
    keys_blow = keys_draw = keys_bent = 0;
    for (k = 0; k < NKEYS; k++) {
        n = key_note(k);
        if (n < 0 || n > 127 || nmap[n].id < 0)
            continue;
        if (nmap[n].bend)
            keys_bent |= 1u << k;
        else if (nmap[n].id >= 10)
            keys_draw |= 1u << k;
        else
            keys_blow |= 1u << k;
    }
}

#define NTAB 32u
static struct {
    uint8_t id, bends;                   /* id: draw * 10 + hole */
} tab[NTAB];
static uint32_t ntab;                    /* tokens written since the start (the last NTAB kept) */

#define NO_KEY 0xFFu
static struct {
    uint32_t sounding;                   /* bit id: hole id sounding (blow 0..9, draw 10..19) */
    int32_t note[20];                    /* each sounding hole's note, bent */
    uint8_t bend[20], tok[20], key[20];  /* its bend, its tablature token, the key playing it (NO_KEY: the lick) */
    uint8_t wah, trem;
    int32_t last_note;
} play = {.last_note = -1};

static struct {
    char msg[2][24];                     /* a short message over the header */
    uint16_t msg_rgb;                    /* its second part's colour (0: the theme's) */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t redraw;
} ui;

static void midi_send(uint32_t cin, uint32_t st, uint32_t d1, uint32_t d2)
{
    plat_midi_send(cin | st << 8 | d1 << 16 | d2 << 24, TO_USB);
}

static void tab_add(uint32_t id)
{
    tab[ntab % NTAB].id = (uint8_t)id;
    tab[ntab % NTAB].bends = 0;
    play.tok[id] = (uint8_t)(ntab % NTAB);
    ntab++;
}

/* hole id sounds, bent; a hole already sounding (another key's note on it) moves to this one, as a bend does */
static void hole_on(uint32_t id, uint32_t bend, uint32_t key)
{
    int32_t own = hole_note(id / 10u, id % 10u), note = own - (int32_t)bend;
    if (play.sounding >> id & 1u) {
        midi_send(0x08u, 0x80u, (uint32_t)play.note[id], 0u);
    } else {
        play.sounding |= 1u << id;
        tab_add(id);
        hm_note_on(id, own);
    }
    hm_bend(id, (float)bend);
    play.note[id] = note;
    play.bend[id] = (uint8_t)bend;
    play.key[id] = (uint8_t)key;
    if (bend > tab[play.tok[id]].bends)
        tab[play.tok[id]].bends = (uint8_t)bend;
    play.last_note = note;
    midi_send(0x09u, 0x90u, (uint32_t)note, 100u);
    ui.redraw = 1;
}

static void hole_off(uint32_t id)
{
    if (!(play.sounding >> id & 1u))
        return;
    play.sounding &= ~(1u << id);
    hm_note_off(id);
    midi_send(0x08u, 0x80u, (uint32_t)play.note[id], 0u);
    ui.redraw = 1;
}

static void holes_off(void)              /* before the notes move (tuning, key, octave) */
{
    uint32_t id;
    for (id = 0; id < 20u; id++)
        hole_off(id);
}

/* HOLES: the keys as the harmonica: white keys 1-10 blow holes 1-10, black keys 1-10 draw them, white keys 11-13 bend
 * a half, a whole and a whole and a half step */
static int8_t hole_role[NKEYS];          /* HOLES: the hole id a key plays (0..19), 20 + n a bend of n half steps, -1 */
static uint8_t hole_key[20];             /* HOLES: the key of each hole id */
static uint32_t key_ids[NKEYS];          /* the holes each held key sounds: its own and its neighbours (the mouth) */
static uint32_t bend_keys;               /* HOLES: the bend keys held, bit n for n half steps */

static void holes_map(void)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
    uint32_t i;
    for (i = 0; i < NKEYS; i++)
        hole_role[i] = -1;
    for (i = 0; i < 10u; i++) {
        hole_role[WK[i]] = (int8_t)i;
        hole_key[i] = WK[i];
        hole_role[BK[i]] = (int8_t)(10u + i);
        hole_key[10u + i] = BK[i];
    }
    for (i = 0; i < 3u; i++)
        hole_role[WK[10u + i]] = (int8_t)(21u + i);
}

static void play_keys_reset(void)        /* the keys' state forgotten (the mode changed under them) */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        key_ids[k] = 0;
    bend_keys = 0;
}

/* PLAY: the examples, played over and over until PLAY again; SELECT picks one. licks.tab holds them, as tabs:
 * tools/gen_licks.py makes licks.h. Each is written for one of the tunings, and its name shows in that tuning's
 * colour. Each step is one hole or a chord of up to four neighbours, or a rest, held ms; its first hole bends as far
 * as the step asks and the hole allows. */
typedef struct {
    uint8_t hole[4];                     /* id + 1 (blow 1..10, draw 11..20), 0: none (all four: a rest) */
    uint8_t bend;
    uint16_t ms;
} lstep_t;
#include "licks.h"
#define NLICKS (uint8_t)(sizeof LICKS / sizeof LICKS[0])
static struct {
    uint8_t on, step, sounding;
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
        if (on)
            hole_on(id, (uint32_t)s, NO_KEY);
        else
            hole_off(id);
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
        return;
    }
    if (demo.step >= LICKS[set.lick].n) {   /* the end: again from the start, after a breath, until PLAY */
        demo.step = 0;
        demo.next = now + 400u;
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

/* PIANO: the keyboard's octave moved so the whole lick shows on the keys, each note lit as it sounds: the octave
 * nearest the one in use that holds all its notes, else the one that holds the most */
static void lick_fit(void)
{
    const lstep_t *st = LICKS[set.lick].steps;
    uint32_t i, j, n = LICKS[set.lick].n;
    int32_t o, best = set.oct, best_in = -1, best_far = 99;
    if (set.keymode)
        return;                          /* HOLES: every hole has its key */
    for (o = -2; o <= 2; o++) {
        int32_t lo = 53 + 12 * o, hi = lo + (int32_t)NKEYS - 1, in = 0, far = o > set.oct ? o - set.oct : set.oct - o;
        for (i = 0; i < n; i++)
            for (j = 0; j < 4u && st[i].hole[j]; j++) {
                uint32_t id = st[i].hole[j] - 1u;
                int32_t b = j ? 0 : bend_max(id / 10u, id % 10u), note;
                note = hole_note(id / 10u, id % 10u) - (b < st[i].bend ? b : st[i].bend);
                in += note >= lo && note <= hi;
            }
        if (in > best_in || (in == best_in && far < best_far)) {
            best = o;
            best_in = in;
            best_far = far;
        }
    }
    if (best != set.oct) {
        holes_off();
        set.oct = (int8_t)best;
        map_build();
        set_dirty = 1;
        ui.redraw = 1;
    }
}

static void demo_start(void)
{
    lick_fit();
    demo.on = 1;
    demo.step = 0;
    demo.sounding = 0;
    demo.next = plat_ms();
    ui.redraw = 1;
}

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

static void position_text(char *t)       /* "2nd dominant" */
{
    t[0] = 0;
    text_cat(t, ORD[set.pos]);
    text_cat(t, " ");
    text_cat(t, tun()->pos[set.pos].mode);
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
        text_cat(t, ". PLAYING IN ");
        text_cat(t, note_name((int32_t)root_pc()));
        position_text(p);
        text_cat(t, ", ");
        text_cat(t, p);
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
    holes_off();
    set.tuning = (uint8_t)((set.tuning + NTUN + (uint32_t)(d > 0 ? 1 : NTUN - 1)) % NTUN);
    t = tun();
    set.key = 0;
    for (i = 0; i < t->nkeys; i++)       /* the key whose hole 1 blows middle C, as a start */
        if (t->keys[i].hole1 == 60u)
            set.key = (uint8_t)i;
    set.pos = t->def_pos;
    set.oct = octave_for_key();
    map_build();
    changed();
    app_say(t->name, "");
}

static void key_step(int32_t d)
{
    int32_t k = clamp((int32_t)set.key + d, 0, (int32_t)tun()->nkeys - 1);
    char t[16];
    if (k == set.key)
        return;
    holes_off();
    set.key = (uint8_t)k;
    set.oct = octave_for_key();
    map_build();
    changed();
    key_label(t);
    app_say("KEY", t);
}

static void pos_step(int32_t d)
{
    char t[24];
    set.pos = (uint8_t)((set.pos + 4u + (uint32_t)(d > 0 ? 1 : 3)) % 4u);
    changed();
    position_text(t);
    app_say("POSITION", t);
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.oct + d, -2, 2);
    char t[4];
    if (o == set.oct)
        return;
    holes_off();
    set.oct = (int8_t)o;
    map_build();
    changed();
    octave_text(t);
    app_say("OCTAVE", t);
}

static void lick_step(int32_t d)         /* the next lick (or the one before); playing, it starts */
{
    char t[12] = "LICK ";
    int playing = demo.on;
    demo_stop();
    set.lick = (uint8_t)((set.lick + NLICKS + (d > 0 ? 1 : NLICKS - 1)) % NLICKS);
    lick_fit();
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
    } else if (k == KN_KEYS) {
        holes_off();
        play_keys_reset();
        set.keymode ^= 1u;
        changed();
    } else if (k == KN_MOUTH) {
        set.mouth = (uint8_t)clamp(set.mouth + d, 1, 4);
        changed();
    } else if (k == KN_LIGHT) {
        set.lights = (uint8_t)(set.lights == 1u ? 2u : 1u);
        changed();
    } else if (k == KN_NONE) {
        if (set.page == PG_HOME)         /* the knobs work on the pages: say which */
            app_say("KNOBS", "ENV LFO FX GLO");
    } else
        param_step(k, d);
}

static const char *knob_label(uint32_t k)
{
    return k == KN_NOTE ? "Note names" : k == KN_LIGHT ? "Draw lights" : k == KN_KEYS ? "Keys" : k == KN_MOUTH ? "Mouth"
           : k == KN_NONE ? "" : PRM[k].label;
}

static void knob_text(uint32_t k, char *t)
{
    t[0] = 0;
    if (k == KN_NOTE)
        text_cat(t, set.notation ? "C D E" : "DO RE MI");
    else if (k == KN_LIGHT)
        text_cat(t, set.lights == 1u ? "LOW" : "HIGH");
    else if (k == KN_KEYS)
        text_cat(t, set.keymode ? "HOLES" : "PIANO");
    else if (k == KN_MOUTH)
        text_cat(t, set.mouth == 1u ? "1 HOLE" : set.mouth == 2u ? "2 HOLES" : set.mouth == 3u ? "3 HOLES" : "4 HOLES");
    else if (k != KN_NONE)
        param_text(k, t);
}

static int32_t knob_fill(uint32_t k, int32_t w)   /* a parameter's bar, 0..w; -1: no bar */
{
    if (k >= KN_NOTE || PRM[k].unit == U_SRC)
        return -1;
    return (int32_t)((*param(k) - PRM[k].lo) / (PRM[k].hi - PRM[k].lo) * (float)w + 0.5f);
}

/* ------------------------------------------------------------------ input --- */
/* the mouth: a hole and its neighbours, blown or drawn together, as wide as set.mouth */
static uint32_t mouth_ids(uint32_t id)
{
    uint32_t d = id / 10u, h = id % 10u, lo = h, hi = h, m = 0;
    if (set.mouth == 2u) {
        if (h < 9u)
            hi = h + 1u;
        else
            lo = h - 1u;
    } else if (set.mouth == 3u) {
        lo = h ? h - 1u : 0u;
        hi = h < 9u ? h + 1u : 9u;
    } else if (set.mouth == 4u) {        /* four holes, at the ends too */
        lo = h ? h - 1u : 0u;
        if (lo > 6u)
            lo = 6u;
        hi = lo + 3u;
    }
    for (h = lo; h <= hi; h++)
        m |= 1u << (d * 10u + h);
    return m;
}

/* One mouth, one breath: what sounds together is blown or drawn, and within 4 neighbouring holes */
#define MOUTH_REACH 3u                   /* from the lowest hole to the highest */
static int in_reach(uint32_t a, uint32_t b)   /* two hole ids can sound together */
{
    uint32_t ha = a % 10u, hb = b % 10u;
    return a / 10u == b / 10u && (ha > hb ? ha - hb : hb - ha) <= MOUTH_REACH;
}

/* key k sounds hole id, bent, with its neighbours plain (those another key already sounds stay that key's). The mouth
 * goes there: the lick stops, and what sounds out of its reach stops too */
static void key_play(uint32_t k, uint32_t id, uint32_t bend)
{
    uint32_t m = mouth_ids(id), j, i;
    demo_stop();
    for (j = 0; j < 20u; j++)
        if ((play.sounding >> j & 1u) && !(m >> j & 1u))
            for (i = 0; i < 20u; i++)
                if ((m >> i & 1u) && !in_reach(i, j)) {
                    hole_off(j);
                    break;
                }
    key_ids[k] = m;
    for (j = 0; j < 20u; j++)
        if (j == id)
            hole_on(j, bend, k);
        else if ((m >> j & 1u) && !(play.sounding >> j & 1u))
            hole_on(j, 0, k);
}

static void key_release(uint32_t k)
{
    uint32_t j;
    for (j = 0; j < 20u; j++)            /* (another key may have taken a hole since) */
        if ((key_ids[k] >> j & 1u) && (play.sounding >> j & 1u) && play.key[j] == k)
            hole_off(j);
    key_ids[k] = 0;
}

/* HOLES: the bend keys held bend each held hole key's own hole, as far as it allows */
static void holes_bend(void)
{
    uint32_t k, d = bend_keys & 8u ? 3u : bend_keys & 4u ? 2u : bend_keys & 2u ? 1u : 0u;
    for (k = 0; k < NKEYS; k++) {
        int32_t id = hole_role[k], s;
        if (id < 0 || id >= 20 || !key_ids[k] || !(play.sounding >> id & 1u) || play.key[id] != k)
            continue;
        s = bend_max((uint32_t)id / 10u, (uint32_t)id % 10u);
        if (s > (int32_t)d)
            s = (int32_t)d;
        if (s != play.bend[id])
            hole_on((uint32_t)id, (uint32_t)s, k);
    }
}

static void key_event(uint32_t k, int down)
{
    if (set.keymode) {                   /* HOLES */
        int32_t r = hole_role[k];
        if (r < 0)
            return;
        if (r >= 20) {                   /* a bend key */
            if (down)
                bend_keys |= 1u << (r - 20);
            else
                bend_keys &= ~(1u << (r - 20));
            holes_bend();
        } else if (down) {
            int32_t s = bend_max((uint32_t)r / 10u, (uint32_t)r % 10u), d = bend_keys & 8u ? 3 : bend_keys & 4u ? 2
                                                                            : bend_keys & 2u ? 1 : 0;
            key_play(k, (uint32_t)r, (uint32_t)(s < d ? s : d));
        } else
            key_release(k);
        return;
    }
    if (down) {                          /* PIANO: the harmonica's own notes only, as it plays them unbent */
        int32_t n = key_note(k);
        uint32_t id, j, h, d;
        if (n < 0 || n > 127 || nmap[n].id < 0 || nmap[n].bend)
            return;                      /* no hole has this note (a bend would reach it): a dark, silent key */
        id = (uint32_t)nmap[n].id;
        for (d = 0; d < 2u; d++)         /* a note on two holes (G: 2 draw, 3 blow): the one the mouth can reach */
            for (h = 0; h < 10u; h++) {
                uint32_t alt = d * 10u + h;
                int ok = hole_note(d, h) == n;
                for (j = 0; ok && j < 20u; j++)
                    if ((play.sounding >> j & 1u) && !in_reach(alt, j))
                        ok = 0;
                if (ok && play.sounding)
                    id = alt;
            }
        key_play(k, id, 0);
    } else
        key_release(k);
}

static void button(uint32_t b)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (b == PAGE_BTN[i]) {
            set.page = (uint8_t)i;
            ui.redraw = 1;
            return;
        }
    if (b == B_SEL)
        pos_step(1);
    else if (b == B_EDIT) {              /* the next preset sound */
        set.preset = (uint8_t)(set.preset >= NPRESETS - 1u ? 0u : set.preset + 1u);
        set.p = PRESET[set.preset];
        hm_params(&set.p);
        changed();
        app_say("SOUND", PRESET_NAME[set.preset]);
    } else if (b == B_OCTDN)
        octave_step(-1);
    else if (b == B_OCTUP)
        octave_step(1);
    else if (b == B_PLAY) {
        if (demo.on)
            demo_stop();
        else {
            demo_start();
            app_say("PLAY", LICKS[set.lick].name);
            ui.msg_rgb = TUN_RGB[LICKS[set.lick].tuning];
        }
    } else if (b == B_REC) {
        ntab = 0;
        app_say("TAB", "CLEARED");
    } else if (b == B_SAVE) {
        if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
            set_dirty = 0;
            app_say("SAVED", "");
        } else {
            app_say("SAVE", "FAILED");
        }
    }
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k, lit_keys, dim_keys, lit_btn, id;
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
    /* the lights: the blown notes (PIANO) or holes (HOLES) bright, the drawn ones dim, the others dark; what sounds
     * bright (the lick too) */
    if (set.keymode) {
        uint32_t blow = 0, draw = 0;
        for (id = 0; id < 10u; id++) {
            blow |= 1u << hole_key[id];
            draw |= 1u << hole_key[10u + id];
        }
        lit_keys = blow;
        for (k = 0; k < NKEYS; k++)      /* the bend keys held */
            if ((keys >> k & 1u) && hole_role[k] >= 20)
                lit_keys |= 1u << k;
        for (id = 0; id < 20u; id++)
            if (play.sounding >> id & 1u)
                lit_keys |= 1u << hole_key[id];
        dim_keys = draw & ~lit_keys;
    } else {
        lit_keys = keys_blow;
        for (id = 0; id < 20u; id++)
            if (play.sounding >> id & 1u) {
                int32_t kk = play.note[id] - key_note(0);
                if (kk >= 0 && kk < NKEYS)
                    lit_keys |= 1u << kk;
            }
        dim_keys = keys_draw & ~lit_keys;
    }
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

static void header(void)
{
    char t[24];
    if (plat_ms() < ui.msg_until) {
        int32_t x = cv_text(8, 3, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 3, &FONT_B, ui.msg[1], ui.msg_rgb ? ui.msg_rgb : C_HI);
    } else {
        cv_text(8, 3, &FONT_B, PAGE_NAME[set.page], C_WHITE);
        key_label(t);
        text_cat(t, " ");
        text_cat(t, tun()->shortname);
        cv_text(232 - text_w(&FONT_XS, t), 6, &FONT_XS, t, C_HI);
    }
    cv_rect(0, 23, 240, 1, C_LINE);
}

static void hole_cell(uint32_t draw, uint32_t h, int32_t y)
{
    uint32_t id = draw * 10u + h;
    int32_t x = 2 + (int32_t)h * 236 / 10, w = 22, nb = bend_max(draw, h), j;
    int lit = (int)(play.sounding >> id & 1u);
    int32_t note = lit ? play.note[id] : hole_note(draw, h);   /* a bent hole shows the note it bends to */
    uint16_t col = draw ? C_DRAW : C_BLOW;
    char t[4];
    if (lit)
        cv_rect(x, y, w, 32, col);
    else
        frame(x, y, w, 32, C_BOX);
    text_c(x + w / 2, y + 3, &FONT_XS, note_name(note), lit ? C_BLACK : C_WHITE);
    int_text(note / 12 - 1, t);
    text_c(x + w / 2, y + 16, &FONT_T, t, lit ? C_BLACK : C_DIM);
    for (j = 0; j < nb; j++)              /* the bends this hole has, as dots; the ones in use filled */
        cv_rect(x + w / 2 - (nb - 1) * 2 + j * 4 - 1, y + 28, 2, 2,
                lit ? (j < (int32_t)play.bend[id] ? C_WHITE : C_BLACK) : C_GRAY);
    if ((uint32_t)hole_note(draw, h) % 12u == root_pc())   /* the position's tonic */
        cv_rect(x + w - 5, y + 2, 3, 3, lit ? C_BLACK : col);
}

static void draw_home(void)
{
    uint32_t h, i, k;
    int32_t x, w;
    char t[48], p[24];
    for (h = 0; h < 10u; h++) {
        hole_cell(0, h, 28);
        int_text((int32_t)h + 1, t);
        text_c(2 + (int32_t)h * 236 / 10 + 11, 62, &FONT_T, t, C_GRAY);
        hole_cell(1, h, 76);
    }
    t[0] = 0;
    text_cat(t, "Playing in ");
    text_cat(t, note_name((int32_t)root_pc()));
    if (tun()->pos[set.pos].minor)
        text_cat(t, " m");
    cv_text(8, 113, &FONT_B, t, C_WHITE);
    t[0] = 0;
    text_cat(t, ORD[set.pos]);
    text_cat(t, " position, ");
    text_cat(t, tun()->pos[set.pos].mode);
    cv_text(8, 132, &FONT_XS, t, C_GRAY);
    /* the keyboard's range, and what the hands are doing */
    t[0] = 0;
    if (set.keymode)
        text_cat(t, "KEYS HOLES");
    else {
        text_cat(t, "KEYS ");
        note_text(key_note(0), p);
        text_cat(t, p);
        text_cat(t, "-");
        note_text(key_note(NKEYS - 1u), p);
        text_cat(t, p);
    }
    if (set.mouth > 1u)
        text_cat(t, set.mouth == 2u ? "  MOUTH 2" : set.mouth == 3u ? "  MOUTH 3" : "  MOUTH 4");
    if (play.wah)
        text_cat(t, "  WAH");
    if (play.trem)
        text_cat(t, "  TREM");
    cv_text(8, 148, &FONT_XS, t, C_AMB);
    /* the end of the tablature, as much as fits */
    k = ntab > NTAB ? ntab - NTAB : 0u;
    for (i = ntab, w = 0; i > k; i--) {
        uint32_t tk = (i - 1u) % NTAB;
        int32_t tw;
        t[0] = tab[tk].id >= 10u ? '-' : '+';
        int_text((int32_t)(tab[tk].id % 10u) + 1, t + 1);
        for (h = 0; h < tab[tk].bends; h++)
            text_cat(t, "'");
        tw = text_w(&FONT_XS, t) + 6;
        if (w + tw > 224)
            break;
        w += tw;
    }
    for (x = 8; i < ntab; i++) {
        uint32_t tk = i % NTAB;
        t[0] = tab[tk].id >= 10u ? '-' : '+';
        int_text((int32_t)(tab[tk].id % 10u) + 1, t + 1);
        for (h = 0; h < tab[tk].bends; h++)
            text_cat(t, "'");
        x = cv_text(x, 164, &FONT_XS, t, tab[tk].id >= 10u ? C_DRAW : C_BLOW) + 6;
    }
    /* the settings with their own controls, each tile naming its control */
    for (i = 0; i < 4u; i++) {
        static const char *const NAME[4] = {"Position", "Octave", "Lick", "Sound"};
        static const char *const CTL[4] = {"SEL", "OCT -/+", "SELECT", "EDIT"};
        int32_t bx = 2 + (int32_t)i * 59;
        uint16_t c = C_WHITE;
        frame(bx, 182, 56, 50, C_BOX);
        text_c(bx + 28, 184, &FONT_XS, NAME[i], C_GRAY);
        t[0] = 0;
        if (i == 0)
            text_cat(t, ORD[set.pos]);
        else if (i == 1)
            octave_text(t);
        else if (i == 2) {
            text_cat(t, LICKS[set.lick].name);
            c = TUN_RGB[LICKS[set.lick].tuning];
        } else
            text_cat(t, set.preset < NPRESETS ? PRESET_NAME[set.preset] : "CUSTOM");
        text_c(bx + 28, text_w(&FONT_B, t) > 50 ? 200 : 198, text_w(&FONT_B, t) > 50 ? &FONT_XS : &FONT_B, t,
               c);                       /* a long value in the small font */
        text_c(bx + 28, 218, &FONT_T, CTL[i], C_DIM);
    }
}

static void draw_page(void)              /* a page of four values, one a row: name and value large, a thick bar */
{
    uint32_t i;
    char t[32], v[32];
    for (i = 0; i < 4u; i++) {
        uint32_t kn = PAGE_KNOB[set.page][i];
        int32_t y = 28 + (int32_t)i * 48, f, x;
        if (kn == KN_NONE)
            continue;
        t[0] = 'K';
        t[1] = (char)('1' + i);
        t[2] = 0;
        x = cv_text(8, y + 5, &FONT_XS, t, C_DIM) + 6;
        cv_text(x, y + 3, &FONT_B, knob_label(kn), C_AMB);
        knob_text(kn, v);
        cv_text(232 - text_w(&FONT_M, v), y, &FONT_M, v, C_WHITE);   /* capitals only: the values are units, words */
        if ((f = knob_fill(kn, 224)) >= 0) {
            cv_rect(8, y + 29, 224, 9, C_DIM);
            cv_rect(8, y + 29, f, 9, C_HI);
            cv_rect(8 + (f > 2 ? f - 2 : 0), y + 27, 2, 13, C_WHITE);   /* where the value sits */
        }
    }
    if (set.page != PG_SETUP) {
        t[0] = 0;
        text_cat(t, "SOUND ");
        text_cat(t, set.preset < NPRESETS ? PRESET_NAME[set.preset] : "CUSTOM");
        text_cat(t, "   EDIT: NEXT");
        cv_text(8, 219, &FONT_XS, t, C_GRAY);
    }
}

static void draw(void)
{
    int32_t top;
    for (top = 0; top < 240; top += 120) {
        band(top);
        header();
        if (set.page == PG_HOME)
            draw_home();
        else
            draw_page();
        cv_rect(0, 235, 240, 5, TUN_RGB[set.tuning]);
        cv_blit(0, (uint32_t)top);
    }
}

void app_init(void)
{
    const tuning_t *t;
    uint32_t i;
    int ok = plat_store_load(OBJ_SETTINGS, &set, sizeof set) == (int)sizeof set && set.magic == SET_MAGIC
             && set.tuning < NTUN && set.pos < 4u && set.oct >= -2 && set.oct <= 2 && set.page < NPAGES
             && set.preset <= NPRESETS && set.notation <= 1u && set.lick < NLICKS && set.lights >= 1u
             && set.lights <= 2u && set.keymode <= 1u && set.mouth >= 1u && set.mouth <= 4u;
    if (ok) {
        t = &TUN[set.tuning];
        ok = set.key < t->nkeys;
        for (i = 0; ok && i < HM_NPARAMS; i++)
            ok = *param(i) >= PRM[i].lo && *param(i) <= PRM[i].hi;
    }
    if (!ok) {
        set.magic = SET_MAGIC;
        set.tuning = 0;
        set.key = 10;                    /* C */
        set.pos = TUN[0].def_pos;
        set.oct = octave_for_key();
        set.page = PG_HOME;
        set.notation = 1;
        set.lick = 0;
        set.lights = 2;
        set.keymode = 0;
        set.mouth = 1;
        set.preset = 0;
        set.p = PRESET[0];
    }
    map_build();
    holes_map();
    hm_init(44100.0f);
    hm_params(&set.p);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
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
    else if (!strcmp(what, "pos"))
        *got = set.pos;
    else if (!strcmp(what, "oct"))
        *got = set.oct;
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
    else if (!strcmp(what, "keymode"))
        *got = set.keymode;
    else if (!strcmp(what, "mouth"))
        *got = set.mouth;
    else if (!strcmp(what, "blow"))          /* the keyboard's keys: blown notes (bright), */
        *got = (int)keys_blow;
    else if (!strcmp(what, "draw"))          /* drawn ones (dim), */
        *got = (int)keys_draw;
    else if (!strcmp(what, "bent"))          /* bent ones (dark) */
        *got = (int)keys_bent;
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
