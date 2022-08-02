/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <usb/usb_device.h>
#include <drivers/hwinfo.h>
#include "usb_descriptor.h"


#define LOG_LEVEL CONFIG_USB_DEVICE_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_DECLARE(usb_descriptor);

uint8_t *usb_update_sn_string_descriptor(void)
{
    /*
     * nrf52840 hwinfo returns a 64-bit hardware id. Glove80 uses this as a
     * serial number, encoded as hexadecimal into the templated space after the
     * : character in CONFIG_USB_DEVICE_SN. If no : is present, or if
     * insufficient template space is available after the :, instead return the
     * static serial number string.
     */
    const uint8_t template_len = sizeof(CONFIG_USB_DEVICE_SN);
    const uint8_t sn_len = 16;
    const char hex[] = "0123456789ABCDEF";

    static uint8_t serial[sizeof(CONFIG_USB_DEVICE_SN)];
    strncpy(serial, CONFIG_USB_DEVICE_SN, template_len);

    uint8_t* prefix_end = strrchr(serial, ':');
    if (prefix_end == 0) {
        LOG_DBG("Serial number template missing");
        return CONFIG_USB_DEVICE_SN;
    }

    uint8_t prefix_len = prefix_end - serial + 1;
    if (sn_len > template_len - prefix_len - 1) {
        LOG_DBG("Serial number template too short");
        return CONFIG_USB_DEVICE_SN;
    }

    uint8_t hwid[8];
    memset(hwid, 0, sizeof(hwid));
    uint8_t hwlen = hwinfo_get_device_id(hwid, sizeof(hwid));

    if (hwlen > 0) {
        LOG_HEXDUMP_DBG(&hwid, sn_len, "Serial Number");
        uint8_t* template = prefix_end + 1;
        for (int i = 0; i < hwlen; i++) {
            template[i * 2]     = hex[hwid[i] >> 4];
            template[i * 2 + 1] = hex[hwid[i] & 0xF];
        }
    }

    return serial;
}
