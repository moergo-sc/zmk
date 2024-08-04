#pragma once

enum pd_type {
  SLIDER,
  TRACKBALL,
  JOYSTICK
};

struct k_work_q *zmk_pd_work_q();
int zmk_pd_init();
