#ifndef ZEPHYR_INCLUDE_PIXART_H_
#define ZEPHYR_INCLUDE_PIXART_H_

/**
 * @file pixart.h
 *
 * @brief Common header file for all optical motion sensor by PIXART
 */

#include <device.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/* device data structure */
  struct pixart_data {
    const struct device          *dev;
    int16_t                      x;
    int16_t                      y;

    // lock is needed to keep atomic of the trigger handler upadting
    struct k_spinlock            lock;
    // motion interrupt isr
    struct gpio_callback         irq_gpio_cb;
    // the actual trigger handler. This handler also used to flag whether resuming the motion interrupt line
    sensor_trigger_handler_t     data_ready_handler; 
    const struct sensor_trigger *trigger;
    // the work structure holding the trigger handler job
    struct k_work                trigger_handler_work;

    // the work structure for delayable init steps
    struct k_work_delayable      init_work;
    int                          async_init_step;

    //
    bool                         ready; // whether init is finished successfully
    bool                         last_read_burst; // todo: needed?
    int                          err; // error code during async init

    /* the design of the driver is based on interrupt purely, to add polling upon it
       the following work and timer maybe used in application code */
    struct k_work                poll_work;
    struct k_timer               poll_timer;

    // for pmw3610 smart algorithm
    bool                         sw_smart_flag;
  };

  // device config data structure
  struct pixart_config {
    struct gpio_dt_spec irq_gpio;
    struct spi_dt_spec bus;
    struct gpio_dt_spec cs_gpio;
  };

/** @brief Sensor specific attributes of PIXART. */
enum pixart_attribute {
	/** Sensor CPI for both X and Y axes. */
	PIXART_ATTR_CPI = SENSOR_ATTR_PRIV_START,

  /** Sensor CPI for X and Y axes. */
  PIXART_ATTR_XCPI,
  PIXART_ATTR_YCPI,

	/** Enable or disable sleep modes. */
	PIXART_ATTR_REST_ENABLE,

	/** Entering time from Run mode to REST1 mode [ms]. */
	PIXART_ATTR_RUN_DOWNSHIFT_TIME,

	/** Entering time from REST1 mode to REST2 mode [ms]. */
	PIXART_ATTR_REST1_DOWNSHIFT_TIME,

	/** Entering time from REST2 mode to REST3 mode [ms]. */
	PIXART_ATTR_REST2_DOWNSHIFT_TIME,

	/** Sampling frequency time during REST1 mode [ms]. */
	PIXART_ATTR_REST1_SAMPLE_TIME,

	/** Sampling frequency time during REST2 mode [ms]. */
	PIXART_ATTR_REST2_SAMPLE_TIME,

	/** Sampling frequency time during REST3 mode [ms]. */
	PIXART_ATTR_REST3_SAMPLE_TIME,

  /** Select the running mode. */
  PIXART_ATTR_RUN_MODE,
};


#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_PIXART_H_ */
