/*
 * Copyright 2025, Hiroyuki OYAMA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include "pico/async_context.h"
#include "pico/stdlib.h"
#include "drivers/button.h"

#define LOOPER_DEFAULT_BPM 120   // Beats per minute (global tempo)
#define LOOPER_BARS 2            // Loop length in bars
#define LOOPER_BEATS_PER_BAR 4   // Time signature numerator (e.g., 4/4)
#define LOOPER_STEPS_PER_BEAT 4  // Resolution (4 = 16th notes)

#define LOOPER_TOTAL_STEPS (LOOPER_STEPS_PER_BEAT * LOOPER_BEATS_PER_BAR * LOOPER_BARS)
#define LOOPER_CLICK_DIV (LOOPER_TOTAL_STEPS / LOOPER_BARS / LOOPER_BEATS_PER_BAR)

#define LFO_RATE (65536 / (4 * LOOPER_BEATS_PER_BAR * LOOPER_STEPS_PER_BEAT))

// Represents the current playback or recording state.
typedef enum {
    LOOPER_STATE_WAITING = 0,   // BLE not connected, waiting.
    LOOPER_STATE_PLAYING,       // Playing back loop.
    LOOPER_STATE_RECORDING,     // recording in progress.
    LOOPER_STATE_TRACK_SWITCH,  // Switching to next track.
    LOOPER_STATE_TAP_TEMPO,
    LOOPER_STATE_CLEAR_TRACKS,
    LOOPER_STATE_SYNC_MUTE,     // MIDI Clock slave: synced, muted (waiting to play)
    LOOPER_STATE_SYNC_PLAYING   // MIDI Clock slave: synced, playing (sound on)
} looper_state_t;

typedef struct {
    uint64_t last_step_time_us;      // Time of last step transition
    uint64_t button_press_start_us;  // Timestamp when button was pressed
} looper_timing_t;

typedef enum {
    LOOPER_CLOCK_INTERNAL = 0,  // Local timer (self-clocking)
    LOOPER_CLOCK_EXTERNAL,      // MIDI clock
} looper_clock_source_t;

/*
 * Runtime playback state, managed globally.
 * Holds track index, current step, recording progress, and last tick time.
 */
typedef struct {
    uint32_t bpm;
    uint32_t step_period_ms;
    looper_state_t state;          // Current looper mode (e.g. PLAYING, RECORDING).
    uint8_t current_track;         // Index of the active track (for recording or preview).
    uint8_t current_step;          // Index of the current step in the sequence loop.
    uint8_t recording_step_count;  // Step count for ongoing recording session (resets on new record).
    looper_timing_t timing;
    uint8_t ghost_bar_counter;
    uint16_t lfo_phase;
    looper_clock_source_t clock_source;
    async_at_time_worker_t tick_timer;  // Step timer (internal clock mode)
    async_at_time_worker_t sync_timer;  // MIDI sync watchdog timer
} looper_status_t;

typedef struct {
    uint8_t probability;
    uint8_t rand_sample;
} ghost_note_t;

// Represents each MIDI track with note and sequence pattern.
typedef struct {
    const char *name;                       // Human-readable name of the track.
    uint8_t note;                           // MIDI note to trigger.
    uint8_t channel;                        // MIDI channel.
    bool pattern[LOOPER_TOTAL_STEPS];       // Current active pattern
    bool hold_pattern[LOOPER_TOTAL_STEPS];  // Temporary copy saved on button down.
    ghost_note_t ghost_notes[LOOPER_TOTAL_STEPS];
    bool fill_pattern[LOOPER_TOTAL_STEPS];
} track_t;


void looper_status_led_init(void);

looper_status_t *looper_status_get(void);

track_t *looper_tracks_get(size_t *num_tracks);

void looper_update_bpm(uint32_t bpm);

void looper_process_state(uint64_t start_us);

void looper_handle_button_event(button_event_t event);

void looper_handle_tick(async_context_t *ctx, async_at_time_worker_t *worker);

void looper_handle_midi_tick(void);
void looper_handle_midi_start(void);

void looper_handle_input(void);

void looper_schedule_step_timer(void);

void looper_perform_note(uint8_t channel, uint8_t note, uint8_t velocity);
