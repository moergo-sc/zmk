/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_point_device_incremental

#include <device.h>
#include <drivers/behavior.h>
#include <logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/pd_scroll_state_changed.h>
#include <zmk/events/pd_position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

enum pd_incr_mode {
  MOVE_MODE,
  SCROLL_MODE
};

enum pd_incr_flavor { DEFAULT, SWAP, X_ONLY, Y_ONLY };

enum pd_scale_mode { MULTIPLIER, DIVIDOR };

struct behavior_point_device_incremental_config {
  enum pd_incr_mode mode;
  enum pd_incr_flavor flavor;
  enum pd_scale_mode scale_mode;
  int  scale_factor;
  bool smoothing; // todo
};

static int behavior_point_device_incremental_init(const struct device *dev) { return 0; };

static int on_pd_binding_triggered(struct zmk_behavior_binding *binding,
                                   int16_t dx, int16_t dy, int dt, int64_t timestamp)
{
  const struct device *dev = device_get_binding(binding->behavior_dev);
  const struct behavior_point_device_incremental_config *cfg = dev->config;

  // set the real dx, dy
  int16_t x, y;
  switch (cfg->flavor) {
  case SWAP:
    x = dy;
    y = dx;
    break;
  case X_ONLY:
    x = dx;
    y = 0;
    break;
  case Y_ONLY:
    x = 0;
    y = dy;
    break;
  default:
    x = dx;
    y = dy;
    break;
  }

  //
  switch (cfg->scale_mode) {
  case MULTIPLIER:
    x = x * cfg->scale_factor;
    y = y * cfg->scale_factor;
    break;
  case DIVIDOR:
    x = x / cfg->scale_factor;
    y = y / cfg->scale_factor;
    break;
  default:
    LOG_ERR("unsupported scale mode %d", cfg->scale_mode);
		return -ENOTSUP;
  }

  // choose the report type
  switch (cfg->mode) {
  case MOVE_MODE:
    return ZMK_EVENT_RAISE(new_zmk_pd_position_state_changed(
                                                             (struct zmk_pd_position_state_changed){.x=x, .y=y}));
  case SCROLL_MODE:
    return ZMK_EVENT_RAISE(new_zmk_pd_scroll_state_changed(
                                                           (struct zmk_pd_scroll_state_changed){.x=x, .y=y}));
  default:
    LOG_ERR("unsupported work mode %d", cfg->mode);
		return -ENOTSUP;
  }

  return 0;
}

static const struct behavior_driver_api behavior_point_device_incremental_driver_api = {
    .pd_binding_triggered = on_pd_binding_triggered
};

#define KP_INST(n)                                                      \
  static struct behavior_point_device_incremental_config behavior_pd_incr_config_##n = { \
    .mode = DT_ENUM_IDX(DT_DRV_INST(n), mode), \
    .flavor = DT_ENUM_IDX(DT_DRV_INST(n), flavor), \
    .scale_mode = DT_ENUM_IDX(DT_DRV_INST(n), scale_mode), \
    .scale_factor = DT_INST_PROP(n, scale_factor), \
    .smoothing = DT_INST_PROP(n, smoothing),                            \
  };                                                                    \
  DEVICE_DT_INST_DEFINE(n, behavior_point_device_incremental_init, NULL, NULL, &behavior_pd_incr_config_##n, \
                        APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                        &behavior_point_device_incremental_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
