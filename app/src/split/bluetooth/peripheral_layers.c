
#include <zephyr/types.h>
#include <zephyr/sys/util.h>

#include <zmk/split/bluetooth/peripheral_layers.h>

static uint32_t peripheral_layers = 0;

void set_peripheral_layers_state(uint32_t new_layers) { peripheral_layers = new_layers; }

bool peripheral_layer_active(uint8_t layer) {
    return (peripheral_layers & (BIT(layer))) == (BIT(layer));
};