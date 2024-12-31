/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>
#include <zmk/usb.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/led_strip.h>
#include <drivers/ext_power.h>

#include <zmk/rgb_underglow.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/bluetooth/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)

#error "A zmk,underglow chosen node must be declared"

#endif

#if DT_HAS_CHOSEN(zmk_underglow_transform)
#define ZMK_UNDERGLOW_TRANSFORM_NODE DT_CHOSEN(zmk_underglow_transform)
#define ZMK_UNDERGLOW_ROWS DT_PROP(ZMK_UNDERGLOW_TRANSFORM_NODE, rows)
#define ZMK_UNDERGLOW_COLS DT_PROP(ZMK_UNDERGLOW_TRANSFORM_NODE, columns)
#define ZMK_UNDERGLOW_TRANSFORM_LENGTH DT_PROP_LEN(ZMK_UNDERGLOW_TRANSFORM_NODE, map)
static int underglow_transform[] = DT_PROP(ZMK_UNDERGLOW_TRANSFORM_NODE, map);
#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100
#define TICKS_PER_SECOND 30
static struct led_rgb BLACK = (struct led_rgb){r : 0, g : 0, b : 0};

BUILD_ASSERT(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN <= CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX,
             "ERROR: RGB underglow maximum brightness is less than minimum brightness");

struct rgb_underglow_effect {
    char unique_name[50];
    void (*tick_function)(void);
    void (*event_listener)(const zmk_event_t *);
};

struct rgb_underglow_state {
    struct zmk_led_hsb color;
    uint8_t animation_speed;
    uint8_t current_effect;
    uint16_t animation_step;
    bool on;
    bool active;
    bool status_active;
    uint16_t status_animation_step;
};

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static struct led_rgb status_pixels[STRIP_NUM_PIXELS];

static struct rgb_underglow_state state;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif

void zmk_rgb_set_ext_power(void);

static void zmk_rgb_underglow_deactivate(void) {
    state.active = false;
    zmk_rgb_set_ext_power();
}

static void zmk_rgb_underglow_activate(void) {
    state.active = true;
    zmk_rgb_set_ext_power();
}

static struct zmk_led_hsb hsb_scale_min_max(struct zmk_led_hsb hsb) {
    hsb.b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
            (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * hsb.b / BRT_MAX;
    return hsb;
}

static struct zmk_led_hsb hsb_scale_zero_max(struct zmk_led_hsb hsb) {
    hsb.b = hsb.b * CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX / BRT_MAX;
    return hsb;
}

static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb) {
    float r, g, b;

    uint8_t i = hsb.h / 60;
    float v = hsb.b / ((float)BRT_MAX);
    float s = hsb.s / ((float)SAT_MAX);
    float f = hsb.h / ((float)HUE_MAX) * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    struct led_rgb rgb = {r : r * 255, g : g * 255, b : b * 255};

    return rgb;
}

#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
static int keymap_pos_to_led_index(int pos) {
    int index = 0;
    for (int i = 0; i < ZMK_UNDERGLOW_TRANSFORM_LENGTH; i++) {
        if (underglow_transform[i] != -1 && underglow_transform[i] <= STRIP_NUM_PIXELS * 2) {
            if (index == pos) {
                return i;
            }
            index++;
        }
    }
    return -1;
}

static int keymap_pos_to_row(int pos) { return keymap_pos_to_led_index(pos) / ZMK_UNDERGLOW_COLS; }

static int keymap_pos_to_col(int pos) { return keymap_pos_to_led_index(pos) % ZMK_UNDERGLOW_COLS; }
#endif

static void fade_all_leds(void) {
    int active_leds = 0;
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        int r = (pixels[i].r > 5) ? pixels[i].r * 0.97 : 0;
        int g = (pixels[i].g > 5) ? pixels[i].g * 0.97 : 0;
        int b = (pixels[i].b > 5) ? pixels[i].b * 0.97 : 0;
        pixels[i] = (struct led_rgb){r : r, g : g, b : b};
        if (r > 0 || g > 0 || b > 0) {
            active_leds++;
        }
    }
    if (state.active && active_leds == 0) {
        zmk_rgb_underglow_deactivate();
    }
}

