{ stdenvNoCC, lib, fetchgit, runCommand }:
let
  manifestJSON = builtins.fromJSON (builtins.readFile ./manifest.json);

  mkModule = { name, revision, url, sha256, ... }:
    stdenvNoCC.mkDerivation (finalAttrs: {
      name = "zmk-module-${name}";

      src = fetchgit {
        inherit name url sha256;
        rev = revision;
      };

      dontUnpack = true;
      dontBuild = true;

      installPhase = ''
        mkdir $out
        ln -s ${finalAttrs.src} $out/${name}
      '';

      passthru = {
        modulePath = "${finalAttrs.finalPackage}/${name}";
      };
   });

  modules = lib.listToAttrs (lib.forEach manifestJSON ({ name, ... }@args:
    lib.nameValuePair name (mkModule args)));
in


# Zephyr with no modules, from the frozen manifest.
# For now the modules are passed through as passthru
stdenvNoCC.mkDerivation {
  name = "zephyr";
  src = modules.zephyr.src;

  dontBuild = true;

  # This awkward structure is required by
  #   COMMAND ${PYTHON_EXECUTABLE} ${ZEPHYR_BASE}/../tools/uf2/utils/uf2conv.py
  installPhase = ''
    mkdir -p $out/zephyr
    mv * $out/zephyr

    # uf2 is gone, not sure what replaced it
  '';

  passthru = {
    modules = removeAttrs modules ["zephyr"];
  };
}
