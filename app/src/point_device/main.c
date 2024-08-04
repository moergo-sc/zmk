/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <kernel.h>
#include <zmk/point_device.h>

#if IS_ENABLED(CONFIG_ZMK_PD_WORK_QUEUE_DEDICATED)
static const struct k_work_queue_config pd_work_q_config = {.name = "Processor for events from all point devices",
                                                            .no_yield = false    };
static struct k_work_q pd_work_q;
K_THREAD_STACK_DEFINE(pd_work_stack_area, CONFIG_ZMK_PD_DEDICATED_WORK_QUEUE_STACK_SIZE);
#endif

struct k_work_q *zmk_pd_work_q() {
#if IS_ENABLED(CONFIG_ZMK_PD_WORK_QUEUE_DEDICATED)
    return &pd_work_q;
#else
    return &k_sys_work_q;
#endif
}

int zmk_pd_init() {
#if IS_ENABLED(CONFIG_ZMK_PD_WORK_QUEUE_DEDICATED)
  k_work_queue_init(&pd_work_q);
  k_work_queue_start(&pd_work_q, pd_work_stack_area,
                     K_THREAD_STACK_SIZEOF(pd_work_stack_area),
                     CONFIG_ZMK_PD_DEDICATED_WORK_QUEUE_PRIORITY,
                     &pd_work_q_config);
#endif
    return 0;
}
