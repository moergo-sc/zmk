/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT pixart_paw3395

#include <kernel.h>
#include <sys/byteorder.h>
#include "paw3395.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(paw3395, CONFIG_PAW3395_LOG_LEVEL);

/* todo: add reset pin into the init sequence */

/* Timings (in us) used in SPI communication. Since MCU should not do other tasks during wait, k_busy_wait is used instead of k_sleep */
// - sub-us time is rounded to us, due to the limitation of k_busy_wait, see : https://github.com/zephyrproject-rtos/zephyr/issues/6498
#define T_NCS_SCLK	1			/* 120 ns (rounded to 1us) */
#define T_SCLK_NCS_WR	1 /* 1 us */
#define T_SRAD		2			/* 2 us */
#define T_SRAD_MOTBR	2			/* same as T_SRAD */
#define T_SRX		2 /* 2 us */
#define T_SWX		5 /*  5 us */
#define T_BEXIT		1			/* 500 ns (rounded to 1us)*/

/* Sensor registers (addresses) */
// common registers as pmw3360
#define PAW3395_REG_PRODUCT_ID			0x00
#define PAW3395_REG_REVISION_ID			0x01
#define PAW3395_REG_MOTION			  0x02
#define PAW3395_REG_DELTA_X_L			0x03
#define PAW3395_REG_DELTA_X_H			0x04
#define PAW3395_REG_DELTA_Y_L			0x05
#define PAW3395_REG_DELTA_Y_H			0x06
#define PAW3395_REG_SQUAL			    0x07
#define PAW3395_REG_RAW_DATA_SUM		0x08
#define PAW3395_REG_MAXIMUM_RAW_DATA		0x09
#define PAW3395_REG_MINIMUM_RAW_DATA		0x0A
#define PAW3395_REG_SHUTTER_LOWER		0x0B
#define PAW3395_REG_SHUTTER_UPPER		0x0C

#define PAW3395_REG_OBSERVATION			0x15
#define PAW3395_REG_MOTION_BURST		0x16

// power-up reset and shutdown registers
#define PAW3395_REG_POWER_UP_RESET		0x3A
#define PAW3395_REG_SHUTDOWN			    0x3B

// rest mode register
#define PAW3395_REG_PERFORMANCE 0x40

// resolution/cpi registers
#define PAW3395_REG_SET_RESOLUTION         0x47
#define PAW3395_REG_RESOLUTION_X_LOW       0x48
#define PAW3395_REG_RESOLUTION_X_HIGH      0x49
#define PAW3395_REG_RESOLUTION_Y_LOW       0x4A
#define PAW3395_REG_RESOLUTION_Y_HIGH      0x4B

// other registers
#define PAW3395_REG_ANGLE_SNAP      0x56
#define PAW3395_REG_RAWDATA_OUTPUT  0x58
#define PAW3395_REG_RAWDATA_STATUS  0x59
#define PAW3395_REG_RIPPLE_CONTROL  0x5A
#define PAW3395_REG_AXIS_CONTROL    0x5B
#define PAW3395_REG_MOTION_CONTROL  0x5C
#define PAW3395_REG_INVERSE_PRODUCT_ID		0x5F

// rest mode related
#define PAW3395_REG_RUN_DOWNSHIFT		0x77
#define PAW3395_REG_REST1_PERIOD		0x78
#define PAW3395_REG_REST1_DOWNSHIFT	0x79
#define PAW3395_REG_REST2_PERIOD		0x7A
#define PAW3395_REG_REST2_DOWNSHIFT	0x7B
#define PAW3395_REG_REST3_PERIOD		0x7C
#define PAW3395_REG_RUN_DOWNSHIFT_MULT  0x7D
#define PAW3395_REG_REST_DOWNSHIFT_MULT 0x7E

// the following registers need special setting procedure
#define PAW3395_REG_ANGLE_TUNE1_H			0x05
#define PAW3395_REG_ANGLE_TUNE1_L			0x77
#define PAW3395_REG_ANGLE_TUNE2_H			0x05
#define PAW3395_REG_ANGLE_TUNE2_L			0x78
#define PAW3395_REG_LIFT_CONFIG_H			0x0C
#define PAW3395_REG_LIFT_CONFIG_L			0x4E

// the mode register is used in run mode selection
#define PAW3395_REG_RUN_MODE   0x40

/* Sensor identification values */
#define PAW3395_PRODUCT_ID			0x51

/* Power-up register commands */
#define PAW3395_POWERUP_CMD_RESET  0x5A

/* Max register count readable in a single motion burst */
#define PAW3395_MAX_BURST_SIZE			12

/* Register count used for reading a single motion burst */
#define PAW3395_BURST_SIZE			6

/* Position of X in motion burst data */
#define PAW3395_DX_POS				2
#define PAW3395_DY_POS				4

/* Rest_En position in Performance register. */
#define PAW3395_REST_EN_POS			7