static int get_index(int row, int col) {
#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
    int transform_index = row * ZMK_UNDERGLOW_COLS + col;
    if (transform_index >= ZMK_UNDERGLOW_TRANSFORM_LENGTH) {
        return -EINVAL;
    }
    int index = underglow_transform[transform_index];
#else
    int index = row + col;
#endif
    return index;
}

static void set_led(int row, int col, struct led_rgb color) {
    int index = get_index(row, col);
    if (index < 0 || index > STRIP_NUM_PIXELS * 2) {
        return;
    }
    if (color.r > 0 || color.g > 0 || color.b > 0) {
        zmk_rgb_underglow_activate();
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (index >= STRIP_NUM_PIXELS) {
        pixels[index % STRIP_NUM_PIXELS] = color;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (index < STRIP_NUM_PIXELS) {
        pixels[index] = color;
    }
#endif
#else
    pixels[index % STRIP_NUM_PIXELS] = color;
#endif
}

static struct led_rgb get_led(int row, int col) {
    int index = get_index(row, col);
    if (index < 0) {
        return BLACK;
    }
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (index >= STRIP_NUM_PIXELS) {
        return pixels[index % STRIP_NUM_PIXELS];
    } else {
        return BLACK;
    }
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if (index < STRIP_NUM_PIXELS) {
        return pixels[index];
    } else {
        return BLACK;
    }
#endif
#else
    return pixels[index];
#endif
}

static void zmk_rgb_underglow_effect_solid(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = hsb_to_rgb(hsb_scale_min_max(state.color));
    }
}

static void zmk_rgb_underglow_effect_breathe(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.b = abs(state.animation_step - 1200) / 12;

        pixels[i] = hsb_to_rgb(hsb_scale_zero_max(hsb));
    }

    state.animation_step += state.animation_speed * 10;

    if (state.animation_step > 2400) {
        state.animation_step = 0;
    }
}

static void zmk_rgb_underglow_effect_spectrum(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = state.animation_step;

        pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
    }

    state.animation_step += state.animation_speed;
    state.animation_step = state.animation_step % HUE_MAX;
}

static void zmk_rgb_underglow_effect_swirl(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = (HUE_MAX / STRIP_NUM_PIXELS * i + state.animation_step) % HUE_MAX;

        pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
    }

    state.animation_step += state.animation_speed * 2;
    state.animation_step = state.animation_step % HUE_MAX;
}

static bool just_dimmed(int row, int col) {
    double window_start = 0.6;
    double window_length = 0.1;
    struct led_rgb led_val = get_led(row, col);
    struct led_rgb state_col = hsb_to_rgb(hsb_scale_zero_max(state.color));
    return led_val.r >= state_col.r * window_start && led_val.g >= state_col.g * window_start &&
           led_val.b >= state_col.b * window_start &&
           led_val.r <= state_col.r * (window_start + window_length) &&
           led_val.g <= state_col.g * (window_start + window_length) &&
           led_val.b <= state_col.b * (window_start + window_length);
}

static void zmk_rgb_underglow_effect_matrix(void) {
    fade_all_leds();
    int num_pixels = STRIP_NUM_PIXELS;
    if (IS_ENABLED(CONFIG_ZMK_SPLIT)) {
        num_pixels = num_pixels * 2;
    }

#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
    for (int col = 0; col < ZMK_UNDERGLOW_COLS; col++) {
        for (int row = 0; row < ZMK_UNDERGLOW_ROWS; row++) {
            if (just_dimmed(row, col) && row + 1 < ZMK_UNDERGLOW_ROWS) {
                set_led(row + 1, col, hsb_to_rgb(hsb_scale_zero_max(state.color)));
            } else if (row == 0 && rand() % 250 == 0) {
                set_led(row, col, hsb_to_rgb(hsb_scale_zero_max(state.color)));
            }
        }
    }
#else
    int index = state.animation_step / 10;
    set_led(0, index, state.color);
#endif

    state.animation_step++;
    state.animation_step = state.animation_step % (num_pixels * 10);
}

static void pixels_position_state_changed(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev;
    if ((pos_ev = as_zmk_position_state_changed(eh)) != NULL && pos_ev->state) {
#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
        int index = keymap_pos_to_led_index(pos_ev->position);
        if (index == -1) {
            return;
        }
#else
        int index = pos_ev->position;
#endif
        struct zmk_led_hsb color = state.color;
        color.h = rand() % 360;
        set_led(0, index, hsb_to_rgb(hsb_scale_zero_max(color)));
    }
}

