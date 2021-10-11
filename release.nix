{ pkgs ? import <nixpkgs> {} }:

with pkgs;
let
  zmkPkgs = (import ./default.nix { inherit pkgs; });
  lambda  = (import ./lambda { inherit pkgs; });

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

  zmkCompileScript = writeShellScriptBin "compileZmk" ''
    set -eo pipefail
    if [ ! -f "$1" ]; then
      echo "Usage: compileZmk [file.keymap]" >&2
      exit 1
    fi
    KEYMAP="$(${pkgs.busybox}/bin/realpath $1)"
    export PATH=${lib.makeBinPath (with pkgs; zmk.nativeBuildInputs)}:$PATH
    export CMAKE_PREFIX_PATH=${zephyr}
    cmake -G Ninja -S ${zmk.src}/app ${lib.escapeShellArgs zmk.cmakeFlags} "-DUSER_CACHE_DIR=/tmp/.cache" "-DKEYMAP_FILE=$KEYMAP"
    ninja
  '';

  builderImage = dockerTools.buildImage {
    name = "zmk-builder";
    tag = "latest";
    fromImage = depsImage;
    contents = [ zmkCompileScript pkgs.busybox ];
  };

  lambdaEntrypoint = writeShellScriptBin "lambdaEntrypoint" ''
    set -euo pipefail
    export PATH=${lib.makeBinPath [zmkCompileScript]}:$PATH
    cd ${lambda.source}
    ${lambda.bundleEnv}/bin/bundle exec aws_lambda_ric "app.LambdaFunction::Handler.process"
  '';

  lambdaImage = dockerTools.buildImage {
    name = "zmk-builder-lambda";
    tag = "latest";
    fromImage = builderImage;
    contents = [ lambdaEntrypoint ];
    config = {
      User = "deploy";
      Cmd = [ "${lambdaEntrypoint}/bin/lambdaEntrypoint" ];
    };

  };
in {
  inherit builderImage lambdaImage zmkCompileScript lambdaEntrypoint;
}
