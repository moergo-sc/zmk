/*
 * Copyright (c) 2022 Yong Zhou
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_kscan_cap1203

#define USE_POLLING IS_ENABLED(CONFIG_ZMK_KSCAN_CAP1203_POLL)

#include <kernel.h>
#include <drivers/i2c.h>
#include <drivers/gpio.h>

#include <logging/log.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/mouse.h>
#include <dt-bindings/zmk/mouse.h>
#include <drivers/kscan.h>

#if defined(CONFIG_CAP1203_MIX_MODE) || defined(CONFIG_CAP1203_SLIDER_MODE)
#include <drivers/slider.h>
#endif

LOG_MODULE_REGISTER(kscan_cap1203, CONFIG_ZMK_LOG_LEVEL);

/* List of important registers and associated commands */
#define REG_PRODUCT_ID 0xFD
#define PRODUCT_ID 0x6D
#define REG_VENDOR_ID 0xFE
#define VENDOR_ID 0x5D
#define REG_REVISION 0xFF
#define REVISION_ID 0x00

#define REG_MAIN_CONTROL 0x0
#define CONTROL_INT 0x1

#define REG_GENERAL_STATUS 0x02
#define REG_INPUT_STATUS 0x03

#define REG_INTERRUPT_ENABLE 0x27
#define INTERRUPT_ENABLE 0x7
#define INTERRUPT_DISABLE 0x0

#define REG_REPEAT_ENABLE 0x28
#define REPEAT_ENABLE 0x7
#define REPEAT_DISABLE 0x0

#define REG_MULT_CONFIG 0x2A
#define MULT_BLK_EN_POS 7
#define B_MULT_T1_POS   3
#define B_MULT_T0_POS 2

#define REG_CONFIGURATION_2 0x44
#define RELEASE_INT_POS 0

/* device driver data */
struct kscan_cap1203_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
  bool invert_direction;
};

struct kscan_cap1203_data {
#if defined(CONFIG_CAP1203_MIX_MODE) || defined(CONFIG_CAP1203_SLIDER_MODE)
  struct slider_data slider_data;
#endif

#ifdef CONFIG_CAP1203_MIX_MODE
  bool is_slide; // slide or button manueover
  uint8_t press_state; // accumulated sensor status at end of cycle
  int64_t update_duration;
#endif

	const struct device *dev;
	kscan_callback_t callback; // zmk's kscan callback
  struct k_timer  timer;
	struct k_work work; // actual processing work
	struct gpio_callback int_gpio_cb; // alert gpio pin callback
  uint8_t touch_state; // current sensor status
  int     update_counter;
};

#if defined(CONFIG_CAP1203_SLIDER_MODE) || defined(CONFIG_CAP1203_MIX_MODE)
/* array of the status patterns, the array_index+1 is the position indicator */
/* static uint8_t const slider_pattern[5] = {1, 3, 2, 6, 4}; */
/* array of slider positions with slider pattern as array index */
/* The position of unknown slider patterns is defined to be 0 */
static uint8_t const slider_position[8] = {0, 1, 3, 2, 5, 0, 4, 0};
#endif

/* Functions and variables for debugging usage */
#ifdef CONFIG_ZMK_USB_LOGGING
static inline void print_register(const struct i2c_dt_spec *i2c, uint8_t reg, const char* prefix)
{
  uint8_t status;
	int err = i2c_reg_read_byte_dt(i2c, reg, &status);
	if (err < 0) {
    LOG_ERR("Debug can's read register: 0x%x", reg);
		return;
	}
	LOG_INF("%s: register 0x%x = 0x%x", prefix, reg, status);
  return;
}
#endif

/* Write a single bit in a register without touching other bits */
static inline int kscan_cap1203_bit_write(const struct i2c_dt_spec *i2c, uint8_t reg, \
                                   uint8_t pos, bool enable)
{
  uint8_t val;
  int err;

  if((err=i2c_reg_read_byte_dt(i2c, reg, &val))) {
    return err;
  }

  if( enable )
    WRITE_BIT(val, pos, 1);
  else
    WRITE_BIT(val, pos, 0);

  return i2c_reg_write_byte_dt(i2c, reg, val);
}

