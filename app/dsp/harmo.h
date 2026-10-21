/* SPDX-License-Identifier: MPL-2.0 */
/* The harmonica's voice: a three-operator FM reed (OP3 -> OP2 -> OP1, all at 1:1), breath noise, a nasal bump, a
 * saturating amp and the hand's wah and tremolo. The main loop sends notes and settings; the audio interrupt renders.
 * Compiled apart at -O2 (fm1.toml o2): only fastmath.h, no libm, no doubles. */
#pragma once
#include <stdint.h>

/* every field a float: the app treats the struct as an array of HM_NPARAMS values (PR_* in app.c, same order) */
typedef struct {
    float attack_ms;                     /* 5 .. 120 */
    float index;                         /* OP2 -> OP1, 0.3 .. 4 (x1.5 at the attack) */
    float grain;                         /* OP3 -> OP2, 0 .. 2.5 */
    float nasal_db;                      /* the bump at 1.5 kHz, 0 .. 14 dB */
    float breath;                        /* breath noise, 0 .. 0.4 */
    float vib_cents;                     /* vibrato depth, 0 .. 30 cents */
    float rate_hz;                       /* vibrato and tremolo rate, 2 .. 8 Hz */
    float trem;                          /* the hand's tremolo depth, 0 .. 1 (while its key is held) */
    float drive;                         /* the amp's saturation, 0 .. 1 */
    float wah_lo_hz;                     /* the cut-off with the hands closed, 300 .. 2000 Hz */
    float glide_ms;                      /* how fast a bend gets there, 10 .. 200 ms */
    float vol;                           /* the voice's level, 0 .. 1 */
    float source;                        /* 0 the recorded harmonica (hm_samples.c), 1 the FM reed */
} hm_params_t;
#define HM_NPARAMS 13u

/* a recorded note (tools/samples.py writes them, dsp/hm_samples.c): 16-bit mono, looped from loop_start to loop_end
 * (the data goes on a little past the end); a note plays the sample whose root is nearest */
typedef struct {
    const int16_t *pcm;
    uint32_t len, loop_start, loop_end;
    int16_t root, tune;                  /* the note it plays, MIDI; and its tuning, cents */
    uint8_t lo, hi;                      /* the keys the SoundFont gave it (kept for reference) */
    float gain;
} hm_sample_t;
extern const hm_sample_t HM_SAMPLES[];
extern const uint32_t HM_NSAMPLES;
extern const float HM_SAMPLE_RATE;

void hm_init(float sample_rate);
/* main loop: queued, applied at the next block */
void hm_params(const hm_params_t *p);
void hm_note_on(uint32_t id, int32_t midi);
void hm_note_off(uint32_t id);
void hm_bend(uint32_t id, float semis);  /* semitones down, 0 = none: the voice glides there */
void hm_wah(int on);
void hm_trem(int on);
void hm_panic(void);                     /* any context: silence now */
/* audio interrupt: n mono samples, about -1 .. 1 */
void hm_render(float *out, uint32_t n);
