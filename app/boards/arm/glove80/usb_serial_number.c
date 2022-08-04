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

int base32_encode(const uint8_t *data, int length, uint8_t *result,
                  int bufSize);


uint8_t *usb_update_sn_string_descriptor(void)
{
    /*
     * nrf52840 hwinfo returns a 64-bit hardware id. Glove80 uses this as a
     * serial number, encoded as base32 into the templated space after the :
     * character in CONFIG_USB_DEVICE_SN. If no : is present, or if insufficient
     * template space is available after the :, instead return the static serial
     * number string.
     */
    const uint8_t template_len = sizeof(CONFIG_USB_DEVICE_SN);
    const uint8_t sn_len = 13;

    static uint8_t serial[sizeof(CONFIG_USB_DEVICE_SN)];
    strncpy(serial, CONFIG_USB_DEVICE_SN, template_len);

    uint8_t* prefix_end = strrchr(serial, ':');
    if (prefix_end == 0) {
        LOG_DBG("Serial number template missing");
        return CONFIG_USB_DEVICE_SN;
    }

    uint8_t prefix_len = prefix_end - serial + 1;
    if (sn_len + 1 > template_len - prefix_len) {
        LOG_DBG("Serial number template too short");
        return CONFIG_USB_DEVICE_SN;
    }

    uint8_t hwid[8];
    memset(hwid, 0, sizeof(hwid));
    uint8_t hwlen = hwinfo_get_device_id(hwid, sizeof(hwid));

    if (hwlen > 0) {
        LOG_HEXDUMP_DBG(&hwid, sn_len, "Serial Number");
        base32_encode(hwid, hwlen, prefix_end + 1, sn_len + 1);
    }

    return serial;
}

// Base32 implementation
//
// Copyright 2010 Google Inc.
// Author: Markus Gutschke
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

int base32_encode(const uint8_t *data, int length, uint8_t *result,
                  int bufSize) {
  if (length < 0 || length > (1 << 28)) {
    return -1;
  }
  int count = 0;
  if (length > 0) {
    int buffer = data[0];
    int next = 1;
    int bitsLeft = 8;
    while (count < bufSize && (bitsLeft > 0 || next < length)) {
      if (bitsLeft < 5) {
        if (next < length) {
          buffer <<= 8;
          buffer |= data[next++] & 0xFF;
          bitsLeft += 8;
        } else {
          int pad = 5 - bitsLeft;
          buffer <<= pad;
          bitsLeft += pad;
        }
      }
      int index = 0x1F & (buffer >> (bitsLeft - 5));
      bitsLeft -= 5;
      result[count++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"[index];
    }
  }
  if (count < bufSize) {
    result[count] = '\000';
  }
  return count;
}