/* Check the status of the sensor by comparing the vid, revision and pid with the datasheet */
static inline int kscan_cap1203_check_firmware(const struct device *dev)
{
	const struct kscan_cap1203_config *config = dev->config;

  uint8_t val;
  int err;

  if((err=i2c_reg_read_byte_dt(&config->i2c, REG_PRODUCT_ID, &val))) {
    LOG_INF("Can't read register: product id");
    return err;
  }
  else if( val != PRODUCT_ID ) {
    LOG_INF("Unequal product id 0x%x (expected: 0x%x)", val, PRODUCT_ID);
    return -EIO;
  }

  if((err=i2c_reg_read_byte_dt(&config->i2c, REG_VENDOR_ID, &val))) {
    LOG_INF("Can't read register: vendor id");
    return err;
  }
  else if( val != VENDOR_ID ) {
    LOG_INF("Unequal vendor id 0x%x (expected: 0x%x)", val, VENDOR_ID);
    return -EIO;
  }

  return 0;
}

/* Config multiple touch circuitry */
inline static int kscan_cap1203_configure_multiple_touch(const struct i2c_dt_spec *i2c, \
                                                         bool enable, \
                                                         int num)
{
  uint8_t val=0;

  if(enable) {
    WRITE_BIT(val, MULT_BLK_EN_POS, 1);

    switch (num) {
    case 1:
      WRITE_BIT(val, B_MULT_T1_POS, 0);
      WRITE_BIT(val, B_MULT_T0_POS, 0);
      break;
    case 2:
      WRITE_BIT(val, B_MULT_T1_POS, 0);
      WRITE_BIT(val, B_MULT_T0_POS, 1);
      break;
    case 3:
      WRITE_BIT(val, B_MULT_T1_POS, 1);
      WRITE_BIT(val, B_MULT_T0_POS, 0);
      break;
    default:
      LOG_ERR("Number of channels is out of range: max=3");
      return -EINVAL;
    }
  }
  else {
    WRITE_BIT(val, MULT_BLK_EN_POS, 0);
  }

  return i2c_reg_write_byte_dt(i2c, REG_MULT_CONFIG, val);
}
/* Enable/disable generation of release interrupt */
static int kscan_cap1203_enable_release_interrupt(const struct i2c_dt_spec *i2c,\
                                                  bool enable)
{
  return kscan_cap1203_bit_write(i2c, REG_CONFIGURATION_2, RELEASE_INT_POS, !enable);
}


/* De-assert alert line and clear the sensor status input register */
static int kscan_cap1203_clear_interrupt(const struct i2c_dt_spec *i2c)
{
	uint8_t ctrl;
	int r;

	r = i2c_reg_read_byte_dt(i2c, REG_MAIN_CONTROL, &ctrl);
	if (r < 0) {
		return r;
	}

	ctrl = ctrl & ~CONTROL_INT;

	return i2c_reg_write_byte_dt(i2c, REG_MAIN_CONTROL, ctrl);
}

/* Enable/disable the alert line */
static int kscan_cap1203_enable_interrupt(const struct i2c_dt_spec *i2c, bool enable)
{
	uint8_t intr = enable ? INTERRUPT_ENABLE : INTERRUPT_DISABLE;

	return i2c_reg_write_byte_dt(i2c, REG_INTERRUPT_ENABLE, intr);
}

/* determine the press status based on latest sensor input */
// arg1 new_input: the current status input register value
// arg2 ch: the sensor channel to be tested
// arg3 pressed: whether the state change is pressed or released
// return: true if there is state change, otherwise false
inline static bool kscan_cap1203_change_state(uint8_t *old_input,  \
                                              uint8_t *new_input, \
                                              int  ch, \
                                              bool *pressed)
{

  if((*old_input & BIT(ch) ) != (*new_input & BIT(ch))) {
    *pressed = *new_input & BIT(ch);
    return true;
  }
  return false;
}