#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void light_circle_leds(int x_center, int y_center, int radius, struct led_rgb color) {
    if (radius == 0) {
        set_led(y_center, x_center, color);
        return;
    }
    float num_pixels = 4 + (radius - 1) * 8;
    for (int i = 0; i < num_pixels; i++) {
        double angle = 2 * M_PI * (i / num_pixels);
        int x = round(radius * cos(angle)) + x_center;
        int y = round(radius * sin(angle)) + y_center;

        if (x < ZMK_UNDERGLOW_COLS && y < ZMK_UNDERGLOW_ROWS && x >= 0 && y >= 0) {
            set_led(y, x, color);
        }
    }
}
#endif

#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
#define RIPPLE_STEPS 100
#define RIPPLE_LENGTH 5
#define MAX_RIPPLE_AGE                                                                             \
    RIPPLE_STEPS *(fmax(ZMK_UNDERGLOW_COLS, ZMK_UNDERGLOW_ROWS) + (RIPPLE_LENGTH + 1) / 2)
#define MAX_RIPPLES 3
static int ripple_positions[MAX_RIPPLES];
static int ripple_ages[MAX_RIPPLES];

static int get_brightness(float stage, int radius) {
    int x = radius;
    if (x < fmax(stage - RIPPLE_LENGTH, 0) || x > stage) {
        return 0;
    }
    float val = pow((x + RIPPLE_LENGTH - stage), 2) * log10f(-x + 1 + stage);
    return fmin(val, RIPPLE_LENGTH) * 100 / RIPPLE_LENGTH;
}

#endif

static void ripple_position_state_changed(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev;
    if ((pos_ev = as_zmk_position_state_changed(eh)) != NULL && pos_ev->state) {
#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
        if (keymap_pos_to_row(pos_ev->position) >= 0 && keymap_pos_to_col(pos_ev->position) >= 0) {
            for (int i = 0; i < MAX_RIPPLES; i++) {
                if (ripple_positions[i] == -1) {
                    ripple_positions[i] = pos_ev->position;
                    ripple_ages[i] = MAX_RIPPLE_AGE;
                    break;
                }
            }
        }
#else
        int index = pos_ev->position;
        struct zmk_led_hsb color = state.color;
        color.h = rand() % 360;
        set_led(0, index, hsb_to_rgb(hsb_scale_zero_max(color)));
#endif
    }
}

static void ripple() {
    fade_all_leds();
#ifdef ZMK_UNDERGLOW_TRANSFORM_NODE
    float seconds_per_ripple = 2;
    struct zmk_led_hsb color = state.color;
    color.h = state.animation_step % 360;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (ripple_positions[i] != -1) {
            ripple_ages[i] -= (int)(MAX_RIPPLE_AGE / TICKS_PER_SECOND / seconds_per_ripple);

            int stage = (MAX_RIPPLE_AGE - ripple_ages[i]) / 100.0;
            int min_radius = fmax(stage - RIPPLE_LENGTH, 0);

            for (int j = 0; j < RIPPLE_LENGTH; j++) {
                color.b = get_brightness(stage, min_radius + j);
                light_circle_leds(keymap_pos_to_col(ripple_positions[i]),
                                  keymap_pos_to_row(ripple_positions[i]), min_radius + j,
                                  hsb_to_rgb(hsb_scale_zero_max(color)));
            }

            if (ripple_ages[i] < 0) {
                ripple_positions[i] = -1;
            }
        }
    }
    state.animation_step = (state.animation_step + 1) % 360;
#endif
}

static const struct rgb_underglow_effect effects[] = {
    {"ZMK_BASE_SOLID", &zmk_rgb_underglow_effect_solid, NULL},
    {"ZMK_BASE_BREATHE", &zmk_rgb_underglow_effect_breathe, NULL},
    {"ZMK_BASE_SPECTRUM", &zmk_rgb_underglow_effect_spectrum, NULL},
    {"ZMK_BASE_SWIRL", &zmk_rgb_underglow_effect_swirl, NULL},
    {"ZMK_BASE_MATRIX", &zmk_rgb_underglow_effect_matrix, NULL},
    {"ZMK_REACTIVE_GLITTER", &fade_all_leds, &pixels_position_state_changed},
    {"ZMK_REACTIVE_RIPPLE", &ripple, &ripple_position_state_changed},
};

