{ pkgs ? import ./nix/pinned-nixpkgs.nix }:

with pkgs;

let
  bundleEnv = bundlerEnv {
    name = "lambda-bundler-env";
    inherit ruby;
    gemfile  = ./Gemfile;
    lockfile = ./Gemfile.lock;
    gemset   = ./gemset.nix;
  };

  source = stdenv.mkDerivation {
    name    = "lambda-builder";
    version = "0.0.1";
    src = ./.;
    installPhase = ''
      cp -r ./ $out
    '';
  };

in
{
  inherit bundleEnv source;
}
