{ lib
, stdenv
, fetchFromGitHub
, pkg-config
, meson
, ninja
, cairo
, glib
, git
, json_c
, libevdev
, libinput
, libxml2
, pcre
, pango
, wayland
, wayland-protocols
, wlroots
, libxcb
, libxkbcommon
, xwayland
, libdrm
, scdoc
}:

stdenv.mkDerivation rec {
  pname = "labwc";
  version = "unstable-2021-08-15";

  src = ./.;

  nativeBuildInputs = [ pkg-config meson ninja scdoc git ];
  buildInputs = [
    cairo
    glib
    json_c # for sway
    libevdev # sway
    libinput
    libxml2
    pango
    pcre # sway
    wayland
    wayland-protocols
    wlroots
    libxcb
    libxkbcommon
    xwayland
    libdrm
  ];

  mesonFlags = [ "-Dxwayland=enabled" ];

  meta = with lib; {
    homepage = "https://github.com/johanmalm/labwc";
    description = "Openbox alternative for Wayland";
    license = licenses.gpl2Plus;
    maintainers = with maintainers; [ AndersonTorres ];
    platforms = platforms.unix;
  };
}
