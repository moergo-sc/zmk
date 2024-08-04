#define DT_DRV_COMPAT cirque_pinnacle

#include <drivers/sensor.h>
#include <logging/log.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/mouse.h>

#define SCROLL_DIV_FACTOR 5
#define SCROLL_LAYER_INDEX COND_CODE_0(DT_INST_NODE_HAS_PROP(0, scroll_layer), (255), \
                                       (DT_INST_PROP(0, scroll_layer)))


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

static struct sensor_value dx, dy, btn;

const struct device *trackpad = DEVICE_DT_GET(DT_DRV_INST(0));

LOG_MODULE_REGISTER(trackpad, CONFIG_SENSOR_LOG_LEVEL);

/* update and send report */
static int64_t trackpad_update_handler(struct k_work *work) {
/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
  int64_t start_time = k_ticks_to_us_floor64(k_uptime_ticks());
/* #endif */

  // remaining scroll from last update
  static int8_t scroll_ver_rem = 0, scroll_hor_rem = 0;
  uint8_t button;
  static uint8_t last_button = 0;
  static uint8_t last_pressed = 0;

  // update report with latest position
  zmk_hid_mouse_movement_set(0, 0);
  zmk_hid_mouse_scroll_set(0, 0);

  const uint8_t layer = zmk_keymap_highest_layer_active();
  if (layer == SCROLL_LAYER_INDEX) {   // lower
    const int16_t total_hor = dx.val1 + scroll_hor_rem, total_ver = -(dy.val1 + scroll_ver_rem);
    scroll_hor_rem = total_hor % SCROLL_DIV_FACTOR;
    scroll_ver_rem = total_ver % SCROLL_DIV_FACTOR;
    zmk_hid_mouse_scroll_update(total_hor / SCROLL_DIV_FACTOR, total_ver / SCROLL_DIV_FACTOR);
    button = RCLK;
  } else {
    zmk_hid_mouse_movement_update(CLAMP(dx.val1, INT8_MIN, INT8_MAX), CLAMP(dy.val1, INT8_MIN, INT8_MAX));
    button = LCLK;
  }

  if (!last_pressed && btn.val1) {
    zmk_hid_mouse_buttons_press(button);
    last_button = button;
  } else if (last_pressed && !btn.val1) {
    zmk_hid_mouse_buttons_release(last_button);
  }
  // send the report to host
  zmk_endpoints_send_mouse_report();
  last_pressed = btn.val1;

/* #if IS_ENABLED(CONFIG_SENSOR_LOG_LEVEL_DBG) */
  return start_time = k_ticks_to_us_floor64(k_uptime_ticks()) - start_time;
/* #else */
/*   return 0; */
/* #endif */
}

static void handle_trackpad(const struct device *dev, const struct sensor_trigger *trig) {
  current_interrupt_time = k_ticks_to_us_floor64(k_uptime_ticks());
  interrupt_interval = current_interrupt_time - last_interrupt_time;
  idle_interval = current_interrupt_time - time_buffer;


    int ret = sensor_sample_fetch(dev);
    if (ret < 0) {
        LOG_ERR("fetch: %d", ret);
        return;
    }

    ret = sensor_channel_get(dev, SENSOR_CHAN_POS_DX, &dx);
    if (ret < 0) {
        LOG_ERR("get dx: %d", ret);
        return;
    }
    ret = sensor_channel_get(dev, SENSOR_CHAN_POS_DY, &dy);
    if (ret < 0) {
        LOG_ERR("get dy: %d", ret);
        return;
    }
    ret = sensor_channel_get(dev, SENSOR_CHAN_PRESS, &btn);
    if (ret < 0) {
        LOG_ERR("get btn: %d", ret);
        return;
    }

    send_report_duration = trackpad_update_handler(NULL);

    time_buffer = k_ticks_to_us_floor64(k_uptime_ticks());
    handler_duration = time_buffer - current_interrupt_time;

    LOG_INF("interrupt interval (us): %lld ; idle: %lld ; handler time cost: %lld",\
            interrupt_interval, idle_interval, handler_duration);
    LOG_INF("Send report time cost: %lld; Position: %d %d", send_report_duration, dx.val1, dy.val1);
    last_interrupt_time = current_interrupt_time;
}

static int trackpad_init() {
    struct sensor_trigger trigger = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ALL,
    };
    printk("trackpad");
    if (sensor_trigger_set(trackpad, &trigger, handle_trackpad) < 0) {
        LOG_ERR("can't set trigger");
        return -EIO;
    };
    return 0;
}

SYS_INIT(trackpad_init, APPLICATION, CONFIG_ZMK_KSCAN_INIT_PRIORITY);