static int zmk_led_generate_status(void);

static void zmk_led_write_pixels(void) {
    static struct led_rgb led_buffer[STRIP_NUM_PIXELS];
    int bat0 = zmk_battery_state_of_charge();
    int blend = 0;
    int reset_ext_power = 0;
    if (state.status_active) {
        blend = zmk_led_generate_status();
    }

    // fast path: no status indicators, battery level OK
    if (blend == 0 && bat0 >= 20) {
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
        return;
    }
    // battery below minimum charge
    if (bat0 < 10) {
        memset(pixels, 0, sizeof(struct led_rgb) * STRIP_NUM_PIXELS);
        if (state.on) {
            int c_power = ext_power_get(ext_power);
            if (c_power && !state.status_active) {
                // power is on, RGB underglow is on, but battery is too low
                state.on = false;
                reset_ext_power = true;
            }
        }
    }

    if (blend == 0) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i] = pixels[i];
        }
    } else if (blend >= 256) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i] = status_pixels[i];
        }
    } else if (blend < 256) {
        uint16_t blend_l = blend;
        uint16_t blend_r = 256 - blend;
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i].r =
                ((status_pixels[i].r * blend_l) >> 8) + ((pixels[i].r * blend_r) >> 8);
            led_buffer[i].g =
                ((status_pixels[i].g * blend_l) >> 8) + ((pixels[i].g * blend_r) >> 8);
            led_buffer[i].b =
                ((status_pixels[i].b * blend_l) >> 8) + ((pixels[i].b * blend_r) >> 8);
        }
    }

    // battery below 20%, reduce LED brightness
    if (bat0 < 20) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i].r = led_buffer[i].r >> 1;
            led_buffer[i].g = led_buffer[i].g >> 1;
            led_buffer[i].b = led_buffer[i].b >> 1;
        }
    }

    int err = led_strip_update_rgb(led_strip, led_buffer, STRIP_NUM_PIXELS);
    if (err < 0) {
        LOG_ERR("Failed to update the RGB strip (%d)", err);
    }

    if (reset_ext_power) {
        zmk_rgb_set_ext_power();
    }
}

#define UNDERGLOW_INDICATORS DT_PATH(underglow_indicators)

#if defined(DT_N_S_underglow_indicators_EXISTS)
#define UNDERGLOW_INDICATORS_ENABLED 1
#else
#define UNDERGLOW_INDICATORS_ENABLED 0
#endif

#if !UNDERGLOW_INDICATORS_ENABLED
static int zmk_led_generate_status(void) { return 0; }
#else

const uint8_t underglow_layer_state[] = DT_PROP(UNDERGLOW_INDICATORS, layer_state);
const uint8_t underglow_ble_state[] = DT_PROP(UNDERGLOW_INDICATORS, ble_state);
const uint8_t underglow_bat_lhs[] = DT_PROP(UNDERGLOW_INDICATORS, bat_lhs);
const uint8_t underglow_bat_rhs[] = DT_PROP(UNDERGLOW_INDICATORS, bat_rhs);

#define HEXRGB(R, G, B)                                                                            \
    ((struct led_rgb){                                                                             \
        r : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (R)) / 0xff,                                       \
        g : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (G)) / 0xff,                                       \
        b : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (B)) / 0xff                                        \
    })
const struct led_rgb red = HEXRGB(0xff, 0x00, 0x00);
const struct led_rgb yellow = HEXRGB(0xff, 0xff, 0x00);
const struct led_rgb green = HEXRGB(0x00, 0xff, 0x00);
const struct led_rgb dull_green = HEXRGB(0x00, 0xff, 0x68);
const struct led_rgb magenta = HEXRGB(0xff, 0x00, 0xff);
const struct led_rgb white = HEXRGB(0xff, 0xff, 0xff);
const struct led_rgb lilac = HEXRGB(0x6b, 0x1f, 0xce);

