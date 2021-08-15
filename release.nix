let overlay = self: super: {
      mesonNew = super.meson.overrideAttrs (o: rec {
        version = "0.58.1";
        pname = "meson";
        src = self.python3.pkgs.fetchPypi {
          inherit pname version;
          sha256 = "0padn0ykwz8azqiwkhi8p97bl742y8lsjbv0wpqpkkrgcvda6i1i";
        };
      });
      wlroots = (super.wlroots.overrideAttrs (o: {
        version = "devel";
        nativeBuildInputs = o.nativeBuildInputs ++ [ self.libseat ];
        src = self.fetchFromGitHub {
          owner = "swaywm";
          repo = "wlroots";
          rev = "3364eec07e91eb51f770f2f7d619a07d96c0b5c4";
          sha256 = "15q61pl5pac80vab7a5yzbh8m2jq3xx8c2mv6zj8yryazxwjg475";
        };
      })).override { meson = self.mesonNew; };
    };
in (import <nixpkgs> {  overlays = [overlay]; }).callPackage ./default.nix {}
