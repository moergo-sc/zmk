/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Listener for position and scroll change

#include <logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/pd_scroll_state_changed.h>
#include <zmk/events/pd_position_state_changed.h>
#include <zmk/mouse.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

typedef struct {
  int16_t x;
  int16_t y;
  bool    scroll;
}__attribute__((aligned(4))) zmk_pd_msg;

K_MSGQ_DEFINE(zmk_pd_msgq, sizeof(zmk_pd_msg), CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE, 4);

static void pd_process_msgq(struct k_work *work) {
    zmk_pd_msg msg;

    while (k_msgq_get(&zmk_pd_msgq, &msg, K_NO_WAIT) == 0) {
      if (msg.scroll) {
        LOG_INF("Send pd scroll data (%d, %d)", msg.x, msg.y);
        zmk_hid_mouse_scroll_set(0, 0);
        zmk_hid_mouse_movement_set(0, 0);
        zmk_hid_mouse_scroll_update(msg.x, msg.y);
      }
      else {
        LOG_INF("Send pd position data (%d, %d)", msg.x, msg.y);
        zmk_hid_mouse_movement_set(0, 0);
        zmk_hid_mouse_scroll_set(0, 0);
        zmk_hid_mouse_movement_update(CLAMP(msg.x, INT8_MIN, INT8_MAX), CLAMP(msg.y, INT8_MIN, INT8_MAX));
      }

      zmk_endpoints_send_mouse_report();
    }
}

K_WORK_DEFINE(pd_msg_processor, pd_process_msgq);

int pd_listener(const zmk_event_t *eh) {
    const struct zmk_pd_position_state_changed *mv_ev = as_zmk_pd_position_state_changed(eh);
    if (mv_ev) {
      zmk_pd_msg msg = {.x = mv_ev->x, .y = mv_ev->y, .scroll = false};
      k_msgq_put(&zmk_pd_msgq, &msg, K_NO_WAIT);
      k_work_submit_to_queue(zmk_mouse_work_q(), &pd_msg_processor);
      return 0;
    }

    const struct zmk_pd_scroll_state_changed *sc_ev = as_zmk_pd_scroll_state_changed(eh);
    if (sc_ev) {
      zmk_pd_msg msg = {.x = sc_ev->x, .y = sc_ev->y, .scroll = true};
      k_msgq_put(&zmk_pd_msgq, &msg, K_NO_WAIT);
      k_work_submit_to_queue(zmk_mouse_work_q(), &pd_msg_processor);
      return 0;
    }
    return 0;
}

ZMK_LISTENER(pd_listener, pd_listener);
ZMK_SUBSCRIPTION(pd_listener, zmk_pd_position_state_changed);
ZMK_SUBSCRIPTION(pd_listener, zmk_pd_scroll_state_changed);
