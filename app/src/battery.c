/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, CONFIG_BATTERY_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/battery.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/activity.h>
#include <zmk/workqueue.h>

static uint8_t last_state_of_charge = 0;

uint8_t zmk_battery_state_of_charge(void) { return last_state_of_charge; }

#if DT_HAS_CHOSEN(zmk_battery)
static const struct device *const battery = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));
#else
#warning                                                                                           \
    "Using a node labeled BATTERY for the battery sensor is deprecated. Set a zmk,battery chosen node instead. (Ignore this if you don't have a battery sensor.)"
static const struct device *battery;
#endif

static int zmk_battery_update(const struct device *battery) {
    struct sensor_value state_of_charge;

    LOG_INF("update soc");

    int rc = sensor_sample_fetch_chan(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);

    if (rc != 0) {
        LOG_DBG("Failed to fetch battery values: %d", rc);
        return rc;
    }
#ifdef CONFIG_ZMK_USB_LOGGING
    rc = sensor_channel_get(battery, SENSOR_CHAN_ALL, &state_of_charge);
#else
    rc = sensor_channel_get(battery, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &state_of_charge);
#endif

    if (rc != 0) {
        LOG_DBG("Failed to get battery state of charge: %d", rc);
        return rc;
    }
    else
      LOG_INF("Latest battery values: %d", state_of_charge.val1);

    if (last_state_of_charge != state_of_charge.val1) {
        last_state_of_charge = state_of_charge.val1;
<<<<<<< HEAD
#if IS_ENABLED(CONFIG_BT_BAS)
        LOG_DBG("Setting BAS GATT battery level to %d.", last_state_of_charge);
=======

        LOG_INF("Setting BAS GATT battery level to %d.", last_state_of_charge);
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048

        rc = bt_bas_set_battery_level(last_state_of_charge);

        if (rc != 0) {
            LOG_WRN("Failed to set BAS GATT battery level (err %d)", rc);
            return rc;
        }
#endif
        rc = raise_zmk_battery_state_changed(
            (struct zmk_battery_state_changed){.state_of_charge = last_state_of_charge});
    }

    return rc;
}

static void zmk_battery_work(struct k_work *work) {
    int rc = zmk_battery_update(battery);

    if (rc != 0) {
        LOG_DBG("Failed to update battery value: %d.", rc);
    }
}

K_WORK_DEFINE(battery_work, zmk_battery_work);

static void zmk_battery_timer(struct k_timer *timer) {
<<<<<<< HEAD
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &battery_work);
=======
  LOG_INF("battery timer trigger");
  k_work_submit(&battery_work);
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
}

K_TIMER_DEFINE(battery_timer, zmk_battery_timer, NULL);

static void zmk_battery_start_reporting() {
    if (device_is_ready(battery)) {
        k_timer_start(&battery_timer, K_NO_WAIT, K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL));
    }
}

static int zmk_battery_init(void) {
#if !DT_HAS_CHOSEN(zmk_battery)
    battery = device_get_binding("BATTERY");

    if (battery == NULL) {
        return -ENODEV;
    }

    LOG_WRN("Finding battery device labeled BATTERY is deprecated. Use zmk,battery chosen node.");
#endif

    if (!device_is_ready(battery)) {
        LOG_ERR("Battery device \"%s\" is not ready", battery->name);
        return -ENODEV;
    }

<<<<<<< HEAD
    zmk_battery_start_reporting();
=======
    int rc = zmk_battery_update(battery);

    if (rc != 0) {
        LOG_DBG("Failed to update battery value: %d.", rc);
        return rc;
    }

    /* k_timer_start(&battery_timer, K_MINUTES(1), K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL)); */
    k_timer_start(&battery_timer, K_SECONDS(10), K_SECONDS(CONFIG_ZMK_BATTERY_REPORT_INTERVAL));

    LOG_INF("battery thread initiated");

>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
    return 0;
}

static int battery_event_listener(const zmk_event_t *eh) {

    if (as_zmk_activity_state_changed(eh)) {
        switch (zmk_activity_get_state()) {
        case ZMK_ACTIVITY_ACTIVE:
            zmk_battery_start_reporting();
            return 0;
        case ZMK_ACTIVITY_IDLE:
        case ZMK_ACTIVITY_SLEEP:
            k_timer_stop(&battery_timer);
            return 0;
        default:
            break;
        }
    }
    return -ENOTSUP;
}

ZMK_LISTENER(battery, battery_event_listener);

ZMK_SUBSCRIPTION(battery, zmk_activity_state_changed);

SYS_INIT(zmk_battery_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
