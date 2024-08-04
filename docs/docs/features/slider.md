---
title: Slider
sidebar_label: Slider
---

All devices which can generate 1-d continuous incremental position changes in one manueover
are classified as a slider.
Examples are capacitive touchbar, potentialmeter slider etc..
Rotary encoder is not considered as slider, since the manueover generates an on/off state change
by nature.

The implementation of the slider feature uses a capacitive slider as the reference design.
The slider is composed of multiple discrete capacitive sensors in a row.
These sensors can be used as buttons seperately or as a single slider cooperatively.
The implementation support three modes:
- button : only button usage is activated
- slider : only slider usage is activeated
- mix : both button and slider usages are activated


The reference hardware is based on [CAP1203](https://www.microchip.com/en-us/product/CAP1203), which supports up to three input sensors.
The driver of CAP1203 is basically a kscan device, with support of slide moanuover.
The device node is a I2C slave defined as:
```
  &i2c0 {
	  compatible = "nordic,nrf-twi";
	  sda-pin = <X>;
  	  scl-pin = <X>;
      status = "okay";

	  kscan1: kscan1@28 {
		  compatible = "zmk,kscan-cap1203";
          status = "okay";
          reg = <0x28>;
		  int-gpios = <&gpio1  11 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
	  };
  };
```

The different working mode is selected using Kconfig options:
- CONFIG_CAP1203_BUTTON_MODE
- CONFIG_CAP1203_SLIDER_MODE
- CONFIG_CAP1203_MIX_MODE

## Button Mode
The driver node works as a normal kscan driver like others.
The keypress related behaviors are all supported naturally.
It can be set as the main chosen node by itself:
```
chosen {
    zmk,kscan = &kscan1;
    zmk,matrix_transform = &default_transform;
};
```

Or more likely, it can be a component of a composite kscan node:
```
```
To enable the driver code, the following configuration should also be added in config file:
```
chosen {
    zmk,kscan = &kscan;
    zmk,matrix_transform = &default_transform;
};

kscan: kscan {
		compatible = "zmk,kscan-composite";
		label = "KSCAN";
		rows = <2>;
		columns = <7>;

		normal_keys {
			kscan = <&kscan0>;
		};

		touch_keys: touch_keys {
			kscan = <&kscan1>;
            row-offset = <1>;
		};
	};

kscan0: kscan0 {
		compatible = "zmk,kscan-gpio-direct";
		label = "KEYBOARD-MATRIX";
		input-gpios
		= <&gpio0  6 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio0  8 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio0 24 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio0  9 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio0 10 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio1  4 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		, <&gpio1  6 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>
		;
	};
```

## Slider Mode

## Mix Mode


## Programming Interface
A dedicated data structure `slider_data` needs to be inserted at the beginning of the 
device driver's data structure.
A kscan wrapper of the kscan api is used to hold a `kscan_slider_configure` function.
`kscan_slider_configure` is used to setup the slider callback and the slider id in the system.