/* read the current touch status */
static int kscan_cap1203_read(const struct device *dev)
{
	const struct kscan_cap1203_config *config = dev->config;
	struct kscan_cap1203_data *data = dev->data;
	int r;
	uint8_t input;

  LOG_INF("Beginning of ISR:");
#ifdef CONFIG_CAP1203_BUTTON_MODE
	bool pressed;

  // read general status
#ifdef CONFIG_ZMK_USB_LOGGING
  print_register(&config->i2c, REG_GENERAL_STATUS, "General Status");
#endif

  // read sensor input status
	r = i2c_reg_read_byte_dt(&config->i2c, REG_INPUT_STATUS, &input);
	if (r < 0) {
		return r;
	}
	LOG_INF("Initial input status: 0x%x (old: 0x%x)", input, data->touch_state);

  // check the state change, only pressed event can be discovered at this stage
  for( int ch=0; ch < 3; ch++ ) {
    if(kscan_cap1203_change_state(&data->touch_state, &input, ch, &pressed)) {
      data->callback(dev, 0, ch, pressed);
      LOG_INF("Pad %d %s", ch+1, pressed ? "pressed" : "released");
    }
  }

	// Clear INT bit to clear SENSOR INPUT STATUS bits and dis-claim interrupt line
	r = kscan_cap1203_clear_interrupt(&config->i2c);
	if (r < 0) {
		return r;
	}

  // read sensor input status again, this time to discover release event
	r = i2c_reg_read_byte_dt(&config->i2c, REG_INPUT_STATUS, &data->touch_state);
	if (r < 0) {
		return r;
	}
	LOG_INF("Input status after clearing: 0x%x (old: 0x%x)", data->touch_state, input);

  // if interrput is triggered by release, there will be state change after claering
  for( int ch=0; ch < 3; ch++ ) {
    if(kscan_cap1203_change_state(&input, &data->touch_state, ch, &pressed)) {
      data->callback(dev, 0, ch, pressed);
      LOG_INF("Pad %d %s", ch+1, pressed ? "pressed" : "released");
    }
  }

#else // slider or mix mode
  // read general status
#ifdef CONFIG_ZMK_USB_LOGGING
  data->update_counter++;
  print_register(&config->i2c, REG_GENERAL_STATUS, "General Status");
  if(!data->touch_state)
    LOG_INF("Start of one cycle");
#endif

	struct slider_data *slider_data = dev->data;
  // get the curren timestamp
  int64_t timestamp = k_uptime_get();

#ifdef CONFIG_CAP1203_MIX_MODE
  // get cycle start time
  if(data->touch_state == 0) {
    data->update_duration = k_uptime_get();
  }
#endif

  // First reading of sensor input status, only pressed info updated
	r = i2c_reg_read_byte_dt(&config->i2c, REG_INPUT_STATUS, &input);
	if (r < 0) {
		return r;
	}
  // Clear INT bit to update release info
  r = kscan_cap1203_clear_interrupt(&config->i2c);
  if (r < 0) {
    return r;
  }
  // Second reading of sensor input status, which includes release info
  r = i2c_reg_read_byte_dt(&config->i2c, REG_INPUT_STATUS, &input);
  if (r < 0) {
    return r;
  }
  LOG_INF("Update %d status: 0x%x", data->update_counter, input);

  // Process the slider pattern, unknown patterns are skipped
  if(input) { // skip last update

#ifdef CONFIG_CAP1203_MIX_MODE
    data->press_state |= input;
#endif

    if(data->touch_state != 0) { // also skip the first update for slider
      slider_data->delta_position = slider_position[input] - slider_data->step;
      slider_data->delta_time = timestamp - slider_data->pre_ts;

      if (slider_data->delta_position != 0) {
#ifdef CONFIG_ZMK_USB_LOGGING
        slider_data->acc_position += slider_data->delta_position;
#endif

#ifdef CONFIG_CAP1203_MIX_MODE
        if(!data->is_slide) {
          data->is_slide = true;
          LOG_INF("Discover slide movement!");
        }
#endif

        // slider callback to process the deltas
        if (slider_data->callback)
          slider_data->callback(dev, config->invert_direction ? -slider_data->delta_position : slider_data->delta_position,
                                slider_data->delta_time);
      }
      LOG_INF("dPos: %d, dT: %d ms", slider_data->delta_position, slider_data->delta_time);
    }

  }

  // book-keeping stuff
  data->touch_state = input;
  slider_data->step = slider_position[input];
  slider_data->pre_ts = timestamp;

  if(!input) { // reach the end of slide gesture
#ifdef CONFIG_CAP1203_MIX_MODE
    // calculate the cycle duration  in ms
    data->update_duration = k_uptime_get() - data->update_duration;

    if(data->is_slide) {
      LOG_INF("End of one cycle. Slide manuover, total delta: %d", slider_data->acc_position);
      data->is_slide = false;
    }
    else if( data->update_duration <= 200 ) { // send button tap
      LOG_INF("End of one cycle. Tap manuover, pressed buttons: %d", data->press_state);

      // send press state
      for(int i=0; i < 3; i++) {
        if(BIT(i) & data->press_state) {
          data->callback(dev, 0, i, true);
        }
      }

      k_msleep(5);

      // send release state
      for(int i=0; i < 3; i++) {
        if(BIT(i) & data->press_state) {
          data->callback(dev, 0, i, false);
        }
      }

      data->press_state = 0;
    }
    else
      LOG_INF("End of one cycle. Cancelled slide manueover");
#else
      LOG_INF("End of one cycle. Total delta: %d", slider_data->acc_position);
#endif

    slider_data->step = 0;
    slider_data->delta_position = 0;
    slider_data->acc_position = 0;
    slider_data->delta_time = 0;
    data->update_counter = 0;
  }
#endif

  LOG_INF("End of ISR.\n");
	return 0;
}

