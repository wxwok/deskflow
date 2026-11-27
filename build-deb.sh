#!/bin/bash
# Build script for deskflow .deb package on Ubuntu 24.04
# Bundles libportal 0.8.1 and Qt 6.8.3 in a private directory
# to avoid conflicts with system libraries.
#
# Usage: ./build-deb.sh [QT_DIR]
#   QT_DIR: Path to Qt installation (default: $HOME/Qt/6.8.3/gcc_64)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DEPS_DIR="$SCRIPT_DIR/build-deps"
STAGE_DIR="$BUILD_DIR/deb-staging"
QT_DIR="${1:-$HOME/Qt/6.8.3/gcc_64}"
JOBS="$(nproc)"

LIBPORTAL_VERSION="0.8.1"
LIBPORTAL_TAG="0.8.1"

PRIVATE_LIBDIR="/usr/lib/deskflow"

log() { echo "==> $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
log "Checking prerequisites..."

[[ -d "$QT_DIR" ]] || die "Qt directory not found: $QT_DIR"
[[ -f "$QT_DIR/lib/libQt6Core.so.6" ]] || die "Qt6Core not found in $QT_DIR"

for cmd in cmake g++ pkg-config patchelf dpkg-deb git meson ninja; do
  command -v "$cmd" &>/dev/null || die "Missing required tool: $cmd"
done

pkg-config --exists "libei-1.0" || die "libei-dev not installed (apt install libei-dev)"

# ---------------------------------------------------------------------------
# Build libportal from source
# ---------------------------------------------------------------------------
build_libportal() {
  local src="$DEPS_DIR/libportal"
  local prefix="$DEPS_DIR/install"

  if pkg-config --exists "libportal >= $LIBPORTAL_VERSION" 2>/dev/null; then
    log "System libportal >= $LIBPORTAL_VERSION found, skipping build"
    return 0
  fi

  if [[ -f "$prefix/lib/x86_64-linux-gnu/pkgconfig/libportal.pc" ]]; then
    local installed_ver
    installed_ver=$(PKG_CONFIG_PATH="$prefix/lib/x86_64-linux-gnu/pkgconfig" \
                    pkg-config --modversion libportal 2>/dev/null || echo "0")
    if [[ "$installed_ver" == "$LIBPORTAL_VERSION" ]]; then
      log "libportal $LIBPORTAL_VERSION already built, skipping"
      return 0
    fi
  fi

  log "Building libportal $LIBPORTAL_VERSION from source..."
  mkdir -p "$DEPS_DIR"

  if [[ ! -d "$src" ]]; then
    git clone --depth 1 --branch "$LIBPORTAL_TAG" \
      https://github.com/flatpak/libportal.git "$src"
  fi

  cd "$src"
  rm -rf builddir
  meson setup builddir --prefix="$prefix" \
    -Dbackend-gtk3=disabled \
    -Dbackend-gtk4=disabled \
    -Dbackend-qt5=disabled \
    -Dbackend-qt6=disabled \
    -Ddocs=false \
    -Dtests=false \
    -Dvapi=false \
    -Dintrospection=false

  ninja -C builddir
  ninja -C builddir install
  cd "$SCRIPT_DIR"
}

# ---------------------------------------------------------------------------
# Configure & build deskflow
# ---------------------------------------------------------------------------
build_deskflow() {
  log "Configuring deskflow..."

  local portal_prefix="$DEPS_DIR/install"
  local extra_pkg_config=""

  if [[ -d "$portal_prefix/lib/x86_64-linux-gnu/pkgconfig" ]]; then
    extra_pkg_config="$portal_prefix/lib/x86_64-linux-gnu/pkgconfig"
  fi

  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"

  PKG_CONFIG_PATH="${extra_pkg_config:+$extra_pkg_config:}${PKG_CONFIG_PATH:-}" \
  cmake "$SCRIPT_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_RPATH="$PRIVATE_LIBDIR" \
    -DBUILD_INSTALLER=ON

  log "Building deskflow (using $JOBS cores)..."
  cmake --build . -j"$JOBS"

  cd "$SCRIPT_DIR"
}

# ---------------------------------------------------------------------------
# Resolve library dependencies to bundle
# ---------------------------------------------------------------------------
collect_qt_libs() {
  local dest="$1"
  local qt_lib="$QT_DIR/lib"

  local qt_modules=(
    Qt6Core Qt6DBus Qt6Gui Qt6Network Qt6Widgets Qt6Xml Qt6XcbQpa
    Qt6WaylandClient Qt6WlShellIntegration Qt6OpenGL
  )

  for mod in "${qt_modules[@]}"; do
    local sofile="$qt_lib/lib${mod}.so.6"
    if [[ -e "$sofile" ]]; then
      cp -a "${sofile}"* "$dest/"
    fi
  done

  # ICU libraries bundled with Qt
  for lib in icui18n icuuc icudata; do
    local pattern="$qt_lib/lib${lib}.so.*"
    # shellcheck disable=SC2086
    if compgen -G "$pattern" &>/dev/null; then
      cp -a $pattern "$dest/"
    fi
  done

  # Qt platform plugins
  local plugin_dir="$dest/plugins/platforms"
  mkdir -p "$plugin_dir"
  for plugin in libqxcb.so libqwayland-egl.so libqwayland-generic.so; do
    local src="$QT_DIR/plugins/platforms/$plugin"
    [[ -f "$src" ]] && cp "$src" "$plugin_dir/"
  done

  local theme_dir="$dest/plugins/platformthemes"
  mkdir -p "$theme_dir"
  [[ -f "$QT_DIR/plugins/platformthemes/libqgtk3.so" ]] && \
    cp "$QT_DIR/plugins/platformthemes/libqgtk3.so" "$theme_dir/"

  # Wayland shell integration plugins
  local wsi_dir="$dest/plugins/wayland-shell-integration"
  if [[ -d "$QT_DIR/plugins/wayland-shell-integration" ]]; then
    mkdir -p "$wsi_dir"
    cp "$QT_DIR/plugins/wayland-shell-integration/"*.so "$wsi_dir/" 2>/dev/null || true
  fi
}

# ---------------------------------------------------------------------------
# Assemble the .deb staging tree
# ---------------------------------------------------------------------------
assemble_deb() {
  log "Assembling .deb package..."

  rm -rf "$STAGE_DIR"
  DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR"

  # Remove dev/test artifacts installed by googletest
  rm -rf "$STAGE_DIR/usr/local/include" \
         "$STAGE_DIR/usr/local/lib" \
         "$STAGE_DIR/usr/local/share/doc/deskflow/html"

  # Relocate from usr/local to usr
  mkdir -p "$STAGE_DIR/usr/bin" "$STAGE_DIR/usr/share"
  cp -a "$STAGE_DIR/usr/local/bin/"* "$STAGE_DIR/usr/bin/"
  cp -a "$STAGE_DIR/usr/local/share/"* "$STAGE_DIR/usr/share/"
  rm -rf "$STAGE_DIR/usr/local"

  # Private lib directory
  local privlib="$STAGE_DIR$PRIVATE_LIBDIR"
  mkdir -p "$privlib"

  # Bundle libportal if built locally
  local portal_lib="$DEPS_DIR/install/lib/x86_64-linux-gnu"
  if [[ -f "$portal_lib/libportal.so.1.0.0" ]]; then
    cp -a "$portal_lib"/libportal.so* "$privlib/"
  fi

  # Bundle Qt and ICU
  collect_qt_libs "$privlib"

  # Create wrapper scripts
  for bin in deskflow deskflow-core; do
    mv "$STAGE_DIR/usr/bin/$bin" "$STAGE_DIR/usr/bin/${bin}.bin"
    cat > "$STAGE_DIR/usr/bin/$bin" << WEOF
#!/bin/sh
export LD_LIBRARY_PATH=$PRIVATE_LIBDIR\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
export QT_PLUGIN_PATH=$PRIVATE_LIBDIR/plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}
exec /usr/bin/${bin}.bin "\$@"
WEOF
    chmod +x "$STAGE_DIR/usr/bin/$bin"
  done

  # Determine version from cmake project
  local version
  version=$(grep -oP 'SET\(CPACK_PACKAGE_VERSION\s+"\K[^"]+' "$BUILD_DIR/CPackConfig.cmake" 2>/dev/null \
         || grep -oP 'set\(CPACK_PACKAGE_VERSION\s+"\K[^"]+' "$BUILD_DIR/CPackConfig.cmake" 2>/dev/null \
         || echo "0.0.0")

  # Determine dependencies
  local deps="libglib2.0-0t64, libx11-6, libxtst6, libxext6, libxrandr2"
  deps="$deps, libxinerama1, libxi6, libxkbcommon0, libssl3t64"
  deps="$deps, libei1, libice6, libsm6, libgl1, libdbus-1-3"
  deps="$deps, libxkbfile1, libxcb-cursor0"

  # Check for t64 suffix availability and fall back
  if ! dpkg -l libglib2.0-0t64 &>/dev/null 2>&1; then
    deps="${deps//libglib2.0-0t64/libglib2.0-0}"
  fi
  if ! dpkg -l libssl3t64 &>/dev/null 2>&1; then
    deps="${deps//libssl3t64/libssl3}"
  fi

  local installed_size
  installed_size=$(du -sk "$STAGE_DIR" | cut -f1)

  mkdir -p "$STAGE_DIR/DEBIAN"
  cat > "$STAGE_DIR/DEBIAN/control" << CTRLEOF
Package: deskflow
Version: $version
Architecture: amd64
Maintainer: Deskflow <maintainers@deskflow.org>
Description: Keyboard and mouse sharing utility
 Deskflow is a free and open source keyboard and mouse sharing app.
 Use the keyboard, mouse, or trackpad of one computer to control
 nearby computers, and work seamlessly between them.
 .
 This package bundles Qt 6.8.3, libportal $LIBPORTAL_VERSION, and ICU
 in a private directory to avoid conflicts with system libraries.
Section: utils
Priority: optional
Installed-Size: $installed_size
Depends: $deps
Recommends: qt6-svg-plugins
CTRLEOF

  # Build the .deb
  local arch
  arch=$(dpkg --print-architecture)
  local deb_name="deskflow-${version}-$(lsb_release -cs)-${arch}.deb"
  local deb_path="$BUILD_DIR/$deb_name"

  dpkg-deb --build "$STAGE_DIR" "$deb_path"

  log "Package built: $deb_path"
  log "Size: $(du -h "$deb_path" | cut -f1)"
  echo ""
  echo "To install:"
  echo "  sudo dpkg -i $deb_path"
  echo ""
  echo "To remove:"
  echo "  sudo dpkg -r deskflow"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
log "Building deskflow .deb package"
log "  Source:  $SCRIPT_DIR"
log "  Qt:      $QT_DIR"
log "  Jobs:    $JOBS"
echo ""

build_libportal
build_deskflow
assemble_deb

log "Done!"
