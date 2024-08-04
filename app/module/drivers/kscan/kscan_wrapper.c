
#include <drivers/kscan_wrapper.h>

void kscan_slider_config(const struct device *dev,
                         kscan_slider_callback_t callback,
                         int id)
{
  const struct kscan_driver_api *api =
    (struct kscan_driver_api *)dev->api;

  struct kscan_slider_api *slider_api = CONTAINER_OF(api, struct kscan_slider_api, kscan_api);
  __ASSERT(slider_api ? true : false, "The kscan_slider_api is not used.");

  slider_api->slider_config(dev, callback, id);
  return;
};
