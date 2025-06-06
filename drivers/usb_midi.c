/*
 * Copyright 2025, Hiroyuki OYAMA
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "bsp/board_api.h"
#include "ghost_note.h"
#include "looper.h"
#include "pico/bootrom.h"
#include "tusb.h"

#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
#define USB_PID                                                                            \
    (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | _PID_MAP(MIDI, 3) | \
     _PID_MAP(VENDOR, 4))

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_MIDI_OUT 0x01
#define EPNUM_MIDI_IN 0x81

#define EPNUM_CDC_NOTIF 0x82  // notification endpoint for CDC
#define EPNUM_CDC_OUT 0x03    // out endpoint for CDC
#define EPNUM_CDC_IN 0x83     // in endpoint for CDC

tusb_desc_device_t const desc_device = {.bLength = sizeof(tusb_desc_device_t),
                                        .bDescriptorType = TUSB_DESC_DEVICE,
                                        .bcdUSB = 0x0200,
                                        .bDeviceClass = 0x00,
                                        .bDeviceSubClass = 0x00,
                                        .bDeviceProtocol = 0x00,
                                        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

                                        .idVendor = 0xCafe,
                                        .idProduct = USB_PID,
                                        .bcdDevice = 0x0100,

                                        .iManufacturer = 0x01,
                                        .iProduct = 0x02,
                                        .iSerialNumber = 0x03,
                                        .bNumConfigurations = 0x01};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

enum { ITF_NUM_MIDI = 0, ITF_NUM_MIDI_STREAMING, ITF_NUM_CDC, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MIDI_IFACE,
    STRID_CDC_IFACE,
};

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, STRID_MIDI_IFACE, EPNUM_MIDI_OUT, (0x80 | EPNUM_MIDI_IN), 64),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC_IFACE, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT,
                       EPNUM_CDC_IN, 64)};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    /* 1‑3 : generic device strings */
    "TinyUSB",                  // 1: Manufacturer
    "Pico MIDI Looper",         // 2: Product
    "",                         // 3: Serial, dynamically generated
    "Pico USB MIDI Interface",  // 4: iInterface (MIDI)
    "Pico USB CDC Console",     // 5: iInterface (CDC)
};

static uint16_t descriptor_string_buffer[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;
    switch (index) {
        case STRID_LANGID:
            memcpy(&descriptor_string_buffer[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL:
            chr_count = board_usb_get_serial(descriptor_string_buffer + 1, 32);
            break;

        default:
            // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
            // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors
            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
                return NULL;
            }

            const char *str = string_desc_arr[index];

            // Cap at max char
            chr_count = strlen(str);
            const size_t max_count =
                sizeof(descriptor_string_buffer) / sizeof(descriptor_string_buffer[0]) -
                1;  // -1 for string type
            if (chr_count > max_count) {
                chr_count = max_count;
            }

            // Convert ASCII string into UTF-16
            for (size_t i = 0; i < chr_count; i++) {
                descriptor_string_buffer[1 + i] = str[i];
            }
            break;
    }

    // first byte is length (including header), second byte is string type
    descriptor_string_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return descriptor_string_buffer;
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
    (void)itf;
    if (p_line_coding->bit_rate == 1200) {
        reset_usb_boot(0, 0);
    }
}

void usb_midi_init(void) {
    board_init();

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
}

bool usb_midi_is_connected(void) { return tud_mounted(); }

void usb_midi_send_note(uint8_t channel, uint8_t note, uint8_t velocity) {
    uint8_t const cable_num = 0;
    // Send Note On for current position at full velocity (127) on channel 1.
    uint8_t note_on[] = {0x90 | channel, note, velocity};
    tud_midi_stream_write(cable_num, note_on, sizeof(note_on));

    // Send Note Off for previous note.
    uint8_t note_off[] = {0x80 | channel, note, 0};
    tud_midi_stream_write(cable_num, note_off, sizeof(note_off));
}

static inline int clamp(int x, int lo, int hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

enum {
    MIDI_CC_SOUND_CONTROLLER1 = 70,
    MIDI_CC_SOUND_CONTROLLER2 = 71,
    MIDI_CC_SOUND_CONTROLLER3 = 72,
    MIDI_CC_SOUND_CONTROLLER4 = 73,
    MIDI_CC_SOUND_CONTROLLER5 = 74,
    MIDI_CC_SOUND_CONTROLLER6 = 75,
    MIDI_CC_SOUND_CONTROLLER7 = 76,
    MIDI_CC_SOUND_CONTROLLER8 = 77,
    MIDI_CC_SOUND_CONTROLLER9 = 78,
    MIDI_CC_SOUND_CONTROLLER10 = 79,
    MIDI_CC_SOUND_CONTROLLER11 = 80,
};

static void update_ghost_parameters(uint8_t channel, uint8_t cc, uint8_t value) {
    (void)channel;
    ghost_parameters_t *params = ghost_note_parameters();
    switch (cc) {
        case MIDI_CC_SOUND_CONTROLLER1:
            params->ghost_intensity = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER2:  // euclidean k_max (1-16)
            params->euclidean.k_max = (uint8_t)clamp((int)value, 1, 16);
            break;
        case MIDI_CC_SOUND_CONTROLLER3:  // euclidean k_sufficient (0-k_max)
            params->euclidean.k_sufficient =
                (uint8_t)clamp((int)value, 0, params->euclidean.k_max);
            break;
        case MIDI_CC_SOUND_CONTROLLER4:  // euclidean k_intensity (0.0-1.0)
            params->euclidean.k_intensity = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER5:  // euclidean probability (0.0-1.0)
            params->euclidean.probability = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER6:  // boundary before_probability (0.0-1.0)
            params->boundary.before_probability = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER7:  // boundary after_probability (0.0-1.0)
            params->boundary.after_probability = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER8:  // fill start_mean (0.0-MAX_MEAN)
            params->fill.start_mean = (value / 127.0f) * 32;
            break;
        case MIDI_CC_SOUND_CONTROLLER9:  // fill start_sd (0.0-MAX_SD)
            params->fill.start_sd = (value / 127.0f) * 16;
            break;
        case MIDI_CC_SOUND_CONTROLLER10:  // fill probability (0.0-1.0)
            params->fill.probability = value / 127.0f;
            break;
        case MIDI_CC_SOUND_CONTROLLER11:  // fill interval_bar (0, 2, 4, 8, 16)
            params->fill.interval_bar = (value / 127.0f) * 16;
            break;
        default:
            break;
    }
}

void usb_midi_task(void) {
    tud_task();

    while (tud_midi_available()) {
        uint8_t packet[4] = {0};
        tud_midi_packet_read(packet);
        uint8_t status = packet[1];
        uint8_t channel = status & 0x0F;
        uint8_t message = status & 0xF0;
        if (packet[0] == 0x0F && status == 0xF8)
            looper_handle_midi_tick();
        else if (packet[0] == 0x0F && status == 0xFA)
            looper_handle_midi_start();
        else if (message == 0xB0)
            update_ghost_parameters(channel, packet[2], packet[3]);
    }
}
