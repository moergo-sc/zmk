#include <drivers/sensor.h>
#include <devicetree.h>
#include <init.h>

#include <logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/point_device.h>
#include <zmk/trackballs.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/pd_raw_event.h>
#include <zmk/events/endpoint_selection_changed.h>

#if ZMK_KEYMAP_HAS_TRACKBALLS

static int  max_poll_count = 0;
static int  polling_interval = 0;

struct trackballs_data_item {
  uint8_t id;
  const struct device *dev;
  struct sensor_trigger trigger;
  struct k_work  poll_work;
  struct k_timer  poll_timer;
  int  polling_count;

  struct sensor_value dx;
  struct sensor_value dy;
};

#define _TRACKBALL_ITEM(node)                                                                         \
  {.dev = NULL, .polling_count=0, .trigger = {.type = SENSOR_TRIG_DATA_READY, .chan = SENSOR_CHAN_ALL}},
#define TRACKBALL_ITEM(idx, _)                                                                        \
    COND_CODE_1(DT_NODE_HAS_STATUS(ZMK_KEYMAP_TRACKBALLS_BY_IDX(idx), okay),                          \
                (_TRACKBALL_ITEM(ZMK_KEYMAP_TRACKBALLS_BY_IDX(idx))), ())

static struct trackballs_data_item trackballs[] = {UTIL_LISTIFY(ZMK_KEYMAP_TRACKBALLS_LEN, TRACKBALL_ITEM, 0)};

// msg structure
typedef struct {
  uint8_t id;
  int16_t dx;
  int16_t dy;
  int     dt;
}__attribute__((aligned(4))) zmk_trackballs_msg;

K_MSGQ_DEFINE(zmk_trackballs_msgq, sizeof(zmk_trackballs_msg), CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE, 4);

/* the msg processor */
void zmk_trackballs_process_msgq(struct k_work *work) {
  zmk_trackballs_msg msg;
  while (k_msgq_get(&zmk_trackballs_msgq, &msg, K_NO_WAIT) == 0) {
    LOG_INF("Process event from trackball_%d: dx: %d, dy: %d, dt: %d ms", msg.id, msg.dx, msg.dy, msg.dt);
    ZMK_EVENT_RAISE(new_zmk_pd_raw_event((struct zmk_pd_raw_event){
          .type = TRACKBALL,
          .id = msg.id, .dx = msg.dx, .dy = msg.dy,
          .dt = msg.dt, .update_time = k_uptime_get()}));
  }
}

K_WORK_DEFINE(zmk_trackballs_msgq_work, zmk_trackballs_process_msgq);

// polling work
static void zmk_trackballs_poll_handler(struct k_work *work) {
	struct trackballs_data_item *item = CONTAINER_OF(work, struct trackballs_data_item, poll_work);
  const struct device *dev = item->dev;

  // fetch dx and dy from sensor and save them into pixart_data structure
  int ret = sensor_sample_fetch(dev);
  if (ret < 0) {
    LOG_ERR("fetch: %d", ret);
    return;
  }

  // get the x, y delta
  ret = sensor_channel_get(dev, SENSOR_CHAN_POS_DX, &item->dx);
  if (unlikely(ret < 0)) {
      LOG_ERR("get dx: %d", ret);
      return;
  }
  ret = sensor_channel_get(dev, SENSOR_CHAN_POS_DY, &item->dy);
  if (unlikely(ret < 0)) {
      LOG_ERR("get dy: %d", ret);
      return;
  }

  // put the msg
  zmk_trackballs_msg msg = {
    .id = item->id,
    .dx = item->dx.val1,
    .dy = item->dy.val1,
    .dt = polling_interval
  };

  if(msg.dx != 0 || msg.dy != 0) {
    LOG_INF("New position received: dx = %d, dy = %d", msg.dx, msg.dy);
    k_msgq_put(&zmk_trackballs_msgq, &msg, K_NO_WAIT);
    k_work_submit_to_queue(zmk_pd_work_q(), &zmk_trackballs_msgq_work);
  }
}

// trigger handler
static void zmk_trackballs_trigger_handler(const struct device *dev, const struct sensor_trigger *trig) {
  LOG_INF("New interrupt received");

	struct trackballs_data_item *item = CONTAINER_OF(trig, struct trackballs_data_item, trigger);

  // do not resume motion interrupt by passing-in null handler 
  if (sensor_trigger_set(item->dev, trig, NULL) < 0) {
    LOG_ERR("can't stop motion interrupt line");
  };

  // start the polling timer (the real work now is dispatched to a timer-based polling)
  LOG_INF("Polling start ...");
  k_timer_start(&item->poll_timer, K_NO_WAIT, K_MSEC(polling_interval));
}

