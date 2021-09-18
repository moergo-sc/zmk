# Building Zephyr™ Mechanical Keyboard (ZMK) Firmware with Nix

This extension is added by MoErgo for the Glove80 keyboard.

Nix makes setup significantly easier. With this approach `west` is not needed.
You can however still choose to build using the standard Zephyr `west` toolchain
if you wish.

# To build a target

In ZMK root directory,

    nix-build -A *target* [-o *output_directory*]

For example,

    nix-build -A glove80_left -o left

The `output_directory` nix creates is a symlink. If you prefer not to rely on
symlink (perhaps because you are using WSL on Windows), you can make a copy of
the resulting `uf2` file using:

    cp -f $(nix-build -A *target* --no-out-link)/zmk.uf2 .

# To build Glove80

In ZMK root directory,

    cp -f $(nix-build -A glove80_combined --no-out-link)/glove80.uf2 .

# Adding new targets

Edit default.nix and add an target based on zmk

An example is:

    glove80_left = zmk.override {
      board = "glove80_lh";
    };
