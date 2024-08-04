/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_battery_max17048

#include <device.h>
#include <drivers/i2c.h>
#include <kernel.h>
#include <sys/util.h>
#include <logging/log.h>
#include <sys/byteorder.h>
#include <drivers/sensor.h>

LOG_MODULE_REGISTER(max17048, CONFIG_BATTERY_LOG_LEVEL);

#define REG_VCELL 0x02 // 78.125 uV
#define REG_SOC 0x04 // 1%/256
#define REG_MODE 0x06
#define REG_VERSION 0x08
#define REG_HIBRT 0x0A
#define REG_CONFIG 0x0C
#define REG_VALERT 0x14
#define REG_CRATE 0x16 // 0.208%/hr
#define REG_VRESET 0x18 // vcell threshold
#define REG_STATUS 0x1A // ov, uv, soc change, soc low, reset alert
#define REG_CMD 0xFE

struct max17048_config {
	struct i2c_dt_spec i2c;
  const char *i2c_bus_name;
};

struct max17048_data {
  const struct device *dev;
  uint16_t state_of_charge;
  uint16_t vcell;
};

static int max17048_read_register(const struct device *dev, uint8_t reg, uint16_t *value) {
  const struct max17048_config *config = dev->config;

  uint8_t i2c_data[2];
  int ret = i2c_burst_read_dt(&config->i2c, reg, i2c_data, 2);
  if (ret != 0) {
    LOG_DBG("Unable to read register");
    return ret;
  }

  *value = sys_get_be16(i2c_data);

  return 0;
}

static int max17048_write_register(const struct device *dev, uint8_t reg, uint16_t value) {
  const struct max17048_config *config = dev->config;

  uint8_t buf[2];
	sys_put_be16(value, buf);

  return i2c_burst_write_dt(&config->i2c, reg, buf, 2);
}

static int max17048_set_rcomp_value(const struct device *dev, uint8_t rcomp_value) {
  uint16_t tmp = 0;
  int err = max17048_read_register(dev, REG_CONFIG, &tmp);
  if (err != 0) {
    return err;
  }

  tmp = ((uint16_t)rcomp_value << 8) | (tmp & 0xFF);
  err = max17048_write_register(dev, REG_CONFIG, tmp);
  if (err != 0) {
    return err;
  }

  LOG_DBG("set RCOMP to %d", rcomp_value);
  return 0;
}

static int max17048_set_sleep_enabled(const struct device *dev, bool sleep) {
  uint16_t tmp = 0;
  int err = max17048_read_register(dev, REG_CONFIG, &tmp);
  if (err != 0) {
    return err;
  }

  if (sleep) {
    tmp |= 0x80;
  } else {
    tmp &= ~0x0080;
  }

  err = max17048_write_register(dev, REG_CONFIG, tmp);
  if (err != 0) {
    return err;
  }

  LOG_DBG("sleep mode %s", sleep ? "enabled" : "disabled");
  return 0;
}

static int max17048_sample_fetch(const struct device *dev, enum sensor_channel chan) {
  struct max17048_data *const data = dev->data;

  if (chan != SENSOR_CHAN_ALL
      && chan != SENSOR_CHAN_GAUGE_VOLTAGE
      && chan != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE) {
    LOG_DBG("unsupported channel %d", chan);
    return -ENOTSUP;
  }

  int err = max17048_read_register(dev, REG_SOC, &data->state_of_charge);
  if (err != 0) {
    LOG_WRN("failed to read state-of-charge: %d", err);
    return err;
  }

  err = max17048_read_register(dev, REG_VCELL, &data->vcell);
  if (err != 0) {
    LOG_WRN("failed to read vcell: %d", err);
    return err;
  }

  LOG_INF("read values: soc=%d, vcell=%d", data->state_of_charge, data->vcell);

  return 0;
}

static int max17048_channel_get(const struct device *dev, enum sensor_channel chan,
                                struct sensor_value *val) {
  struct max17048_data *const data = dev->data;
  unsigned int tmp = 0;

  switch (chan) {
  case SENSOR_CHAN_GAUGE_VOLTAGE:
    // 1250 / 16 = 78.125
    tmp = data->vcell * 1250 / 16;
    val->val1 = tmp / 1000000; // in V
    val->val2 = tmp % 1000000; // in uV
    LOG_INF("Vcell = %d.%d", val->val1, val->val2);
    break;

  case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
    val->val1 = (data->state_of_charge >> 8); // in 1%
    val->val2 = (data->state_of_charge & 0xFF) * 1000 / 256; // in 0.001%
    LOG_INF("SOC = %d.%d%", val->val1, val->val2);
    break;

  case SENSOR_CHAN_ALL:
    tmp = data->vcell * 1250 / 16;
    val->val1 = tmp / 1000000; // in V
    val->val2 = tmp % 1000000; // in uV
    LOG_INF("Vcell = %d.%d", val->val1, val->val2);
    val->val1 = (data->state_of_charge >> 8); // in 1%
    val->val2 = (data->state_of_charge & 0xFF) * 1000 / 256; // in 0.001%
    LOG_INF("SOC = %d.%d%", val->val1, val->val2);
    break;

  default:
    return -ENOTSUP;
  }

  return 0;
}

static int max17048_init(const struct device *dev) {
  struct max17048_data *data = dev->data;
  const struct max17048_config *config = dev->config;

  // check readiness of i2c bus
	if (!device_is_ready(config->i2c.bus)) {
		LOG_ERR("I2C controller (%s) not ready", config->i2c_bus_name);
		return -ENODEV;
	}

  // init data structure
	data->dev = dev;

  // check version register
  uint16_t ic_version = 0;
  int err = max17048_read_register(dev, REG_VERSION, &ic_version);
  if (err != 0) {
    LOG_WRN("could not get IC version!");
    return err;
  }

  // bring the device out of sleep
  /* max17048_set_sleep_enabled(dev, false); */

  // set the default rcomp value -- 0x97, as stated in the datasheet
  /* max17048_set_rcomp_value(dev, 0x97); */

  LOG_INF("device initialised at 0x%x (i2c=%s) (version %d)", config->i2c.addr,
          config->i2c_bus_name, ic_version);

  return 0;
}

static const struct sensor_driver_api max17048_api = {.sample_fetch = max17048_sample_fetch,
                                                      .channel_get = max17048_channel_get};

#define MAX17048_INIT(inst)                                                                        \
    static struct max17048_config max17048_config_##inst = {                                     \
        .i2c_bus_name = DT_INST_BUS_LABEL(inst),                                                \
		    .i2c = I2C_DT_SPEC_INST_GET(inst),				                                              \
    };                                                                                             \
                                                                                                   \
    static struct max17048_data max17048_data_##inst = {};                                \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(inst, max17048_init, NULL, &max17048_data_##inst,                   \
                          &max17048_config_##inst, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,     \
                          &max17048_api);

DT_INST_FOREACH_STATUS_OKAY(MAX17048_INIT)