// timer expiry function
void zmk_trackballs_timer_expiry(struct k_timer *timer) {
	struct trackballs_data_item *item = CONTAINER_OF(timer, struct trackballs_data_item, poll_timer);

  // check whether reaching the polling count limit
  if(item->polling_count < max_poll_count) {
    LOG_INF("Poll %d", item->polling_count);
    // submit polling work to mouse work queue
    k_work_submit_to_queue(zmk_pd_work_q(), &item->poll_work);

    // update status
    item->polling_count++;
  }
  else {
    LOG_INF("Polling end.");
    // stop timer
    k_timer_stop(&item->poll_timer);
  }
}

// timer stop function
void zmk_trackballs_timer_stop(struct k_timer *timer) {
	struct trackballs_data_item *item = CONTAINER_OF(timer, struct trackballs_data_item, poll_timer);

  // reset polling count
  item->polling_count = 0;

  // resume motion interrupt line by setting the handler to a real object
  if (sensor_trigger_set(item->dev, &item->trigger, zmk_trackballs_trigger_handler) < 0) {
    LOG_ERR("can't resume motion interrupt line");
  };
}

int zmk_trackballs_endpoint_listener(const zmk_event_t *eh) {
  LOG_INF("Update polling parameters based on current endpoint");

  // update polling parameters
  switch (zmk_endpoints_selected()) {
#if IS_ENABLED(CONFIG_ZMK_USB)
  case ZMK_ENDPOINT_USB: {
    max_poll_count = CONFIG_ZMK_TRACKBALL_POLL_DURATION / CONFIG_ZMK_TRACKBALL_USB_POLL_INTERVAL;
    polling_interval = CONFIG_ZMK_TRACKBALL_USB_POLL_INTERVAL;
    break;
  }
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
  case ZMK_ENDPOINT_BLE: {
    max_poll_count = CONFIG_ZMK_TRACKBALL_POLL_DURATION / CONFIG_ZMK_TRACKBALL_BLE_POLL_INTERVAL;
    polling_interval = CONFIG_ZMK_TRACKBALL_BLE_POLL_INTERVAL;
    break;
  }
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
  default:
    LOG_ERR("Unsupported endpoint");
  }

  return ZMK_EV_EVENT_BUBBLE;
}

static void zmk_trackballs_init_item(const char *node, uint8_t i, uint8_t abs_i) {
    LOG_INF("Init %s at index %d with trackball id %d", node, i, abs_i);

    // init item structure
    trackballs[i].dev = device_get_binding(node);
    trackballs[i].id = abs_i;

    if (!trackballs[i].dev) {
        LOG_WRN("Failed to find device for %s", node);
        return;
    }

    // setup the timer and handler function of the polling work
    k_timer_init(&trackballs[i].poll_timer, zmk_trackballs_timer_expiry, zmk_trackballs_timer_stop);
    k_work_init(&trackballs[i].poll_work, zmk_trackballs_poll_handler);

    // init trigger handler
    int err;
    int count = 0;
    do {
      err = sensor_trigger_set(trackballs[i].dev, &trackballs[i].trigger, zmk_trackballs_trigger_handler);
      if (err == -EBUSY) {
        count++;
        k_sleep(K_MSEC(20));
      }
    } while (err == -EBUSY && count < 100);

    if (err) {
      LOG_ERR("Cannot enable trigger in app/src/point_device/trackball.c %d  ", err);
      return;
    }

    // init the polling parameters
    zmk_trackballs_endpoint_listener(NULL);
}

#define _TRACKBALL_INIT(node) zmk_trackballs_init_item(DT_LABEL(node), local_index++, absolute_index++);
#define TRACKBALL_INIT(idx, _i)                                                                       \
    COND_CODE_1(DT_NODE_HAS_STATUS(ZMK_KEYMAP_TRACKBALLS_BY_IDX(idx), okay),                          \
                (_TRACKBALL_INIT(ZMK_KEYMAP_TRACKBALLS_BY_IDX(idx))), (absolute_index++;))

static int zmk_trackballs_init(const struct device *_arg) {
  LOG_INF("Init trackballs ...");
  int local_index = 0;
  int absolute_index = 0;

  UTIL_LISTIFY(ZMK_KEYMAP_TRACKBALLS_LEN, TRACKBALL_INIT, 0)
    return 0;
}

SYS_INIT(zmk_trackballs_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

ZMK_LISTENER(zmk_trackballs, zmk_trackballs_endpoint_listener)
ZMK_SUBSCRIPTION(zmk_trackballs, zmk_endpoint_selection_changed)

#endif

