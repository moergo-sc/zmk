#define DT_DRV_COMPAT pixart_pmw33xx

#include <drivers/sensor.h>
#include <logging/log.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/mouse.h>
#include <dt-bindings/zmk/mouse.h>

#include <zmk/event_manager.h>
#include <zmk/events/endpoint_selection_changed.h>

#include <pmw33xx/pmw33xx.h>

#define SCROLL_DIV_FACTOR 5
/* #define SCROLL_LAYER_INDEX 4 */
#define SCROLL_LAYER_INDEX COND_CODE_0(DT_INST_NODE_HAS_PROP(0, scroll_layer), (255), \
                                       (DT_INST_PROP(0, scroll_layer)))

#define CPI_DIVIDOR COND_CODE_0(DT_INST_NODE_HAS_PROP(0, cpi_dividor), (1), \
                                       (DT_INST_PROP(0, cpi_dividor)))


/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
// in us
static int64_t last_interrupt_time = 0;
static int64_t current_interrupt_time = 0;
static int64_t interrupt_interval = 0;
static int64_t handler_duration = 0;
static int64_t send_report_duration = 0;
static int64_t idle_interval = 0;
static int64_t time_buffer = 0;
/* #endif */

static int  polling_count = 0;
static int  max_poll_count = 0;
static int  polling_interval = 0;
#define BLE_POLL_COUNT 20
#define BLE_POLL_INTERVAL 15 // in ms
#define USB_POLL_COUNT 300
#define USB_POLL_INTERVAL 1 // in ms

//
static struct sensor_value dx, dy;

const struct device *trackball = DEVICE_DT_GET(DT_DRV_INST(0));

LOG_MODULE_REGISTER(trackball, CONFIG_SENSOR_LOG_LEVEL);


/* update and send report */
static int64_t trackball_update_handler(struct k_work *work) {
/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
  int64_t start_time = k_ticks_to_us_floor64(k_uptime_ticks());
/* #endif */

  // remaining scroll from last update
  static int8_t scroll_ver_rem = 0, scroll_hor_rem = 0;
  dx.val1 = dx.val1/CPI_DIVIDOR;
  dy.val1 = dy.val1/CPI_DIVIDOR;

  // update report with latest position
  zmk_hid_mouse_movement_set(0, 0);
  zmk_hid_mouse_scroll_set(0, 0);

  const uint8_t layer = zmk_keymap_highest_layer_active();
  if (layer == SCROLL_LAYER_INDEX) {   // lower
    const int16_t total_hor = dx.val1 + scroll_hor_rem, total_ver = -(dy.val1 + scroll_ver_rem);
    scroll_hor_rem = total_hor % SCROLL_DIV_FACTOR;
    scroll_ver_rem = total_ver % SCROLL_DIV_FACTOR;
    zmk_hid_mouse_scroll_update(total_hor / SCROLL_DIV_FACTOR, total_ver / SCROLL_DIV_FACTOR);
  } else {
    zmk_hid_mouse_movement_update(CLAMP(dx.val1, INT8_MIN, INT8_MAX), CLAMP(dy.val1, INT8_MIN, INT8_MAX));
  }

  // send the report to host
  zmk_endpoints_send_mouse_report();

/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
  return start_time = k_ticks_to_us_floor64(k_uptime_ticks()) - start_time;
/* #else */
/*   return 0; */
/* #endif */
}