static void zmk_led_battery_level(int bat_level, const uint8_t *addresses, size_t addresses_len) {
    struct led_rgb bat_colour;

    if (bat_level > 40) {
        bat_colour = green;
    } else if (bat_level > 20) {
        bat_colour = yellow;
    } else {
        bat_colour = red;
    }

    // originally, six levels, 0 .. 100

    for (int i = 0; i < addresses_len; i++) {
        int min_level = (i * 100) / (addresses_len - 1);
        if (bat_level >= min_level) {
            status_pixels[addresses[i]] = bat_colour;
        }
    }
}

static void zmk_led_fill(struct led_rgb color, const uint8_t *addresses, size_t addresses_len) {
    for (int i = 0; i < addresses_len; i++) {
        status_pixels[addresses[i]] = color;
    }
}

#define ZMK_LED_NUMLOCK_BIT BIT(0)
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)

static int zmk_led_generate_status(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        status_pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
    }

    // BATTERY STATUS
    zmk_led_battery_level(zmk_battery_state_of_charge(), underglow_bat_lhs,
                          DT_PROP_LEN(UNDERGLOW_INDICATORS, bat_lhs));

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    uint8_t peripheral_level = 0;
    int rc = zmk_split_get_peripheral_battery_level(0, &peripheral_level);

    if (rc == 0) {
        zmk_led_battery_level(peripheral_level, underglow_bat_rhs,
                              DT_PROP_LEN(UNDERGLOW_INDICATORS, bat_rhs));
    } else if (rc == -ENOTCONN) {
        zmk_led_fill(red, underglow_bat_rhs, DT_PROP_LEN(UNDERGLOW_INDICATORS, bat_rhs));
    } else if (rc == -EINVAL) {
        LOG_ERR("Invalid peripheral index requested for battery level read: 0");
    }
#endif

    // CAPSLOCK/NUMLOCK/SCROLLOCK STATUS
    zmk_hid_indicators_t led_flags = zmk_hid_indicators_get_current_profile();

    if (led_flags & ZMK_LED_CAPSLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, capslock)] = red;
    if (led_flags & ZMK_LED_NUMLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, numlock)] = red;
    if (led_flags & ZMK_LED_SCROLLLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, scrolllock)] = red;

    // LAYER STATUS
    for (uint8_t i = 0; i < DT_PROP_LEN(UNDERGLOW_INDICATORS, layer_state); i++) {
        if (zmk_keymap_layer_active(i))
            status_pixels[underglow_layer_state[i]] = magenta;
    }

    struct zmk_endpoint_instance active_endpoint = zmk_endpoints_selected();

    if (!zmk_endpoints_preferred_transport_is_active())
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, output_fallback)] = red;

    int active_ble_profile_index = zmk_ble_active_profile_index();
    for (uint8_t i = 0;
         i < MIN(ZMK_BLE_PROFILE_COUNT, DT_PROP_LEN(UNDERGLOW_INDICATORS, ble_state)); i++) {
        int8_t status = zmk_ble_profile_status(i);
        int ble_pixel = underglow_ble_state[i];
        if (status == 2 && active_endpoint.transport == ZMK_TRANSPORT_BLE &&
            active_ble_profile_index == i) { // connected AND active
            status_pixels[ble_pixel] = white;
        } else if (status == 2) { // connected
            status_pixels[ble_pixel] = dull_green;
        } else if (status == 1) { // paired
            status_pixels[ble_pixel] = red;
        } else if (status == 0) { // unused
            status_pixels[ble_pixel] = lilac;
        }
    }

    enum zmk_usb_conn_state usb_state = zmk_usb_get_conn_state();
    if (usb_state == ZMK_USB_CONN_HID &&
        active_endpoint.transport == ZMK_TRANSPORT_USB) { // connected AND active
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = white;
    } else if (usb_state == ZMK_USB_CONN_HID) { // connected
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = dull_green;
    } else if (usb_state == ZMK_USB_CONN_POWERED) { // powered
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = red;
    } else if (usb_state == ZMK_USB_CONN_NONE) { // disconnected
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = lilac;
    }

    int16_t blend = 256;
    if (state.status_animation_step < (500 / 25)) {
        blend = ((state.status_animation_step * 256) / (500 / 25));
    } else if (state.status_animation_step > (8000 / 25)) {
        blend = 256 - (((state.status_animation_step - (8000 / 25)) * 256) / (2000 / 25));
    }
    if (blend < 0)
        blend = 0;
    if (blend > 256)
        blend = 256;

    return blend;
}
#endif // underglow_indicators exists