/* cpi/resolution range */
#define PAW3395_MAX_CPI				26000
#define PAW3395_MIN_CPI				50
#define PAW3395_SET_RESOLUTION_CMD 0x01
#define PAW3395_RIPPLE_CONTROL_EN_POS 7

/* write command bit position */
#define SPI_WRITE_BIT				BIT(7)

/* Helper macros used to convert sensor values. */
#define PAW3395_SVALUE_TO_CPI(svalue) ((uint32_t)(svalue).val1)
#define PAW3395_SVALUE_TO_TIME(svalue) ((uint32_t)(svalue).val1)
#define PAW3395_SVALUE_TO_RUNMODE(svalue) ((uint32_t)(svalue).val1)
#define PAW3395_SVALUE_TO_BOOL(svalue) ((svalue).val1 != 0)


/* setting registers, defined in paw3395_priv.c */
extern const size_t paw3395_pwrup_registers_length1;
extern const uint8_t paw3395_pwrup_registers_addr1[];
extern const uint8_t paw3395_pwrup_registers_data1[];
extern const size_t paw3395_pwrup_registers_length2;
extern const uint8_t paw3395_pwrup_registers_addr2[];
extern const uint8_t paw3395_pwrup_registers_data2[];


extern const size_t paw3395_mode_registers_length[];
extern const uint8_t* paw3395_mode_registers_addr[];
extern const uint8_t* paw3395_mode_registers_data[];

//////// Sensor initialization steps definition //////////
// init is done in non-blocking manner (i.e., async), a //
// delayable work is defined for this purpose           //
enum paw3395_init_step {
  ASYNC_INIT_STEP_POWER_UP, // reset cs line and assert power-up reset
  ASYNC_INIT_STEP_LOAD_SETTING, // load register setting
  ASYNC_INIT_STEP_CONFIGURE, // set other registes like cpi and donwshift time (run, rest1, rest2) and clear motion registers

  ASYNC_INIT_STEP_COUNT // end flag
};

/* Timings (in ms) needed in between steps to allow each step finishes succussfully. */
// - Since MCU is not involved in the sensor init process, i is allowed to do other tasks.
//   Thus, k_sleep or delayed schedule can be used.
static const int32_t async_init_delay[ASYNC_INIT_STEP_COUNT] = {
	[ASYNC_INIT_STEP_POWER_UP]         = 50, // required in spec
	[ASYNC_INIT_STEP_LOAD_SETTING]    = 10,  // 5ms required in spec, tests show >10ms
	[ASYNC_INIT_STEP_CONFIGURE]        = 0,
};

static int paw3395_async_init_power_up(const struct device *dev);
static int paw3395_async_init_load_setting(const struct device *dev);
static int paw3395_async_init_configure(const struct device *dev);

static int (* const async_init_fn[ASYNC_INIT_STEP_COUNT])(const struct device *dev) = {
	[ASYNC_INIT_STEP_POWER_UP] = paw3395_async_init_power_up,
	[ASYNC_INIT_STEP_LOAD_SETTING] = paw3395_async_init_load_setting,
	[ASYNC_INIT_STEP_CONFIGURE] = paw3395_async_init_configure,
};

//////// Function definitions //////////

// checked and keep
static int spi_cs_ctrl(const struct device *dev, bool enable)
{
	const struct pixart_config *config = dev->config;
	int err;

	if (!enable) {
		k_busy_wait(T_NCS_SCLK);
	}

	err = gpio_pin_set_dt(&config->cs_gpio, (int)enable);
	if (err) {
		LOG_ERR("SPI CS ctrl failed");
	}

	if (enable) {
		k_busy_wait(T_NCS_SCLK);
	}

	return err;
}