// polling work
static void trackball_poll_handler(struct k_work *work) {
/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
  current_interrupt_time = k_ticks_to_us_floor64(k_uptime_ticks());
  interrupt_interval = current_interrupt_time - last_interrupt_time;
  idle_interval = current_interrupt_time - time_buffer;
/* #endif */


    // fetch latest position from sensor
    int ret = sensor_sample_fetch(trackball);
    if (ret < 0) {
        LOG_ERR("fetch: %d", ret);
        return;
    }

    // get the x, y delta
    ret = sensor_channel_get(trackball, SENSOR_CHAN_POS_DX, &dx);
    if (ret < 0) {
        LOG_ERR("get dx: %d", ret);
        return;
    }
    ret = sensor_channel_get(trackball, SENSOR_CHAN_POS_DY, &dy);
    if (ret < 0) {
        LOG_ERR("get dy: %d", ret);
        return;
    }

    if(dx.val1 != 0 || dy.val1 != 0) {
    // process the updated position and send to host
    /* k_work_submit_to_queue(zmk_mouse_work_q(), &trackball_update); */
/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
      send_report_duration = trackball_update_handler(NULL);
/* #else */
/*     trackball_update_handler(NULL); */
/* #endif */
      LOG_INF("Position updated: poll interval: %lld; send time: %lld ; new pos: %d %d",\
              interrupt_interval, send_report_duration, dx.val1, dy.val1);
    }
    
/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
    time_buffer = k_ticks_to_us_floor64(k_uptime_ticks());
    handler_duration = time_buffer - current_interrupt_time;
    LOG_DBG("idle time: %lld ; handler time cost: %lld", \
            idle_interval, handler_duration);

    last_interrupt_time = current_interrupt_time;
}

K_WORK_DEFINE(trackball_poll_work, &trackball_poll_handler);

// polling timer
void trackball_timer_expiry(struct k_timer *timer);
void trackball_timer_stop(struct k_timer *timer);

K_TIMER_DEFINE(trackball_timer, trackball_timer_expiry, trackball_timer_stop);

// timer expiry function
void trackball_timer_expiry(struct k_timer *timer) {
  // check whether reaching the polling count limit    
    if(polling_count < max_poll_count) {
      // submit polling work to mouse work queue
      k_work_submit_to_queue(zmk_mouse_work_q(), &trackball_poll_work);

      // update status
      polling_count++;
    }
    else {
      // stop timer
      k_timer_stop(&trackball_timer);
    }
}

// timer stop function
void trackball_timer_stop(struct k_timer *timer) {
  // reset status
  polling_count = 0;

  // resume motion interrupt line
  const struct pmw33xx_config *cfg = trackball->config;
  if (gpio_pin_interrupt_configure(cfg->motswk_spec.port, cfg->motswk_spec.pin, GPIO_INT_LEVEL_ACTIVE)) {
    LOG_WRN("Unable to set MOTSWK GPIO interrupt");
  }
}


// trigger handler
static void trackball_trigger_handler(const struct device *dev, const struct sensor_trigger *trig) {
  struct pmw33xx_data *data = dev->data;
  LOG_INF("I'm OLD trackball implementation");

  // do not resume motion interrupt
  data->resume_interrupt = false;

  // start the polling timer
  k_timer_start(&trackball_timer, K_NO_WAIT, K_MSEC(polling_interval));
}

static int trackball_init() {

    struct sensor_trigger trigger = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ALL,
    };
    printk("trackball");
    if (sensor_trigger_set(trackball, &trigger, trackball_trigger_handler) < 0) {
        LOG_ERR("can't set trigger");
        return -EIO;
    };

    // init polling parameters
    switch (zmk_endpoints_selected()) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_ENDPOINT_USB: {
      max_poll_count = USB_POLL_COUNT;
      polling_interval = USB_POLL_INTERVAL;
      return 0;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_ENDPOINT_BLE: {
      max_poll_count = BLE_POLL_COUNT;
      polling_interval = BLE_POLL_INTERVAL;
      return 0;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    default:
        LOG_ERR("Unsupported endpoint");
        return -ENOTSUP;
    }
}

SYS_INIT(trackball_init, APPLICATION, CONFIG_SENSOR_INIT_PRIORITY);

//////////////
int trackball_endpoint_listener(const zmk_event_t *eh) {
  LOG_INF("endpoint changing...");

    // update polling parameters
    switch (zmk_endpoints_selected()) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_ENDPOINT_USB: {
      max_poll_count = USB_POLL_COUNT;
      polling_interval = USB_POLL_INTERVAL;
      break;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_ENDPOINT_BLE: {
      max_poll_count = BLE_POLL_COUNT;
      polling_interval = BLE_POLL_INTERVAL;
      break;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    default:
        LOG_ERR("Unsupported endpoint");
    }

  return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(trackball, trackball_endpoint_listener)
ZMK_SUBSCRIPTION(trackball, zmk_endpoint_selection_changed)
