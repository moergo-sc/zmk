{ pkgs ? import <nixpkgs> {} }:

with pkgs;
let
  zmkPkgs = (import ./default.nix { inherit pkgs; });
  inherit (zmkPkgs) zmk zephyr;

  baseImage = dockerTools.buildImage {
    name = "base-image";
    tag  = "latest";

    runAsRoot = ''
      #!${busybox}/bin/sh
      set -e

      ${dockerTools.shadowSetup}
      groupadd -r deploy
      useradd -r -g deploy -d /data -M deploy
      mkdir /data
      chown -R deploy:deploy /data

      mkdir -m 1777 /tmp
    '';
  };

  referToPackages = name: ps: writeTextDir name (builtins.toJSON ps);

  depsImage = dockerTools.buildImage {
    name = "deps-image";
    fromImage = baseImage;
    # FIXME: can zephyr.modules be in zmk's buildInputs without causing trouble?
    contents = lib.singleton (referToPackages "deps-refs" (zmk.buildInputs ++ zmk.nativeBuildInputs ++ zmk.zephyrModuleDeps));
  };

  compileScript = writeShellScriptBin "compileZmk" ''
    set -euo pipefail
    export PATH=${lib.makeBinPath (with pkgs; zmk.nativeBuildInputs)}:$PATH
    export CMAKE_PREFIX_PATH=${zephyr}
    cmake -G Ninja -S ${zmk.src}/app ${lib.escapeShellArgs zmk.cmakeFlags} "-DUSER_CACHE_DIR=/tmp/.cache"
    ninja
  '';
in
dockerTools.buildImage {
  name = "zmk-builder";
  tag = "latest";
  fromImage = depsImage;
  contents = [ compileScript pkgs.busybox ];

  config = {
    User = "deploy";
    WorkingDir = "/data";
    Cmd = [ compileScript ];
  };
}
