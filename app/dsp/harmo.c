/* SPDX-License-Identifier: MPL-2.0 */
/* The harmonica's voice (harmo.h): the recorded harmonica (hm_samples.c), each note from the sample nearest it and
 * looped, or the FM reed of the browser sketch (HarmoDSP); then the same breath, hands and amp for both. A sine
 * table, RBJ biquads, coefficients once a block, the voices' state in plain structs. */
#include "harmo.h"
#include "fastmath.h"

#define NV 8u                            /* voices: a chord of holes, and the release of the last ones */
#define SIN_N 1024u
#define RING 64u

typedef struct {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
} bq_t;

typedef struct {
    uint32_t id, age;
    uint8_t on, rel, fm;                 /* sounding; let go (fading out); the FM reed, not a sample */
    const hm_sample_t *s;                /* the sample, its read position and its rate for the note unbent */
    float pos, rate;
    float f, p1, p2, p3;                 /* the note's frequency; the operators' phases, in turns */
    float env, t;                        /* the envelope, the time since the note began */
    float bend, bend_to;                 /* cents, gliding to bend_to */
    bq_t nb;                             /* the breath noise's band-pass, at three times the note */
} voice_t;

enum { CMD_ON = 1, CMD_OFF, CMD_BEND, CMD_WAH, CMD_TREM };
typedef struct {
    uint8_t t;
    uint8_t id;
    int16_t midi;
    float semis;
} cmd_t;

static float SIN[SIN_N + 1u];
static float sr = 44100.0f;
static voice_t v[NV];
static uint32_t age;
static hm_params_t P, p_next;            /* the render's settings, and the main loop's next */
static volatile uint32_t p_seq;
static uint32_t p_seen;
static cmd_t ring[RING];
static volatile uint32_t ring_head, ring_tail;   /* written by the main loop, by the render */
static volatile uint8_t panic_now;
static float lfo, wah_f = 7000.0f;
static uint8_t wah_on, trem_on;
static uint32_t seed = 22222u;
static bq_t hp, pk, wah, tone;

#define BARRIER() __asm__ volatile("" ::: "memory")

static inline float sin_t(float ph)      /* sine of a phase in turns, from the table */
{
    float x, f;
    uint32_t i;
    ph -= fm_floorf(ph);
    x = ph * (float)SIN_N;
    i = (uint32_t)x;
    f = x - (float)i;
    return SIN[i] + (SIN[i + 1u] - SIN[i]) * f;
}

static inline float noise(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)(int32_t)seed * (1.0f / 2147483648.0f);
}