// checked and keep
static int reg_read(const struct device *dev, uint8_t reg, uint8_t *buf)
{
	int err;
	/* struct pixart_data *data = dev->data; */
	const struct pixart_config *config = dev->config;

	__ASSERT_NO_MSG((reg & SPI_WRITE_BIT) == 0);

	err = spi_cs_ctrl(dev, true);
	if (err) {
		return err;
	}

	/* Write register address. */
	const struct spi_buf tx_buf = {
		.buf = &reg,
		.len = 1
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	err = spi_write_dt(&config->bus, &tx);
	if (err) {
		LOG_ERR("Reg read failed on SPI write");
		return err;
	}

	k_busy_wait(T_SRAD);

	/* Read register value. */
	struct spi_buf rx_buf = {
		.buf = buf,
		.len = 1,
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1,
	};

	err = spi_read_dt(&config->bus, &rx);
	if (err) {
		LOG_ERR("Reg read failed on SPI read");
		return err;
	}

	err = spi_cs_ctrl(dev, false);
	if (err) {
		return err;
	}

	k_busy_wait(T_SRX);

  // todo: needed?
	/* data->last_read_burst = false; */

	return 0;
}

// checked and keep
static int reg_write(const struct device *dev, uint8_t reg, uint8_t val)
{
	int err;
	/* struct pixart_data *data = dev->data; */
	const struct pixart_config *config = dev->config;

	__ASSERT_NO_MSG((reg & SPI_WRITE_BIT) == 0);

	err = spi_cs_ctrl(dev, true);
	if (err) {
		return err;
	}

	uint8_t buf[] = {
		SPI_WRITE_BIT | reg,
		val
	};
	const struct spi_buf tx_buf = {
		.buf = buf,
		.len = ARRAY_SIZE(buf)
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	err = spi_write_dt(&config->bus, &tx);
	if (err) {
		LOG_ERR("Reg write failed on SPI write");
		return err;
	}

	k_busy_wait(T_SCLK_NCS_WR);

	err = spi_cs_ctrl(dev, false);
	if (err) {
		return err;
	}

	k_busy_wait(T_SWX);

	/* data->last_read_burst = false; */

	return 0;
}

static int motion_burst_read(const struct device *dev, uint8_t *buf,
			     size_t burst_size)
{
	int err;
	/* struct pixart_data *data = dev->data; */
	const struct pixart_config *config = dev->config;

	__ASSERT_NO_MSG(burst_size <= PAW3395_MAX_BURST_SIZE);

	/* Write any value to motion burst register only if there have been
	 * other SPI transmissions with sensor since last burst read.
	 */
  // todo: needed or not?
	/* if (!data->last_read_burst) { */
	/* 	err = reg_write(dev, PAW3395_REG_MOTION_BURST, 0x00); */
	/* 	if (err) { */
	/* 		return err; */
	/* 	} */
	/* } */

	err = spi_cs_ctrl(dev, true);
	if (err) {
		return err;
	}

	/* Send motion burst address */
	uint8_t reg_buf[] = {
		PAW3395_REG_MOTION_BURST
	};
	const struct spi_buf tx_buf = {
		.buf = reg_buf,
		.len = ARRAY_SIZE(reg_buf)
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

  // todo: after sending the address, mosi should be held static until burst read is complete? how to do it in zephyr
	err = spi_write_dt(&config->bus, &tx);
	if (err) {
		LOG_ERR("Motion burst failed on SPI write");
		return err;
	}

	k_busy_wait(T_SRAD_MOTBR);

	const struct spi_buf rx_buf = {
		.buf = buf,
		.len = burst_size,
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1
	};

	err = spi_read_dt(&config->bus, &rx);
	if (err) {
		LOG_ERR("Motion burst failed on SPI read");
		return err;
	}

	err = spi_cs_ctrl(dev, false);
	if (err) {
		return err;
	}

	/* Terminate burst */
	k_busy_wait(T_BEXIT);

	/* data->last_read_burst = true; */

	return 0;
}

/** Writing an array of registers in sequence, used in power-up register initialization and running mode switching */
static int burst_write(const struct device *dev, const uint8_t *addr, const uint8_t *buf, size_t size)
{
	int err;

	/* Write data */
	for (size_t i = 0; i < size; i++) {
    err = reg_write(dev, addr[i], buf[i]);

		if (err) {
			LOG_ERR("Burst write failed on SPI write (data)");
			return err;
		}
	}

	/* struct pixart_data *data = dev->data; */
	/* data->last_read_burst = false; */

	return 0;
}

static int check_product_id(const struct device *dev)
{
	uint8_t product_id=0x01;
	int err = reg_read(dev, PAW3395_REG_PRODUCT_ID, &product_id);
	if (err) {
		LOG_ERR("Cannot obtain product id");
		return err;
	}

	if (product_id != PAW3395_PRODUCT_ID) {
		LOG_ERR("Incorrect product id 0x%x (expecting 0x%x)!", product_id, PAW3395_PRODUCT_ID);
		return -EIO;
	}

  return 0;
}

static int upload_pwrup_settings(const struct device *dev)
{
  LOG_INF("Upload firmware settings...");

  // stage 1: configure the first 137 registers
  LOG_INF("stag 1: upload the first 137 registers");
  int err;
  err = burst_write(dev, paw3395_pwrup_registers_addr1, \
                    paw3395_pwrup_registers_data1, paw3395_pwrup_registers_length1);
  if(err) {
    LOG_ERR("Can't setting first group of registers");
    return err;
  }

  // stage 2: read register 0x6C at 1ms interval until value 0x80 is returned
  //          or timeout after 60 times
  LOG_INF("stag 2: poll until 0x80 returned");
  uint8_t value = 0;
  int count = 0;
  while( count < 60 ) {

    // wait for 1ms befor read (timing accuracy 1% is required)
    k_busy_wait(1000);
    /* k_msleep(1); */

    if (reg_read(dev, 0x6C, &value)) {
      LOG_ERR("Failed to read register 0x6C");
      return err;
    }
    LOG_DBG("%d poll returns 0x%x", count+1, value);

    if( value == 0x80 )
      break;

    count++;
  }

  // do some setting if 0x80 is not returned within 60 poll
  if( value != 0x80 ) {
    LOG_INF("value 0x80 is not returned within 60 polls, use alternative setting");
    uint8_t addr[] = {0x7F, 0x6C, 0x7F};
    uint8_t data[] = {0x14, 0x00, 0x00};

    err = burst_write(dev, addr, data, 3);
    if(err) {
      LOG_ERR("Can't setting the backup registers");
      return err;
    }
  }

  // stage 3: configure the remaining 5 rigisters
  LOG_INF("stag 3: upload the remaining 5 registers");
  err = burst_write(dev, paw3395_pwrup_registers_addr2,\
                    paw3395_pwrup_registers_data2, paw3395_pwrup_registers_length2);
  if(err) {
    LOG_ERR("Can't setting the second group of registers");
    return err;
  }

  // stage 4: check the product id
  LOG_INF("stag 4: confirm the setting by checking the product id");
	err = check_product_id(dev);
	if (err) {
		LOG_ERR("Failed checking product id");
		return err;
	}

  LOG_INF("Upload power-up register settins done");

  return 0;
}

static int set_run_mode(const struct device *dev, enum paw3395_run_mode run_mode)
{
  int err;
  uint32_t mode_idx = (uint32_t)run_mode;

  if(mode_idx >= RUN_MODE_COUNT) {
		LOG_ERR("Unknown attribute");
		return -ENOTSUP;
  }

  // stage 1: write a series of registers
  err = burst_write(dev, paw3395_mode_registers_addr[mode_idx],\
                    paw3395_mode_registers_data[mode_idx],\
                    paw3395_mode_registers_length[mode_idx]);

  // stage 2: read run_mode register and set corresponding bits
	uint8_t value;
  if(mode_idx == GAME_MODE) {
    value = 0x83;
  }
  else{
    err = reg_read(dev, PAW3395_REG_RUN_MODE, &value);
    if (err) {
      LOG_ERR("Failed to read RUN_MODE register");
      return err;
    }
  }

  switch (mode_idx) {
  case HP_MODE:
    WRITE_BIT(value, 0, 0);
    WRITE_BIT(value, 1, 0);
    LOG_INF("Enable high-performance mode");
    break;
  case LP_MODE:
    WRITE_BIT(value, 0, 1);
    WRITE_BIT(value, 1, 0);
    LOG_INF("Enable low-performance mode");
    break;
  case OFFICE_MODE:
    WRITE_BIT(value, 0, 0);
    WRITE_BIT(value, 1, 1);
    LOG_INF("Enable office mode");
    break;
  case GAME_MODE:
    LOG_INF("Enable gaming mode");
    break;
  default:
		LOG_ERR("Unknown RUN mode");
		return -ENOTSUP;
  }

  // stage 3: write back to run_mode register
	err = reg_write(dev, PAW3395_REG_RUN_MODE, value);
	if (err) {
		LOG_ERR("Failed to set run mode");
	}
  return err;
}

/* set cpi (x, y seperately): axis true for x, false for y */
static int set_cpi(const struct device *dev, uint32_t cpi, bool axis)
{
	/* Set resolution with CPI step of 50 cpi
	 * 0x0000: 50 cpi (minimum cpi)
	 * 0x0001: 100 cpi
	 * :
	 * 0x0063: 5000 cpi (default cpi)
	 * :
	 * 0x0207: 26000 cpi (maximum cpi)
	 */

	if ((cpi > PAW3395_MAX_CPI) || (cpi < PAW3395_MIN_CPI)) {
		LOG_ERR("CPI value %u out of range", cpi);
		return -EINVAL;
	}

	// Convert CPI to register value
	uint16_t value = (cpi / 50) - 1;
	LOG_INF("Setting %s-CPI to %u (reg value 0x%x)", axis ? "X" : "Y", cpi, value);

  // seperate the two bytes
	uint8_t buf[2];
	sys_put_le16(value, buf);

  // upload the new value
  int err;
  if (axis) { // x-cpi
    uint8_t addr[2] = {PAW3395_REG_RESOLUTION_X_LOW, PAW3395_REG_RESOLUTION_X_HIGH};
    err = burst_write(dev, addr, buf, 2);
  }
  else { // y-cpi
    uint8_t addr[2] = {PAW3395_REG_RESOLUTION_Y_LOW, PAW3395_REG_RESOLUTION_Y_HIGH};
    err = burst_write(dev, addr, buf, 2);
  }
  if (err) {
    LOG_ERR("Failed to upload %s-CPI", axis ? "X" : "Y");
  }

  /* set the cpi */
  if ( cpi > 9000 ) {
    LOG_INF("Enable ripple control, since cpi is too large");

    err = reg_read(dev, PAW3395_REG_RIPPLE_CONTROL, buf);
    if (err) {
      LOG_ERR("Failed to read RIPPLE_CONTROL register");
      return err;
    }

    WRITE_BIT(buf[0], PAW3395_RIPPLE_CONTROL_EN_POS, 1);
    err = reg_write(dev, PAW3395_REG_RIPPLE_CONTROL, buf[0]);
    if (err) {
      LOG_ERR("Failed to enable ripple control");
    }
  }

  /* set the cpi */
	err = reg_write(dev, PAW3395_REG_SET_RESOLUTION, 0x01);
	if (err) {
		LOG_ERR("Failed to set CPI");
	}

	return err;
}

/* Set sampling rate in each mode (in ms) */
static int set_sample_time(const struct device *dev, uint8_t reg_addr, uint32_t sample_time)
{
	uint32_t maxtime, mintime;
  switch (reg_addr) {
  case PAW3395_REG_REST1_PERIOD:
    mintime = 1;
    maxtime = 255;
    break;
  case PAW3395_REG_REST2_PERIOD:
    mintime = 4;
    maxtime = 4*255;
    break;
  case PAW3395_REG_REST3_PERIOD:
    mintime = 8;
    maxtime = 8*255;
    break;
  default:
    LOG_WRN("unrecognizable rest mode register");
    return -ENOTSUP;
  }

	if ((sample_time > maxtime) || (sample_time < mintime)) {
		LOG_WRN("Sample time %u out of range [%u, %u]", sample_time, mintime, maxtime);
		return -EINVAL;
	}

  uint8_t value = sample_time / mintime;
	LOG_INF("Set sample time to %u ms (reg value: 0x%x)", sample_time, value);

	/* The sample time is (reg_value * mintime ) ms. 0x00 is rounded to 0x1 */
  int err = reg_write(dev, reg_addr, value);
	if (err) {
		LOG_ERR("Failed to change sample time");
	}

	return err;
}


/* time unit: ms */
static int set_downshift_time(const struct device *dev, uint8_t reg_addr, uint32_t time)
{
	/* Set downshift time in ms:
	 * - Run downshift time (from Run to Rest1 mode), default: 1 s
	 * - Rest 1 downshift time (from Rest1 to Rest2 mode), default: ~10 s
	 * - Rest 2 downshift time (from Rest2 to Rest3 mode), default: ~10 min
	 */
	uint32_t maxtime;
	uint32_t mintime;

	switch (reg_addr) {
	case PAW3395_REG_RUN_DOWNSHIFT:
		/*
		 * Run downshift time = PAW3395_REG_RUN_DOWNSHIFT * 256 * 0.05 ms
		 */
		maxtime = 3264;
		mintime = 13; // real value is 12.8, rounded to 13
		break;

	case PAW3395_REG_REST1_DOWNSHIFT:
		/*
		 * Rest1 downshift time = PAW3395_REG_RUN_DOWNSHIFT
		 *                        * 64 * Rest1_sample_period (default 1 ms)
		 */
		maxtime = 255 * 64 * CONFIG_PAW3395_REST1_SAMPLE_TIME_MS;
		mintime = 64 * CONFIG_PAW3395_REST1_SAMPLE_TIME_MS;
		break;

	case PAW3395_REG_REST2_DOWNSHIFT:
		/*
		 * Rest2 downshift time = PAW3395_REG_REST2_DOWNSHIFT
		 *                        * 64 * Rest2 rate (default 100 ms)
		 */
		maxtime = 255 * 64 * CONFIG_PAW3395_REST2_SAMPLE_TIME_MS;
		mintime = 64 * CONFIG_PAW3395_REST2_SAMPLE_TIME_MS;
		break;

	default:
		LOG_ERR("Not supported");
		return -ENOTSUP;
	}

	if ((time > maxtime) || (time < mintime)) {
		LOG_WRN("Downshift time %u out of range", time);
		return -EINVAL;
	}

	__ASSERT_NO_MSG((mintime > 0) && (maxtime/mintime <= UINT8_MAX));

	/* Convert time to register value */
	uint8_t value = time / mintime;

	LOG_INF("Set downshift time to %u ms (reg value 0x%x)", time, value);

	int err = reg_write(dev, reg_addr, value);
	if (err) {
		LOG_ERR("Failed to change downshift time");
	}

	return err;
}

static int set_rest_mode(const struct device *dev, bool enable)
{
	uint8_t value;
	int err = reg_read(dev, PAW3395_REG_PERFORMANCE, &value);

	if (err) {
		LOG_ERR("Failed to read PERFORMANCE register");
		return err;
	}

  // be aware: 0 is enable, 1 is disable
	WRITE_BIT(value, PAW3395_REST_EN_POS, !enable);

	LOG_INF("%sable rest modes", (enable) ? ("En") : ("Dis"));
	err = reg_write(dev, PAW3395_REG_PERFORMANCE, value);

	if (err) {
		LOG_ERR("Failed to set rest mode");
	}

	return err;
}

static int paw3395_attr_set(const struct device *dev, enum sensor_channel chan,
			    enum sensor_attribute attr,
			    const struct sensor_value *val)
{
	struct pixart_data *data = dev->data;
	int err;

	if (unlikely(chan != SENSOR_CHAN_ALL)) {
		return -ENOTSUP;
	}

	if (unlikely(!data->ready)) {
		LOG_DBG("Device is not initialized yet");
		return -EBUSY;
	}

	switch ((uint32_t)attr) {
	case PAW3395_ATTR_X_CPI:
		err = set_cpi(dev, PAW3395_SVALUE_TO_CPI(*val), true);
		break;
	case PAW3395_ATTR_Y_CPI:
		err = set_cpi(dev, PAW3395_SVALUE_TO_CPI(*val), false);
		break;

	case PAW3395_ATTR_REST_ENABLE:
		err = set_rest_mode(dev, PAW3395_SVALUE_TO_BOOL(*val));
		break;

	case PAW3395_ATTR_RUN_DOWNSHIFT_TIME:
		err = set_downshift_time(dev,
					    PAW3395_REG_RUN_DOWNSHIFT,
					    PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_REST1_DOWNSHIFT_TIME:
		err = set_downshift_time(dev,
					    PAW3395_REG_REST1_DOWNSHIFT,
					    PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_REST2_DOWNSHIFT_TIME:
		err = set_downshift_time(dev,
					    PAW3395_REG_REST2_DOWNSHIFT,
					    PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_REST1_SAMPLE_TIME:
		err = set_sample_time(dev,
					 PAW3395_REG_REST1_PERIOD,
					 PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_REST2_SAMPLE_TIME:
		err = set_sample_time(dev,
					 PAW3395_REG_REST2_PERIOD,
					 PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_REST3_SAMPLE_TIME:
		err = set_sample_time(dev,
					 PAW3395_REG_REST3_PERIOD,
					 PAW3395_SVALUE_TO_TIME(*val));
		break;

	case PAW3395_ATTR_RUN_MODE:
		err = set_run_mode(dev,
           PAW3395_SVALUE_TO_RUNMODE(*val));
		break;

	default:
		LOG_ERR("Unknown attribute");
		return -ENOTSUP;
	}

	return err;
}

static int paw3395_async_init_power_up(const struct device *dev)
{
  LOG_INF("async_init_power_up");

  /* needed or not? Reset spi port */
  spi_cs_ctrl(dev, false);
  spi_cs_ctrl(dev, true);

	/* Reset sensor */
	return reg_write(dev, PAW3395_REG_POWER_UP_RESET, PAW3395_POWERUP_CMD_RESET);
}

static int paw3395_async_init_load_setting(const struct device *dev)
{
	LOG_INF("async_init_load_setting");

  return upload_pwrup_settings(dev);
}

static int paw3395_async_init_configure(const struct device *dev)
{
  LOG_INF("async_init_configure");

	int err;

  // run mode
  err = set_run_mode(dev, CONFIG_PAW3395_RUN_MODE);

  // cpi
	if (!err) {
    err = set_cpi(dev, CONFIG_PAW3395_X_CPI, true);
  }

	if (!err) {
    err = set_cpi(dev, CONFIG_PAW3395_Y_CPI, false);
  }

  // sample period, which affects scaling of rest1 downshift time
	if (!err) {
		err = set_sample_time(dev,
					 PAW3395_REG_REST1_PERIOD,
					 CONFIG_PAW3395_REST1_SAMPLE_TIME_MS);
  }

	if (!err) {
		err = set_sample_time(dev,
					 PAW3395_REG_REST2_PERIOD,
					 CONFIG_PAW3395_REST2_SAMPLE_TIME_MS);
  }
	if (!err) {
		err = set_sample_time(dev,
					 PAW3395_REG_REST3_PERIOD,
					 CONFIG_PAW3395_REST3_SAMPLE_TIME_MS);
  }

  // downshift time for each rest mode
	if (!err) {
		err = set_downshift_time(dev,
					    PAW3395_REG_RUN_DOWNSHIFT,
					    CONFIG_PAW3395_RUN_DOWNSHIFT_TIME_MS);
	}

	if (!err) {
		err = set_downshift_time(dev,
					    PAW3395_REG_REST1_DOWNSHIFT,
					    CONFIG_PAW3395_REST1_DOWNSHIFT_TIME_MS);
	}

	if (!err) {
		err = set_downshift_time(dev,
					    PAW3395_REG_REST2_DOWNSHIFT,
					    CONFIG_PAW3395_REST2_DOWNSHIFT_TIME_MS);
	}


  // rest mode
	if (!err) {
    if(IS_ENABLED(CONFIG_PAW3395_ENABLE_REST))
      err = set_rest_mode(dev, true);
    else
      err = set_rest_mode(dev, false);
	}

  // clear motion registers and ready to go
	for (uint8_t reg = 0x02; (reg <= 0x06) && !err; reg++) {
		uint8_t buf[1];
		err = reg_read(dev, reg, buf);
	}

	if (err) {
		LOG_ERR("Config the sensor failed");
		return err;
	}

	return 0;
}

// checked and keep
static void paw3395_async_init(struct k_work *work)
{
	struct pixart_data *data = CONTAINER_OF(work, struct pixart_data,
						 init_work);
	const struct device *dev = data->dev;

	LOG_INF("PAW3395 async init step %d", data->async_init_step);

	data->err = async_init_fn[data->async_init_step](dev);
	if (data->err) {
		LOG_ERR("PAW3395 initialization failed");
	} else {
		data->async_init_step++;

		if (data->async_init_step == ASYNC_INIT_STEP_COUNT) {
			data->ready = true; // sensor is ready to work
			LOG_INF("PAW3395 initialized");
		} else {
			k_work_schedule(&data->init_work,
					K_MSEC(async_init_delay[data->async_init_step]));
		}
	}
}


static void irq_handler(const struct device *gpiob, struct gpio_callback *cb,
			uint32_t pins)
{
	int err;
	struct pixart_data *data = CONTAINER_OF(cb, struct pixart_data,
						 irq_gpio_cb);
	const struct device *dev = data->dev;
	const struct pixart_config *config = dev->config;

  // disable the interrupt line first
	err = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
					      GPIO_INT_DISABLE);
	if (unlikely(err)) {
		LOG_ERR("Cannot disable IRQ");
		k_panic();
	}

  // submit the real handler work
	k_work_submit(&data->trigger_handler_work);
}

static void trigger_handler(struct k_work *work)
{
  LOG_DBG("trigger_handler");

	sensor_trigger_handler_t handler;
	int err = 0;
	struct pixart_data *data = CONTAINER_OF(work, struct pixart_data,
						 trigger_handler_work);
	const struct device *dev = data->dev;
	const struct pixart_config *config = dev->config;

  // 1. the first lock period is used to procoss the trigger
  // if data_ready_handler is non-NULL, otherwise do nothing
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	handler = data->data_ready_handler;
	k_spin_unlock(&data->lock, key);

	if (!handler) {
    LOG_DBG("no trigger handler set by application code");
		return;
	}

	handler(dev, data->trigger);

  // 2. the second lock period is used to resume the interrupt line
  // if data_ready_handler is non-NULL, otherwise keep it inactive
	key = k_spin_lock(&data->lock);
	if (data->data_ready_handler) {
		err = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
						      GPIO_INT_LEVEL_ACTIVE);
	}
	k_spin_unlock(&data->lock, key);

	if (unlikely(err)) {
		LOG_ERR("Cannot re-enable IRQ");
		k_panic();
	}
}

static int paw3395_init_irq(const struct device *dev)
{
  LOG_INF("Configure irq...");

	int err;
	struct pixart_data *data = dev->data;
	const struct pixart_config *config = dev->config;

  // check readiness of irq gpio pin
	if (!device_is_ready(config->irq_gpio.port)) {
		LOG_ERR("IRQ GPIO device not ready");
		return -ENODEV;
	}

  // init the irq pin
	err = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
	if (err) {
		LOG_ERR("Cannot configure IRQ GPIO");
		return err;
	}

  // setup and add the irq callback associated
	gpio_init_callback(&data->irq_gpio_cb, irq_handler,
			   BIT(config->irq_gpio.pin));

	err = gpio_add_callback(config->irq_gpio.port, &data->irq_gpio_cb);
	if (err) {
		LOG_ERR("Cannot add IRQ GPIO callback");
	}

  LOG_INF("Configure irq done");

	return err;
}

static int paw3395_init(const struct device *dev)
{
  LOG_INF("Start initializing...");

	struct pixart_data *data = dev->data;
	const struct pixart_config *config = dev->config;
	int err;

  // init device pointer
	data->dev = dev;

  // init trigger handler work
	k_work_init(&data->trigger_handler_work, trigger_handler);

  // check readiness of spi bus
	if (!spi_is_ready(&config->bus)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

  // check readiness of cs gpio pin and init it to inactive
	if (!device_is_ready(config->cs_gpio.port)) {
		LOG_ERR("SPI CS device not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&config->cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("Cannot configure SPI CS GPIO");
		return err;
	}

  // init irq routine
	err = paw3395_init_irq(dev);
	if (err) {
		return err;
	}

  // Setup delayable and non-blocking init jobs, including following steps:
  // 1. power reset
  // 2. upload initial settings
  // 3. other configs like cpi, downshift time, sample time etc.
  // The sensor is ready to work (i.e., data->ready=true after the above steps are finished)
  k_work_init_delayable(&data->init_work, paw3395_async_init);

	k_work_schedule(&data->init_work,
			K_MSEC(async_init_delay[data->async_init_step]));

	return err;
}

static int paw3395_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct pixart_data *data = dev->data;
	uint8_t buf[PAW3395_BURST_SIZE];

	if (unlikely(chan != SENSOR_CHAN_ALL)) {
		return -ENOTSUP;
	}

	if (unlikely(!data->ready)) {
		LOG_DBG("Device is not initialized yet");
		return -EBUSY;
	}

	int err = motion_burst_read(dev, buf, sizeof(buf));

	if (!err) {
		int16_t x = ((int16_t)sys_get_le16(&buf[PAW3395_DX_POS])) / CONFIG_PAW3395_CPI_DIVIDOR;
		int16_t y = ((int16_t)sys_get_le16(&buf[PAW3395_DY_POS])) / CONFIG_PAW3395_CPI_DIVIDOR;
		/* int16_t x = sys_get_le16(&buf[PAW3395_DX_POS]); */
 		/* int16_t y = sys_get_le16(&buf[PAW3395_DY_POS]); */

		if (IS_ENABLED(CONFIG_PAW3395_ORIENTATION_0)) {
			data->x = x;
			data->y = y;
		} else if (IS_ENABLED(CONFIG_PAW3395_ORIENTATION_90)) {
			data->x = y;
			data->y = x;
		} else if (IS_ENABLED(CONFIG_PAW3395_ORIENTATION_180)) {
			data->x = x;
			data->y = -y;
		} else if (IS_ENABLED(CONFIG_PAW3395_ORIENTATION_270)) {
			data->x = -y;
			data->y = -x;
		}
	}

	return err;
}

static int paw3395_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	struct pixart_data *data = dev->data;

	if (unlikely(!data->ready)) {
		LOG_DBG("Device is not initialized yet");
		return -EBUSY;
	}

	switch (chan) {
	case SENSOR_CHAN_POS_DX:
		val->val1 = data->x;
		val->val2 = 0;
		break;

	case SENSOR_CHAN_POS_DY:
		val->val1 = data->y;
		val->val2 = 0;
		break;

	default:
		return -ENOTSUP;
	}

	return 0;
}

/* Setup the callback for actual trigger handling */
// handler could be NULL, in which case the effect is disabling the interrupt line
// Thus it has dual function:
// 1. set up a handler callback
// 2. set up a flag (i.e., data_ready_handler) to indicate resuming the interrput line or not
//    This feature is useful to pass the resuming of the interrupt to application
static int paw3395_trigger_set(const struct device *dev,
			       const struct sensor_trigger *trig,
			       sensor_trigger_handler_t handler)
{
  LOG_INF("trigger_set");

	struct pixart_data *data = dev->data;
	const struct pixart_config *config = dev->config;
	int err;

	if (unlikely(trig->type != SENSOR_TRIG_DATA_READY)) {
		return -ENOTSUP;
	}

	if (unlikely(trig->chan != SENSOR_CHAN_ALL)) {
		return -ENOTSUP;
	}

	if (unlikely(!data->ready)) {
		LOG_DBG("Device is not initialized yet");
		return -EBUSY;
	}

  // spin lock is needed, so that the handler is not invoked before its pointer is assigned
  // a valid value
	k_spinlock_key_t key = k_spin_lock(&data->lock);

  // if non-NULL (a real handler defined), eanble the interrupt line
  // otherwise, disable the interrupt line
	if (handler) {
		err = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
						      GPIO_INT_LEVEL_ACTIVE);
	} else {
		err = gpio_pin_interrupt_configure_dt(&config->irq_gpio,
						      GPIO_INT_DISABLE);
	}

	if (!err) {
		data->data_ready_handler = handler;
	}

  data->trigger = trig;

	k_spin_unlock(&data->lock, key);

	return err;
}

static const struct sensor_driver_api paw3395_driver_api = {
	.sample_fetch = paw3395_sample_fetch,
	.channel_get  = paw3395_channel_get,
	.trigger_set  = paw3395_trigger_set,
	.attr_set     = paw3395_attr_set,
};

#define PAW3395_DEFINE(n)						       \
	static struct pixart_data data##n;				       \
									       \
	static const struct pixart_config config##n = {		       \
		.irq_gpio = GPIO_DT_SPEC_INST_GET(n, irq_gpios),	       \
		.bus = {						       \
			.bus = DEVICE_DT_GET(DT_INST_BUS(n)),		       \
			.config = {					       \
				.frequency = DT_INST_PROP(n,		       \
							  spi_max_frequency),  \
				.operation = SPI_WORD_SET(8) |		       \
					     SPI_TRANSFER_MSB |		       \
					     SPI_MODE_CPOL | SPI_MODE_CPHA,    \
				.slave = DT_INST_REG_ADDR(n),		       \
			},						       \
		},							       \
		.cs_gpio = SPI_CS_GPIOS_DT_SPEC_GET(DT_DRV_INST(n)),	       \
	};								       \
									       \
	DEVICE_DT_INST_DEFINE(n, paw3395_init, NULL, &data##n, &config##n,     \
			      POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,	       \
			      &paw3395_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PAW3395_DEFINE)