/* the actual work to process each alert or polling */
static void kscan_cap1203_work_handler(struct k_work *work)
{
	struct kscan_cap1203_data *data = CONTAINER_OF(work, struct kscan_cap1203_data, work);

	kscan_cap1203_read(data->dev);
}

/* ISR handler: immediatly dispatch the job to the work handler */
static void kscan_cap1203_irq_handler(const struct device *dev,
				struct gpio_callback *cb, uint32_t pins)
{
	struct kscan_cap1203_data *data = CONTAINER_OF(cb, struct kscan_cap1203_data, int_gpio_cb);

	k_work_submit(&data->work);
}

/* Timer handler in poll mode: immediately dispatch the job to the actual work handler */
#if USE_POLLING
static void kscan_cap1203_timer_handler(struct k_timer *timer)
{
	struct kscan_cap1203_data *data = CONTAINER_OF(timer, struct kscan_cap1203_data, timer);

	k_work_submit(&data->work);
}
#endif

static int kscan_cap1203_configure(const struct device *dev,
			     kscan_callback_t callback)
{
	struct kscan_cap1203_data *data = dev->data;
	const struct kscan_cap1203_config *config = dev->config;

	data->callback = callback;

	if (config->int_gpio.port != NULL) {
		int r;

		/* Clear pending interrupt */
		r = kscan_cap1203_clear_interrupt(&config->i2c);
		if (r < 0) {
			LOG_ERR("Could not clear interrupt");
			return r;
		}

		r = kscan_cap1203_enable_interrupt(&config->i2c, true);
		if (r < 0) {
			LOG_ERR("Could not configure interrupt");
			return r;
		}
	}

	return 0;
}

static int kscan_cap1203_enable_callback(const struct device *dev)
{
	struct kscan_cap1203_data *data = dev->data;

	const struct kscan_cap1203_config *config = dev->config;

	if (config->int_gpio.port != NULL) {
    LOG_DBG("enable interrupt callback");
		gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
	}
#if USE_POLLING
	else {
    LOG_DBG("enable timer callback");
		k_timer_start(&data->timer, K_MSEC(CONFIG_ZMK_KSCAN_CAP1203_PERIOD),
			      K_MSEC(CONFIG_ZMK_KSCAN_CAP1203_PERIOD));
	}
#endif
	return 0;
}

static int kscan_cap1203_disable_callback(const struct device *dev)
{
	struct kscan_cap1203_data *data = dev->data;

	const struct kscan_cap1203_config *config = dev->config;

	if (config->int_gpio.port != NULL) {
    LOG_DBG("disable interrupt callback");
		gpio_remove_callback(config->int_gpio.port, &data->int_gpio_cb);
	}
#if USE_POLLING
	else {
    LOG_DBG("disable timer callback");
		k_timer_stop(&data->timer);
	}
#endif
	return 0;
}