enum { BQ_LP, BQ_HP, BQ_BP, BQ_PEAK };
static void bq_set(bq_t *q, int type, float f, float res, float db)
{
    float w = FM_TWO_PI * fm_minf(f, sr * 0.45f) / sr, c = fm_cosf(w), s = fm_sinf(w), al = s / (2.0f * res);
    float b0, b1, b2, a0, a1 = -2.0f * c, a2;
    if (type == BQ_LP) {
        b0 = (1.0f - c) * 0.5f; b1 = 1.0f - c; b2 = b0; a0 = 1.0f + al; a2 = 1.0f - al;
    } else if (type == BQ_HP) {
        b0 = (1.0f + c) * 0.5f; b1 = -(1.0f + c); b2 = b0; a0 = 1.0f + al; a2 = 1.0f - al;
    } else if (type == BQ_BP) {
        b0 = al; b1 = 0.0f; b2 = -al; a0 = 1.0f + al; a2 = 1.0f - al;
    } else {
        float A = fm_db2lin(db * 0.5f);  /* 10^(db/40) */
        b0 = 1.0f + al * A; b1 = a1; b2 = 1.0f - al * A; a0 = 1.0f + al / A; a2 = 1.0f - al / A;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}

static inline float bq_run(bq_t *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 - q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1; q->x1 = x;
    q->y2 = q->y1; q->y1 = fm_flush(y);
    return y;
}

void hm_init(float sample_rate)
{
    uint32_t i;
    sr = sample_rate;
    for (i = 0; i <= SIN_N; i++)
        SIN[i] = fm_sin_turns((float)i / (float)SIN_N);
    bq_set(&hp, BQ_HP, 170.0f, 0.707f, 0.0f);
    P.attack_ms = 20.0f; P.index = 2.6f; P.grain = 1.1f; P.nasal_db = 8.0f; P.breath = 0.18f; P.vib_cents = 8.0f;
    P.rate_hz = 5.5f; P.trem = 0.5f; P.drive = 0.0f; P.wah_lo_hz = 650.0f; P.glide_ms = 45.0f; P.vol = 0.8f;
    P.source = 0.0f;
    p_next = P;
}

/* ------------------------------------------------------------ the main loop's side --- */
static void push(uint8_t t, uint32_t id, int32_t midi, float semis)
{
    uint32_t h = ring_head;
    if (h - ring_tail >= RING)
        return;                          /* full: the render has stalled; drop rather than overwrite */
    ring[h % RING].t = t;
    ring[h % RING].id = (uint8_t)id;
    ring[h % RING].midi = (int16_t)midi;
    ring[h % RING].semis = semis;
    BARRIER();
    ring_head = h + 1u;
}

void hm_params(const hm_params_t *p)
{
    p_next = *p;
    BARRIER();
    p_seq++;
}
void hm_note_on(uint32_t id, int32_t midi) { push(CMD_ON, id, midi, 0.0f); }
void hm_note_off(uint32_t id) { push(CMD_OFF, id, 0, 0.0f); }
void hm_bend(uint32_t id, float semis) { push(CMD_BEND, id, 0, semis); }
void hm_wah(int on) { push(CMD_WAH, 0, on, 0.0f); }
void hm_trem(int on) { push(CMD_TREM, 0, on, 0.0f); }
void hm_panic(void) { panic_now = 1; }

/* ------------------------------------------------------------------- the render --- */
static void note_on(uint32_t id, int32_t midi)
{
    uint32_t i, pick = 0, oldest = 0xFFFFFFFFu;
    voice_t *w;
    for (i = 0; i < NV; i++)             /* the same hole again: the old note lets go */
        if (v[i].on && !v[i].rel && v[i].id == id)
            v[i].rel = 1;
    for (i = 0; i < NV; i++) {           /* a free voice, else the oldest */
        if (!v[i].on) {
            pick = i;
            break;
        }
        if (v[i].age < oldest) {
            oldest = v[i].age;
            pick = i;
        }
    }
    w = &v[pick];
    w->id = id;
    w->age = ++age;
    w->on = 1;
    w->rel = 0;
    w->f = 440.0f * fm_exp2f((float)(midi - 69) * (1.0f / 12.0f));
    w->p1 = w->p2 = w->p3 = 0.0f;
    w->env = w->t = 0.0f;
    w->bend = w->bend_to = 0.0f;
    w->nb.x1 = w->nb.x2 = w->nb.y1 = w->nb.y2 = 0.0f;
    w->fm = (uint8_t)(P.source > 0.5f || !HM_NSAMPLES);
    if (!w->fm) {                        /* the recorded note nearest this one */
        uint32_t best = 0, d, dbest = 1000u;
        for (i = 0; i < HM_NSAMPLES; i++) {
            d = (uint32_t)(midi > HM_SAMPLES[i].root ? midi - HM_SAMPLES[i].root : HM_SAMPLES[i].root - midi);
            if (d < dbest) {
                dbest = d;
                best = i;
            }
        }
        w->s = &HM_SAMPLES[best];
        w->pos = 0.0f;
        w->rate = fm_exp2f(((float)(midi - w->s->root) - (float)w->s->tune * 0.01f) * (1.0f / 12.0f))
                  * HM_SAMPLE_RATE / sr;
    }
    bq_set(&w->nb, BQ_BP, fm_minf(w->f * 3.0f, 8000.0f), 0.9f, 0.0f);
}

static void commands(void)
{
    uint32_t i;
    while (ring_tail != ring_head) {
        cmd_t c = ring[ring_tail % RING];
        BARRIER();
        ring_tail++;
        if (c.t == CMD_ON)
            note_on(c.id, c.midi);
        else if (c.t == CMD_OFF || c.t == CMD_BEND) {
            for (i = 0; i < NV; i++)
                if (v[i].on && !v[i].rel && v[i].id == c.id) {
                    if (c.t == CMD_OFF)
                        v[i].rel = 1;
                    else
                        v[i].bend_to = -100.0f * c.semis;
                }
        } else if (c.t == CMD_WAH)
            wah_on = (uint8_t)(c.midi != 0);
        else if (c.t == CMD_TREM)
            trem_on = (uint8_t)(c.midi != 0);
    }
}

void hm_render(float *out, uint32_t n)
{
    uint32_t i, k;
    float dt = (float)n / sr, ts = 1.0f / sr, vib, atk, kr, ks, a6, drv, norm, lvl, tr, ph;
    if (panic_now) {
        panic_now = 0;
        for (k = 0; k < NV; k++)
            v[k].on = 0;
        ring_tail = ring_head;
    }
    if (p_seq != p_seen) {
        p_seen = p_seq;
        P = p_next;
    }
    commands();
    for (i = 0; i < n; i++)
        out[i] = 0.0f;

    /* once a block: the vibrato, the hands, the nasal bump */
    lfo += P.rate_hz * dt;
    lfo -= fm_floorf(lfo);
    vib = sin_t(lfo) * P.vib_cents;
    wah_f += ((wah_on ? P.wah_lo_hz : 7000.0f) - wah_f) * (1.0f - fm_expf(-dt / (wah_on ? 0.06f : 0.09f)));
    bq_set(&wah, BQ_LP, wah_f, 2.0f, 0.0f);
    bq_set(&pk, BQ_PEAK, 1500.0f, 1.3f, P.nasal_db);
    /* the recording's brightness: a low-pass from 1 to 12 kHz on "Brillance" (the FM reed has its index) */
    bq_set(&tone, BQ_LP, 1000.0f + (P.index - 0.3f) * (11000.0f / 3.7f), 0.707f, 0.0f);
    atk = fm_maxf(0.002f, P.attack_ms * 0.001f);
    kr = fm_expf(-1.0f / (sr * 0.037f));            /* release: ~110 ms to silence */
    ks = fm_expf(-1.0f / (sr * 0.15f));             /* after the attack, down to 80 % */
    a6 = atk * 0.6f;

    for (k = 0; k < NV; k++) {
        voice_t *w = &v[k];
        float inc, a_inc = 1.0f / (atk * sr), idx = P.index * (1.0f / FM_TWO_PI), grain = P.grain * (1.0f / FM_TWO_PI);
        if (!w->on)
            continue;
        w->bend += (w->bend_to - w->bend) * (1.0f - fm_expf(-dt / fm_maxf(0.005f, P.glide_ms * 0.001f)));
        inc = fm_exp2f((w->bend + vib) * (1.0f / 1200.0f));
        if (!w->fm) {                    /* the recording: read at the note's rate, bent; looped */
            const int16_t *pcm = w->s->pcm;
            float step = w->rate * inc, span = (float)(w->s->loop_end - w->s->loop_start), end = (float)w->s->loop_end;
            float g = w->s->gain * (0.45f / 32768.0f);   /* the samples peak at full scale: near the FM reed's level */
            for (i = 0; i < n; i++) {
                uint32_t j = (uint32_t)w->pos;
                float f = w->pos - (float)j, x = (float)pcm[j] + (float)(pcm[j + 1u] - pcm[j]) * f, br;
                w->pos += step;
                if (w->pos >= end)
                    w->pos -= span;
                if (w->rel)
                    w->env *= kr;
                else if (w->t < atk)
                    w->env = fm_minf(1.0f, w->env + a_inc);
                else
                    w->env = 0.8f + (w->env - 0.8f) * ks;
                w->t += ts;
                br = w->t < a6 ? P.breath * 0.4f * w->t / a6 : P.breath * (0.09f + 0.31f * fm_expf(-(w->t - a6) * 10.0f));
                out[i] += x * g * w->env + bq_run(&w->nb, noise()) * br * (w->rel ? w->env * 1.25f : 1.0f);
            }
            if (w->rel && w->env < 1e-4f)
                w->on = 0;
            continue;
        }
        inc *= w->f / sr;
        for (i = 0; i < n; i++) {
            float boost, o3, o2, o1, br;
            if (w->rel)
                w->env *= kr;
            else if (w->t < atk)
                w->env = fm_minf(1.0f, w->env + a_inc);
            else
                w->env = 0.8f + (w->env - 0.8f) * ks;
            w->t += ts;
            /* OP2's index starts half as high again: the reed snaps at the attack */
            boost = w->t < atk ? 1.5f : 1.0f + 0.5f * fm_expf(-(w->t - atk) * (1.0f / 0.09f));
            w->p1 += inc;
            if (w->p1 >= 1.0f)
                w->p1 -= 1.0f;
            w->p2 = w->p3 = w->p1;       /* 1:1:1, one phase */
            o3 = sin_t(w->p3);                                   /* OP3 */
            o2 = sin_t(w->p2 + grain * o3);                      /* OP3 -> OP2 */
            o1 = sin_t(w->p1 + idx * boost * o2);                /* OP2 -> OP1, the carrier */
            br = w->t < a6 ? P.breath * 0.4f * w->t / a6 : P.breath * (0.09f + 0.31f * fm_expf(-(w->t - a6) * 10.0f));
            out[i] += o1 * w->env * 0.22f + bq_run(&w->nb, noise()) * br * (w->rel ? w->env * 1.25f : 1.0f);
        }
        if (w->rel && w->env < 1e-4f)
            w->on = 0;
    }

    /* the amp: low cut, the nasal bump, the saturation, the hands, the level */
    drv = P.drive * 30.0f;
    norm = drv > 0.3f ? 1.0f / fm_tanhf(drv) : 1.0f;
    lvl = P.vol * (1.0f - P.drive * 0.35f);
    ph = lfo * 1.1f + 0.25f;
    tr = trem_on ? 1.0f - P.trem * 0.5f * (1.0f + sin_t(ph)) : 1.0f;
    for (i = 0; i < n; i++) {
        float x = bq_run(&pk, bq_run(&hp, out[i]));
        if (P.source < 0.5f)
            x = bq_run(&tone, x);
        if (drv > 0.3f)
            x = fm_tanhf(drv * x) * norm;
        x = bq_run(&wah, x) * tr * lvl;
        out[i] = fm_tanhf(x * 1.4f) * (1.0f / 1.4f);
    }
}
