#!/bin/bash
# Build a .deb that runs on Ubuntu 24.04 by shipping the Qt, libei, and
# libportal this tree was linked against. Distro packages of those libraries
# on 24.04 are older than Deskflow requires, and they share SONAMEs with the
# newer libraries, so the binaries must not load the system copies.
set -euo pipefail

BUILD_DIR=${1:?build directory}
OUTPUT_DIR=${2:?output directory}
VERSION=${3:?package version}
QT_ROOT=${QT_ROOT:?set QT_ROOT to the Qt gcc_64 prefix}

if ! [[ $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "version must be major.minor.patch.tweak, got: $VERSION" >&2
  exit 1
fi

for tool in cmake patchelf dpkg-deb dpkg-shlibdeps readelf ldd; do
  command -v "$tool" >/dev/null
done

STAGE=$(mktemp -d)
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

DESTDIR=$STAGE cmake --install "$BUILD_DIR" --prefix /usr

LIBDIR=$STAGE/usr/lib/deskflow
mkdir -p "$LIBDIR/plugins"

# Keep the real executables and qt.conf in a private directory. Qt reads
# qt.conf from the application's own directory, so a qt.conf in /usr/bin
# would redirect every Qt app in /usr/bin (VLC, Krita, ...) to our Qt 6
# plugins and break them. /usr/bin only gets symlinks; Qt resolves
# applicationDirPath through /proc/self/exe, so it still finds this qt.conf.
BINDIR=$LIBDIR/bin
mkdir -p "$BINDIR"
for bin in deskflow deskflow-core; do
  mv "$STAGE/usr/bin/$bin" "$BINDIR/$bin"
  ln -s "../lib/deskflow/bin/$bin" "$STAGE/usr/bin/$bin"
done
if find "$STAGE/usr/bin" -maxdepth 1 -type f | grep -q .; then
  echo "unexpected regular files left in /usr/bin:" >&2
  find "$STAGE/usr/bin" -maxdepth 1 -type f >&2
  exit 1
fi
cat >"$BINDIR/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = .
EOF

PLUGIN_GROUPS=(
  platforms
  platformthemes
  imageformats
  iconengines
  tls
  generic
  networkinformation
  wayland-decoration-client
  wayland-graphics-integration-client
  wayland-shell-integration
  xcbglintegrations
)
for group in "${PLUGIN_GROUPS[@]}"; do
  src="$QT_ROOT/plugins/$group"
  if [[ -d $src ]]; then
    mkdir -p "$LIBDIR/plugins/$group"
    find "$src" -maxdepth 1 -type f -name '*.so' -exec cp -a {} "$LIBDIR/plugins/$group/" \;
  fi
done

copy_elf() {
  local src=$1
  local real soname
  real=$(readlink -f "$src")
  soname=$(readelf -d "$real" | awk -F'[][]' '/SONAME/ { print $2; exit }')
  local real_name
  real_name=$(basename "$real")
  if [[ ! -e $LIBDIR/$real_name ]]; then
    cp -a "$real" "$LIBDIR/$real_name"
  fi
  if [[ -n $soname && ! -e $LIBDIR/$soname ]]; then
    ln -sfn "$real_name" "$LIBDIR/$soname"
  fi
}

# Roots whose libraries must travel inside the package.
bundle_root() {
  local path=$1
  [[ $path == "$QT_ROOT"/* || ( -n ${EXTRA_BUNDLE_ROOT:-} && $path == "$EXTRA_BUNDLE_ROOT"/* ) || $path == /opt/qt/* || $path == /usr/local/* || $path == /home/*/Qt/* ]]
}

pending=()
while IFS= read -r -d '' bin; do
  pending+=("$bin")
done < <(find "$BINDIR" "$LIBDIR/plugins" -type f -print0)

seen_objects=" "
round=0
while ((${#pending[@]} > 0)); do
  round=$((round + 1))
  if ((round > 20)); then
    echo "dependency walk did not finish" >&2
    exit 1
  fi
  next=()
  for obj in "${pending[@]}"; do
    if [[ $seen_objects == *" $obj "* ]]; then
      continue
    fi
    seen_objects+=" $obj "
    if ! file -b "$obj" | grep -q 'ELF'; then
      continue
    fi
    while read -r dep; do
      case $dep in
      "" | "not") continue ;;
      esac
      if [[ $dep == "not found" ]]; then
        echo "unresolved library while scanning $obj" >&2
        ldd "$obj" >&2 || true
        exit 1
      fi
      if bundle_root "$dep"; then
        copy_elf "$dep"
        real=$(readlink -f "$dep")
        base=$(basename "$real")
        if [[ $seen_objects != *" $LIBDIR/$base "* ]]; then
          next+=("$LIBDIR/$base")
        fi
      fi
    done < <(ldd "$obj" | awk '/=>/ { print $3 }')
  done
  pending=("${next[@]}")
done

rpath_for() {
  python3 - "$1" "$LIBDIR" <<'PY'
import os, sys
origin = os.path.dirname(sys.argv[1])
print("$ORIGIN/" + os.path.relpath(sys.argv[2], origin))
PY
}

while IFS= read -r -d '' obj; do
  if file -b "$obj" | grep -q 'ELF'; then
    patchelf --set-rpath "$(rpath_for "$obj")" "$obj"
    strip --strip-unneeded "$obj" || true
  fi
done < <(find "$LIBDIR" -type f -print0)

# LD_LIBRARY_PATH is searched before RUNPATH, so the check has to ignore it.
echo "Bundled library check:"
env -u LD_LIBRARY_PATH ldd "$BINDIR/deskflow" | sed 's/^/  /'
if env -u LD_LIBRARY_PATH ldd "$BINDIR/deskflow" | grep -E 'Qt/|/usr/local/|/opt/qt/|not found'; then
  echo "deskflow still loads a library from the build machine" >&2
  exit 1
fi

mkdir -p "$STAGE/DEBIAN" "$STAGE/debian"
cat >"$STAGE/debian/control" <<'EOF'
Source: deskflow

Package: deskflow
Architecture: amd64
EOF
(
  cd "$STAGE"
  # Bundled Qt/libei/libportal have no dpkg symbol files. System libraries still do.
  dpkg-shlibdeps --ignore-missing-info -lusr/lib/deskflow -O usr/lib/deskflow/bin/deskflow usr/lib/deskflow/bin/deskflow-core >"$STAGE/DEBIAN/substvars"
)
rm -rf "$STAGE/debian"
depends=$(sed -n 's/^shlibs:Depends=//p' "$STAGE/DEBIAN/substvars" | tr -d '\n')
# A build machine with distro Qt 6 / libei / libportal installed has symbols
# files for those SONAMEs, so dpkg-shlibdeps maps our private copies to the
# distro packages. They are bundled; drop them from Depends.
depends=$(printf '%s' "$depends" | tr ',' '\n' | sed 's/^ *//' |
  grep -vE '^(libqt6[a-z0-9-]*|libei1|libportal1)( |$)' | paste -sd, - | sed 's/,/, /g')
if [[ -z $depends ]]; then
  echo "dpkg-shlibdeps produced no dependencies" >&2
  exit 1
fi
installed_size=$(du -sk "$STAGE/usr" | awk '{ print $1 }')
cat >"$STAGE/DEBIAN/control" <<EOF
Package: deskflow
Version: $VERSION
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Deskflow Devs <maintainers@deskflow.org>
Installed-Size: $installed_size
Depends: $depends
Homepage: https://deskflow.org
Description: Keyboard and mouse sharing utility (wxwok monitor-layout build)
 Deskflow shares a keyboard and mouse across computers.
 This package is the wxwok per-monitor layout build. Qt, libei, and libportal
 are private copies under /usr/lib/deskflow so it installs on Ubuntu 24.04.
EOF
rm -f "$STAGE/DEBIAN/substvars"

mkdir -p "$OUTPUT_DIR"
outfile="$OUTPUT_DIR/deskflow-${VERSION}-ubuntu-24.04-amd64.deb"

dpkg-deb --root-owner-group --build "$STAGE" "$outfile"
echo "Wrote $outfile"
if dpkg-deb -c "$outfile" | grep -E ' \./usr/bin/qt\.conf$'; then
  echo "package must not ship /usr/bin/qt.conf" >&2
  exit 1
fi