static int kscan_cap1203_init(const struct device *dev)
{
	const struct kscan_cap1203_config *config = dev->config;
	struct kscan_cap1203_data *data = dev->data;
	int r;

  // check readiness of i2c bus
	if (!device_is_ready(config->i2c.bus)) {
		LOG_ERR("I2C controller device not ready");
		return -ENODEV;
	}

  // init data structure
	data->dev = dev;

	k_work_init(&data->work, kscan_cap1203_work_handler);

  // init alert interrupt pin or poll timer
	if (config->int_gpio.port != NULL) {
    LOG_DBG("Init interrupt mode");
		if (!device_is_ready(config->int_gpio.port)) {
			LOG_ERR("Interrupt GPIO controller device not ready");
			return -ENODEV;
		}

		r = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (r < 0) {
			LOG_ERR("Could not confighure interrupt GPIO pin");
			return r;
		}

		r = gpio_pin_interrupt_configure_dt(&config->int_gpio,
						   GPIO_INT_EDGE_TO_ACTIVE);
		if (r < 0) {
			LOG_ERR("Could not configure interrupt GPIO interrupt");
			return r;
		}

		gpio_init_callback(&data->int_gpio_cb, kscan_cap1203_irq_handler,
				   BIT(config->int_gpio.pin));
	}
#if USE_POLLING
	else {
    LOG_DBG("Init poll mode");
		k_timer_init(&data->timer, kscan_cap1203_timer_handler, NULL);

    // disable alert function in poll mode
		r = kscan_cap1203_enable_interrupt(&config->i2c, false);
		if (r < 0) {
			LOG_ERR("Could not configure interrupt");
			return r;
		}
	}
#endif

  // configure release interrupt
  r = kscan_cap1203_enable_release_interrupt(&config->i2c, true);
  if(r) {
    LOG_ERR("Could not configure release interrupt");
    return r;
  }

  // configure repeat interrupt
	r = i2c_reg_write_byte_dt(&config->i2c, REG_REPEAT_ENABLE, REPEAT_DISABLE);
	if (r < 0) {
    LOG_ERR("Could not configure repeat-rate interrupt");
		return r;
	}

  // configure multi-touch circuitry
  r = kscan_cap1203_configure_multiple_touch(&config->i2c, true, 3);
	if (r < 0) {
    LOG_ERR("Could not configure multi-touch circuitry");
		return r;
	}

  // End of init
  LOG_INF("Init success");

	return 0;
}

#if defined(CONFIG_CAP1203_MIX_MODE) || defined(CONFIG_CAP1203_SLIDER_MODE)
static void kscan_cap1203_slider_configure(const struct device *dev,
                                           kscan_slider_callback_t callback,
                                           int id)
{
	struct slider_data *slider_data = dev->data;
  slider_data->callback = callback;
  slider_data->id = id;

  kscan_cap1203_enable_callback(dev);
}

static struct kscan_slider_api kscan_cap1203_slider_api = {
  {
	.config = kscan_cap1203_configure,
	.enable_callback = kscan_cap1203_enable_callback,
	.disable_callback = kscan_cap1203_disable_callback,
  },
  .slider_config = kscan_cap1203_slider_configure
};

#define KSCAN_CAP1203_INIT(index)							\
	static const struct kscan_cap1203_config kscan_cap1203_config_##index = {		\
		.i2c = I2C_DT_SPEC_INST_GET(index),				\
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(index, int_gpios, {0}),	\
    .invert_direction = DT_INST_PROP(index, invert_direction),        \
	};									\
	static struct kscan_cap1203_data kscan_cap1203_data_##index;			\
	DEVICE_DT_INST_DEFINE(index, kscan_cap1203_init, NULL,			\
			      &kscan_cap1203_data_##index, &kscan_cap1203_config_##index,	\
                        APPLICATION, CONFIG_ZMK_KSCAN_INIT_PRIORITY, \
                        &(kscan_cap1203_slider_api.kscan_api));
#else // pure button mode
static const struct kscan_driver_api kscan_cap1203_driver_api = {
	.config = kscan_cap1203_configure,
	.enable_callback = kscan_cap1203_enable_callback,
	.disable_callback = kscan_cap1203_disable_callback,
};
#define KSCAN_CAP1203_INIT(index)                                       \
	static const struct kscan_cap1203_config kscan_cap1203_config_##index = {		\
		.i2c = I2C_DT_SPEC_INST_GET(index),				\
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(index, int_gpios, {0}),	\
	};									\
	static struct kscan_cap1203_data kscan_cap1203_data_##index;			\
	DEVICE_DT_INST_DEFINE(index, kscan_cap1203_init, NULL,			\
			      &kscan_cap1203_data_##index, &kscan_cap1203_config_##index,	\
			      APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY ,		\
			      &kscan_cap1203_driver_api);
#endif

DT_INST_FOREACH_STATUS_OKAY(KSCAN_CAP1203_INIT)
