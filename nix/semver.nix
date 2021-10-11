{ lib, stdenv, fetchFromGitHub, buildGoModule }:

buildGoModule {
  name = "semver";

  src = fetchFromGitHub {
    owner = "ffurrer2";
    repo = "semver";
    rev = "0d3cab9dd5738f5c9b986794b8d19f3ee6cd0112";
    sha256 = "sha256-2m6pAAGBtkmrSv2LwwFlcsyG2yUkhrWuTv+ynjilGxI=";
  };

  patches = [./semver.patch.gz];

  vendorHash = "sha256-IotWZIiGNwdPkjK4C24kUXqsDZuLv53ThhYcJVj+Krk=";
}
