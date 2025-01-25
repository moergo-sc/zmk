/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_underglow_battery

// Dependencies
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/battery.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)


struct underglow_battery_config {
    int threshold;
};

static int underglow_battery_init(const struct device *dev) { return 0; };

static int underglow_battery_process(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct underglow_battery_config *config = dev->config;
    int bat = zmk_battery_state_of_charge();

    if (bat >= config->threshold)
        return binding->param2;
    else
        return binding->param1;
}

static const struct behavior_driver_api underglow_battery_driver_api = {
    .binding_pressed = underglow_battery_process,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};


#define KP_INST(n)                                                                                 \
    static struct underglow_battery_config underglow_battery_config_##n = {                  \
        .threshold = DT_INST_PROP(n, threshold)};                                                  \
    BEHAVIOR_DT_INST_DEFINE(n, underglow_battery_init, NULL, NULL,        \
                            &underglow_battery_config_##n, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &underglow_battery_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
