#!/usr/bin/env bash
# Build and package MIDITrailLinux on Fedora (CI runs this in a fedora container;
# the same script works locally: podman run --rm -v "$PWD:/src:Z" -w /src fedora:44 \
#   .github/scripts/build-linux.sh).
#
# Output: dist/MIDITrail-mod_mod-linux-x86_64.tar.gz with the binary, conf/data/doc
# and the DXVK Native / vkd3d libraries (found next to the binary via RUNPATH $ORIGIN).
set -euo pipefail

if [ "${SKIP_DEPS:-0}" != "1" ] && command -v dnf >/dev/null; then
    dnf install -y --setopt=install_weak_deps=False \
        git xmake gcc-c++ make tar gzip unzip curl findutils binutils patchelf \
        SDL3-devel dxvk-native-devel libvkd3d-utils-devel libvkd3d-shader-devel \
        vulkan-loader-devel alsa-lib-devel freetype-devel fontconfig-devel
fi

# xmake refuses to run as root unless told otherwise (containers run as root)
export XMAKE_ROOT=y

xmake f -p linux -a x86_64 -m release -y
xmake build -y MIDITrailLinux

bin=build/linux/x86_64/release/MIDITrailLinux
name=MIDITrail-mod_mod-linux-x86_64
dist=dist/$name
rm -rf "$dist"
mkdir -p "$dist"
cp "$bin" "$dist/"
strip "$dist/MIDITrailLinux"
cp -r Resources/conf Resources/data Resources/doc "$dist/"
cp README.linux.md "$dist/"
cp Resources/LICENSE.txt "$dist/" 2>/dev/null || true
cp readme_mod_mod_ced.txt "$dist/" 2>/dev/null || true

# bundle DXVK Native and vkd3d (rarely packaged outside Fedora/Arch); SDL3, Vulkan,
# ALSA, FreeType and fontconfig come from the system
bundled_deps() {   # print the resolved paths of $1's DXVK/vkd3d dependencies
    ldd "$1" | awk '/=> \// { print $3 }' | { grep -E '/lib(dxvk_|vkd3d)' || true; }
}
for lib in $(bundled_deps "$bin"); do
    cp -L "$lib" "$dist/$(basename "$lib")"
done
# their own DXVK/vkd3d dependencies (e.g. vkd3d-utils -> libvkd3d)
for bundled in "$dist"/lib*.so.*; do
    for dep in $(bundled_deps "$bundled"); do
        [ -e "$dist/$(basename "$dep")" ] || cp -L "$dep" "$dist/$(basename "$dep")"
    done
done

# RUNPATH is not transitive: let the bundled libraries find each other too
for bundled in "$dist"/lib*.so.*; do
    patchelf --set-rpath '$ORIGIN' "$bundled"
done

tar -C dist -czf "dist/$name.tar.gz" "$name"
echo "packaged dist/$name.tar.gz:"
tar -tzf "dist/$name.tar.gz" | grep -vE '/(conf|data|doc)/.' || true