static void zmk_rgb_underglow_tick(struct k_work *work) {
    if (effects[state.current_effect].tick_function != NULL) {
        effects[state.current_effect].tick_function();
    }

    zmk_led_write_pixels();
}

K_WORK_DEFINE(underglow_tick_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {
    if (!state.on) {
        return;
    }

    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

#if IS_ENABLED(CONFIG_SETTINGS)
static int rgb_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &state, sizeof(state));
        if (rc >= 0) {
            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

struct settings_handler rgb_conf = {.name = "rgb/underglow", .h_set = rgb_settings_set};

static void zmk_rgb_underglow_save_state_work(struct k_work *_work) {
    settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_work_delayable underglow_save_work;
#endif

static int zmk_rgb_underglow_init(void) {
    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (!device_is_ready(ext_power)) {
        LOG_ERR("External power device \"%s\" is not ready", ext_power->name);
        return -ENODEV;
    }
#endif

    state = (struct rgb_underglow_state){
        color : {
            h : CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
            s : CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
            b : CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
        },
        animation_speed : CONFIG_ZMK_RGB_UNDERGLOW_SPD_START,
        current_effect : CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
        animation_step : 0,
        on : IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START)
    };

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    int err = settings_register(&rgb_conf);
    if (err) {
        LOG_ERR("Failed to register the ext_power settings handler (err %d)", err);
        return err;
    }

    k_work_init_delayable(&underglow_save_work, zmk_rgb_underglow_save_state_work);

    settings_load_subtree("rgb/underglow");
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    state.on = zmk_usb_is_powered();
#endif

    if (state.on) {
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(25));
    }

    return 0;
}

int zmk_rgb_underglow_save_state(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    int ret = k_work_reschedule(&underglow_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return MIN(ret, 0);
#else
    return 0;
#endif
}

int zmk_rgb_underglow_get_state(bool *on_off) {
    if (!led_strip)
        return -ENODEV;

    *on_off = state.on;
    return 0;
}

void zmk_rgb_set_ext_power(void) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (ext_power == NULL)
        return;
    int c_power = ext_power_get(ext_power);
    if (c_power < 0) {
        LOG_ERR("Unable to examine EXT_POWER: %d", c_power);
        c_power = 0;
    }
    int desired_state = (state.on && state.active) || state.status_active;
    // force power off, when battery low (<10%)
    if (state.on && !state.status_active) {
        if (zmk_battery_state_of_charge() < 10) {
            desired_state = false;
        }
    }
    if (desired_state && !c_power) {
        int rc = ext_power_enable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to enable EXT_POWER: %d", rc);
        }
    } else if (!desired_state && c_power) {
        int rc = ext_power_disable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to disable EXT_POWER: %d", rc);
        }
    }
#endif
}

int zmk_rgb_underglow_on(void) {
    if (!led_strip)
        return -ENODEV;

    state.on = true;
    zmk_rgb_underglow_activate();

    state.animation_step = 0;
    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(25));

    return zmk_rgb_underglow_save_state();
}

static void zmk_rgb_underglow_off_handler(struct k_work *work) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
    }

    zmk_led_write_pixels();
}

K_WORK_DEFINE(underglow_off_work, zmk_rgb_underglow_off_handler);

int zmk_rgb_underglow_off(void) {
    if (!led_strip)
        return -ENODEV;

    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);

    k_timer_stop(&underglow_tick);
    state.on = false;
    zmk_rgb_underglow_deactivate();
    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_calc_effect(int direction) {
    const int NUM_EFFECTS = sizeof(effects) / sizeof(effects[0]);
    return (state.current_effect + NUM_EFFECTS + direction) % NUM_EFFECTS;
}

