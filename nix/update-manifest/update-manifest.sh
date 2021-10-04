#!/usr/bin/env bash

set -euo pipefail

ignored_modules=(hal_espressif)

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
