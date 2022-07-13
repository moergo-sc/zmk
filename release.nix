{ pkgs ? import <nixpkgs> {} }:

with pkgs;
let
  zmkPkgs = (import ./default.nix { inherit pkgs; });
  lambda  = (import ./lambda { inherit pkgs; });

  nix-utils = pkgs.fetchFromGitHub {
    owner = "iknow";
    repo = "nix-utils";
    rev = "c13c7a23836c8705452f051d19fc4dff05533b53";
    sha256 = "0ax7hld5jf132ksdasp80z34dlv75ir0ringzjs15mimrkw8zcac";
  };

  ociTools = pkgs.callPackage "${nix-utils}/oci" {};

  inherit (zmkPkgs) zmk zephyr;

  accounts = {
    users.deploy = {
      uid = 999;
      group = "deploy";
      home = "/home/deploy";
      shell = "/bin/sh";
    };
    groups.deploy.gid = 999;
  };

  baseLayer = {
    name = "base-layer";
    path = [ pkgs.busybox ];
    entries = ociTools.makeFilesystem {
      inherit accounts;
      tmp = true;
      usrBinEnv = "${pkgs.busybox}/bin/env";
      binSh = "${pkgs.busybox}/bin/sh";
    };
  };

  depsLayer = {
    name = "deps-layer";
    includes = zmk.buildInputs ++ zmk.nativeBuildInputs ++ zmk.zephyrModuleDeps;
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

  appLayer = {
    name = "app-layer";
    path = [ zmkCompileScript ];
    entries = ociTools.makeUserDirectoryEntries accounts "deploy" [
      "/data"
    ];
  };

  lambdaEntrypoint = writeShellScriptBin "lambdaEntrypoint" ''
    set -euo pipefail
    export PATH=${lib.makeBinPath [ zmkCompileScript ]}:$PATH
    cd ${lambda.source}
    ${lambda.bundleEnv}/bin/bundle exec aws_lambda_ric "app.LambdaFunction::Handler.process"
  '';

  lambdaImage = ociTools.makeSimpleImage {
    name = "zmk-builder-lambda";
    layers = [ baseLayer depsLayer appLayer ];
    config = {
      User = "deploy";
      WorkingDir = "/data";
      Cmd = [ "${lambdaEntrypoint}/bin/lambdaEntrypoint" ];
    };

  };
in {
  inherit lambdaImage zmkCompileScript lambdaEntrypoint;
}
