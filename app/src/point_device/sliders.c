/* Description:
*  1. setup sliders callback
*  2. The callback will set up the correct information and raise a slider event
*  3. The processing of slider event is dispatched to listening processors, notably keymap
*/

#include <devicetree.h>
#include <init.h>

#include <logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/point_device.h>
#include <zmk/sliders.h>
#include <drivers/slider.h>
#include <zmk/event_manager.h>
#include <zmk/events/pd_raw_event.h>

#include <zmk/hid.h>
#include <zmk/endpoints.h>

#if ZMK_KEYMAP_HAS_SLIDERS

typedef struct {
  uint8_t id;
  int16_t dPos;
  int     dT;
}__attribute__((aligned(4))) zmk_slider_msg;

K_MSGQ_DEFINE(zmk_slider_msgq, sizeof(zmk_slider_msg), CONFIG_ZMK_KSCAN_EVENT_QUEUE_SIZE, 4);

/* the slider msg processor */
void zmk_slider_process_msgq(struct k_work *item) {
  zmk_slider_msg msg;

  while (k_msgq_get(&zmk_slider_msgq, &msg, K_NO_WAIT) == 0) {
    LOG_INF("Event from slider_%d: dPos: %d, dT: %d ms", msg.id, msg.dPos, msg.dT);
    ZMK_EVENT_RAISE(new_zmk_pd_raw_event((struct zmk_pd_raw_event){
          .type = SLIDER,
          .id = msg.id, .dx = msg.dPos, .dy = msg.dPos,
          .dt = msg.dT, .update_time = k_uptime_get()}));
  }
}

K_WORK_DEFINE(slider_msgq_work, zmk_slider_process_msgq);

/* the slider callback */
void zmk_slider_callback(const struct device *dev, int16_t dPos, int dT)
{
	struct slider_data *slider_data = dev->data;
  zmk_slider_msg msg = {
    .id = slider_data->id,
    .dPos = dPos,
    .dT = dT
  };

  k_msgq_put(&zmk_slider_msgq, &msg, K_NO_WAIT);
  k_work_submit_to_queue(zmk_pd_work_q(), &slider_msgq_work);
}

/* config the sliders: setup callback and assign the slider id */
static void zmk_sliders_init_item(const char *node, uint8_t abs_i) {
    LOG_DBG("Init %s with slider_id %d", node, abs_i);

    const struct device *dev = device_get_binding(node);
    if (!dev) {
        LOG_WRN("Failed to find device for %s", node);
        return;
    }

    kscan_slider_config(dev, zmk_slider_callback, abs_i);
}

#define SLIDER_INIT(idx, _i)                                                                       \
    COND_CODE_1(DT_NODE_HAS_STATUS(ZMK_KEYMAP_SLIDERS_BY_IDX(idx), okay),                          \
      (zmk_sliders_init_item(DT_LABEL(ZMK_KEYMAP_SLIDERS_BY_IDX(idx)), idx);), (;))

static int zmk_sliders_init(const struct device *_arg) {
    UTIL_LISTIFY(ZMK_KEYMAP_SLIDERS_LEN, SLIDER_INIT, 0)
    return 0;
}

SYS_INIT(zmk_sliders_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

