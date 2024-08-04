#ifndef ZEPHYR_INCLUDE_PAW3395_H_
#define ZEPHYR_INCLUDE_PAW3395_H_

#include "../pixart.h"

/**
 * @file paw3395.h
 *
 * @brief Header file for the paw3395 driver.
 */

#include <drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

  enum paw3395_run_mode {
    HP_MODE, // high performance mode (the default mode using standard power-up register sets)
    LP_MODE, // low power mode
    OFFICE_MODE, // office mode (reduced to 10 ips, most power-efficient)
    GAME_MODE, // corded game mode (with best performance and highest power consumption)

    RUN_MODE_COUNT // end flag
  };

enum paw3395_attribute {
	/** Sensor CPI for both X and Y axes. */
	PAW3395_ATTR_X_CPI = SENSOR_ATTR_PRIV_START,

	PAW3395_ATTR_Y_CPI,

	/** Enable or disable sleep modes. */
	PAW3395_ATTR_REST_ENABLE,

	/** Entering time from Run mode to REST1 mode [ms]. */
	PAW3395_ATTR_RUN_DOWNSHIFT_TIME,

	/** Entering time from REST1 mode to REST2 mode [ms]. */
	PAW3395_ATTR_REST1_DOWNSHIFT_TIME,

	/** Entering time from REST2 mode to REST3 mode [ms]. */
	PAW3395_ATTR_REST2_DOWNSHIFT_TIME,

	/** Sampling frequency time during REST1 mode [ms]. */
	PAW3395_ATTR_REST1_SAMPLE_TIME,

	/** Sampling frequency time during REST2 mode [ms]. */
	PAW3395_ATTR_REST2_SAMPLE_TIME,

	/** Sampling frequency time during REST3 mode [ms]. */
	PAW3395_ATTR_REST3_SAMPLE_TIME,

  /** Select run mde. */
  PAW3395_ATTR_RUN_MODE,
};
#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_PAW3395_H_ */