int zmk_rgb_underglow_select_effect(int effect) {
    const int NUM_EFFECTS = sizeof(effects) / sizeof(effects[0]);
    if (!led_strip)
        return -ENODEV;

    if (effect < 0 || effect >= NUM_EFFECTS) {
        return -EINVAL;
    }

    state.current_effect = effect;
    state.animation_step = 0;

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_cycle_effect(int direction) {
    return zmk_rgb_underglow_select_effect(zmk_rgb_underglow_calc_effect(direction));
}

int zmk_rgb_underglow_toggle(void) {
    return state.on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on();
}

static void zmk_led_write_pixels_work(struct k_work *work);
static void zmk_rgb_underglow_status_update(struct k_timer *timer);

K_WORK_DEFINE(underglow_write_work, zmk_led_write_pixels_work);
K_TIMER_DEFINE(underglow_status_update_timer, zmk_rgb_underglow_status_update, NULL);

static void zmk_rgb_underglow_status_update(struct k_timer *timer) {
    if (!state.status_active)
        return;
    state.status_animation_step++;
    if (state.status_animation_step > (10000 / 25)) {
        state.status_active = false;
        k_timer_stop(&underglow_status_update_timer);
    }
    if (!k_work_is_pending(&underglow_write_work))
        k_work_submit(&underglow_write_work);
}

static void zmk_led_write_pixels_work(struct k_work *work) {
    zmk_led_write_pixels();
    if (!state.status_active) {
        zmk_rgb_set_ext_power();
    }
}

int zmk_rgb_underglow_status(void) {
    if (!state.status_active) {
        state.status_animation_step = 0;
    } else {
        if (state.status_animation_step > (500 / 25)) {
            state.status_animation_step = 500 / 25;
        }
    }
    state.status_active = true;
    zmk_led_write_pixels();
    zmk_rgb_set_ext_power();

    k_timer_start(&underglow_status_update_timer, K_NO_WAIT, K_MSEC(25));

    return 0;
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    if (color.h > HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
        return -ENOTSUP;
    }

    state.color = color;

    return 0;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction) {
    struct zmk_led_hsb color = state.color;

    color.h += HUE_MAX + (direction * CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP);
    color.h %= HUE_MAX;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction) {
    struct zmk_led_hsb color = state.color;

    int s = color.s + (direction * CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP);
    if (s < 0) {
        s = 0;
    } else if (s > SAT_MAX) {
        s = SAT_MAX;
    }
    color.s = s;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction) {
    struct zmk_led_hsb color = state.color;

    int b = color.b + (direction * CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP);
    color.b = CLAMP(b, 0, BRT_MAX);

    return color;
}

int zmk_rgb_underglow_change_hue(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_hue(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_sat(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_sat(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_brt(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_brt(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_spd(int direction) {
    if (!led_strip)
        return -ENODEV;

    if (state.animation_speed == 1 && direction < 0) {
        return 0;
    }

    state.animation_speed += direction;

    if (state.animation_speed > 5) {
        state.animation_speed = 5;
    }

    return zmk_rgb_underglow_save_state();
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) ||                                          \
    IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
static int rgb_underglow_auto_state(bool *prev_state, bool new_state) {
    if (state.on == new_state) {
        return 0;
    }
    if (new_state) {
        state.on = *prev_state;
        *prev_state = false;
        return zmk_rgb_underglow_on();
    } else {
        state.on = false;
        *prev_state = true;
        return zmk_rgb_underglow_off();
    }
}

static int rgb_underglow_event_listener(const zmk_event_t *eh) {

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
    if (as_zmk_activity_state_changed(eh)) {
        static bool prev_state = false;
        return rgb_underglow_auto_state(&prev_state,
                                        zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    if (as_zmk_usb_conn_state_changed(eh)) {
        static bool prev_state = false;
        return rgb_underglow_auto_state(&prev_state, zmk_usb_is_powered());
    }
#endif

    return -ENOTSUP;
}

ZMK_LISTENER(rgb_underglow, rgb_underglow_event_listener);
#endif // IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) ||
       // IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_activity_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_usb_conn_state_changed);
#endif

static int zmk_underglow_position_event_listener(const zmk_event_t *eh) {
    if (effects[state.current_effect].event_listener != NULL) {
        effects[state.current_effect].event_listener(eh);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgb_underglow_pos, zmk_underglow_position_event_listener);
ZMK_SUBSCRIPTION(rgb_underglow_pos, zmk_position_state_changed);

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
