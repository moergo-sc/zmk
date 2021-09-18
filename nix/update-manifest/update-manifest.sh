#!/usr/bin/env bash

set -euo pipefail

ignored_modules=(hal_espressif sof tflite-micro thrift bsim babblesim babblesim_base \
                 babblesim_ext_2G4_libPhyComv1 babblesim_ext_2G4_channel_NtNcable \
                 babblesim_ext_2G4_channel_multiatt babblesim_ext_2G4_modem_magic \
                 babblesim_ext_2G4_modem_BLE_simple babblesim_ext_2G4_device_burst_interferer \
                 babblesim_ext_2G4_device_WLAN_actmod babblesim_ext_2G4_phy_v1 \
                 babblesim_ext_2G4_device_playback babblesim_ext_libCryptov1)

prefetch_project() {
  local p=$1
  local name
  name="$(jq -r .name <<< "$p")"

  if [[ " ${ignored_modules[*]} " =~ " ${name} " ]]; then
    echo "Skipping: $name" >&2
    return
  fi

  echo "Prefetching: $name" >&2

  sha256=$(nix-prefetch-git \
    --quiet \
    --fetch-submodules \
    --url "$(jq -r .url <<< "$p")" \
    --rev "$(jq -r .revision <<< "$p")" \
    | jq -r .sha256)

  jq --arg sha256 "$sha256" '. + $ARGS.named' <<< "$p"
}


west manifest --freeze | \
  yaml2json | \
  jq -c '.manifest.projects[]' | \
  while read -r p; do prefetch_project "$p"; done | \
  jq --slurp
