/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_point_device_directional

#include <stdlib.h>
#include <device.h>
#include <drivers/behavior.h>
#include <logging/log.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

enum pd_dir_mode {
  TIME_MODE,
  DISTANCE_MODE,
  EAGER_MODE
};

enum pd_dir_flavor { ONE_DIM, TWO_DIM };

struct behavior_point_device_directional_config {
  enum pd_dir_mode mode;
  enum pd_dir_flavor flavor;
  int  step_size;
};

struct behavior_point_device_directional_data {
  int64_t previous_t;
  int16_t dt;
  int16_t dx;
  int16_t dy;
};

enum pd_dir_type { X_PLUS = 1, X_MINUS, Y_PLUS, Y_MINUS };

static int behavior_point_device_directional_init(const struct device *dev) { return 0; };

static inline void pd_dir_reset_distance(const struct behavior_point_device_directional_config *cfg,
                                         struct behavior_point_device_directional_data *data)
{
  if ( data->dx > cfg->step_size )
    data->dx -= cfg->step_size;
  else
    data->dx = 0;

  if ( data->dy > cfg->step_size )
    data->dy -= cfg->step_size;
  else
    data->dy = 0;
}

static inline void pd_dir_reset(const struct behavior_point_device_directional_config *cfg,
                                struct behavior_point_device_directional_data *data)
{
  switch (cfg->mode) {
  case EAGER_MODE:
    data->dt = 0;
  case TIME_MODE:
    data->dx = 0;
    data->dy = 0;
    data->dt = 0;
    break;
  case DISTANCE_MODE:
    pd_dir_reset_distance(cfg, data);
    break;
  }

}

static inline int pd_dir_time_mode(const struct behavior_point_device_directional_config *cfg,
                                   struct behavior_point_device_directional_data *data,
                                   int16_t dx, int16_t dy, int dt)
{
  // 1. accumulate
  data->dx += dx;
  data->dy += dy;
  data->dt += dt;

  // 2. determine the direction
  if( data->dt >= cfg->step_size ) {
    switch( cfg->flavor ) {
    case ONE_DIM:
      return data->dx > 0 ? X_PLUS : X_MINUS;
      break;
    case TWO_DIM:
      if( abs(data->dx) >= abs(data->dy) )
        return data->dx > 0 ? X_PLUS : X_MINUS;
      else
        return data->dy > 0 ? Y_PLUS : Y_MINUS;
      break;
    default:
      LOG_ERR("unsupported flavor %d", cfg->flavor);
      return -ENOTSUP;
    }
  }

  // 3. direction can't be determied in this event
  return 0;
}

static inline int pd_dir_distance_mode(const struct behavior_point_device_directional_config *cfg,
                                       struct behavior_point_device_directional_data *data,
                                       int16_t dx, int16_t dy)
{
  // 1. accumulate
  data->dx += dx;
  data->dy += dy;

  // 2. determine the direction
  switch (cfg->flavor) {
  case ONE_DIM:
    {
      if( abs(data->dx) >= cfg->step_size )
        return data->dx > 0 ? X_PLUS : X_MINUS;
    }
    break;
  case TWO_DIM:
    {
      if( abs(data->dx) >= cfg->step_size
          && abs(data->dx) >= abs(data->dy) )
        return data->dx > 0 ? X_PLUS : X_MINUS;
      else if ( abs(data->dy) >= cfg->step_size
                && abs(data->dy) >= abs(data->dx) )
        return data->dy > 0 ? Y_PLUS : Y_MINUS;
    }
    break;
  default:
    LOG_ERR("unsupported flavor %d", cfg->flavor);
    return -ENOTSUP;
  }

  // 3. direction can't be determied in this event
  return 0;
}

static inline int pd_dir_eager_mode(const struct behavior_point_device_directional_config *cfg,
                                    struct behavior_point_device_directional_data *data,
                                    int16_t dx, int16_t dy, int dt)
{
  if ( cfg->flavor != ONE_DIM )
    return -ENOTSUP;

  data->dt += dt;
  if( data->dx == 0
      || (data->dx ^ dx) < 0
      || data->dt > cfg->step_size ) {
    data->dx = dx;
    return dx > 0 ? X_PLUS : X_MINUS;
  }

  return 0;
}

static int on_pd_binding_triggered(struct zmk_behavior_binding *binding,
                                   int16_t dx, int16_t dy, int dt, int64_t timestamp)
{
  const struct device *dev = device_get_binding(binding->behavior_dev);
  const struct behavior_point_device_directional_config *cfg = dev->config;
  struct behavior_point_device_directional_data *data = dev->data;

  // event arriving outside of the time window is considered a new manueover
  if( (timestamp - data->previous_t) > CONFIG_ZMK_BHV_PD_DIR_TIME_WINDOW ) {
    data->dx = 0;
    data->dy = 0;
    data->dt = 0;
  }

  // check the direction of the manueover
  int index;
  switch (cfg->mode) {
  case TIME_MODE:
    index = pd_dir_time_mode(cfg, data, dx, dy, dt);
    break;
  case DISTANCE_MODE:
    index = pd_dir_distance_mode(cfg, data, dx, dy);
    break;
  case EAGER_MODE:
    index = pd_dir_eager_mode(cfg, data, dx, dy, dt);
    break;
  default:
    LOG_ERR("unsupported work mode %d", cfg->mode);
		return -ENOTSUP;
  }

  uint32_t keycode;
  switch (index) {
  case X_PLUS:
    keycode = binding->param1;
    break;
  case X_MINUS:
    keycode = binding->param2;
    break;
  case Y_PLUS:
    keycode = binding->param3;
    break;
  case Y_MINUS:
    keycode = binding->param4;
    break;
  case 0: // undetermied state, wait for next event directly
    data->previous_t = timestamp;
		return 0;
  default:
    return -ENOTSUP;
  }

  // keycode determined, send press and release
  LOG_DBG("SEND keycode: %d", keycode);
  ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(keycode, true, timestamp));

  k_msleep(5);

  ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(keycode, false, timestamp));

  // reset state after sending
  data->previous_t = timestamp;
  pd_dir_reset(cfg, data);

  return 0;
}

//
static const struct behavior_driver_api behavior_point_device_directional_driver_api = {
    .pd_binding_triggered = on_pd_binding_triggered
};

#define KP_INST(n)                                                      \
  static struct behavior_point_device_directional_data behavior_pd_dir_data_##n; \
  static struct behavior_point_device_directional_config behavior_pd_dir_config_##n = { \
    .mode = DT_ENUM_IDX(DT_DRV_INST(n), mode), \
    .flavor = DT_ENUM_IDX(DT_DRV_INST(n), flavor), \
    .step_size = DT_INST_PROP(n, step_size), \
  };                                                                    \
  DEVICE_DT_INST_DEFINE(n, behavior_point_device_directional_init, NULL, &behavior_pd_dir_data_##n, \
                        &behavior_pd_dir_config_##n,                    \
                        APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
                        &behavior_point_device_directional_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
