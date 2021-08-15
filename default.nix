{ lib
, stdenv
, fetchFromGitHub
, pkg-config
, meson
, ninja
, cairo
, glib
, git
, libinput
, libxml2
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
  version = "unstable-2021-03-15";

  src = ./.;

  nativeBuildInputs = [ pkg-config meson ninja scdoc git ];
  buildInputs = [
    cairo
    glib
    libinput
    libxml2
    pango
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
