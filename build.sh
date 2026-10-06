#!/bin/bash
# ============================================================
# kvm-fpr 一键构建脚本
#
# 作者：彭刚要
# 邮箱：pgy866@163.com | yaoying@yaoying.vip
# 主页：www.yaoying.vip
#
# Copyright (C) 2026 彭刚要
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 功能：获取 NAppGUI SDK -> 编译 Release 二进制
#       -> 生成 .deb -> 生成 .AppImage
# 用法：./build.sh [版本号]   (默认 1.0.0)
# ============================================================
set -e

VERSION="${1:-1.1.0}"
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$ROOT/build"
NAPPGUI="$WORK/nappgui"
DEB="$ROOT/kvm-fpr_${VERSION}_amd64.deb"
APPIMAGE="$ROOT/kvm-fpr-${VERSION}-x86_64.AppImage"

echo "==> [1/5] 准备构建目录"
mkdir -p "$WORK"
cd "$WORK"

echo "==> [2/5] 获取 NAppGUI 源码 SDK (MIT)"
if [ ! -d "$NAPPGUI" ]; then
    git clone --depth 1 https://github.com/frang75/nappgui_src.git "$NAPPGUI"
fi

echo "==> [3/5] 复制源码并编译 (Release)"
rm -rf "$NAPPGUI/src/kvmfpr"
mkdir -p "$NAPPGUI/src/kvmfpr"
cp -r "$ROOT/src/"* "$NAPPGUI/src/kvmfpr/"
printf 'nap_desktop_app(kvmfpr "" NRC_NONE)\nset_target_properties(kvmfpr PROPERTIES FOLDER "apps")\n' > "$NAPPGUI/src/kvmfpr/CMakeLists.txt"
# 注册 kvmfpr 目标
if ! grep -q 'src/kvmfpr' "$NAPPGUI/CMakeTargets.cmake"; then
    sed -i 's|if (NAPPGUI_DEMO)|set(ALL_TARGETS ${ALL_TARGETS};src/kvmfpr)\nif (NAPPGUI_DEMO)|' "$NAPPGUI/CMakeTargets.cmake"
fi
cmake -S "$NAPPGUI" -B "$WORK/cmake-build" \
      -DCMAKE_BUILD_TYPE=Release -DNAPPGUI_DEMO=False -DNAPPGUI_WEB=False
cmake --build "$WORK/cmake-build" --target kvmfpr -j"$(nproc)"
BIN="$WORK/cmake-build/Release/bin/kvmfpr"

echo "==> [4/5] 打包 .deb"
rm -rf "$WORK/deb"
mkdir -p "$WORK/deb/DEBIAN" \
         "$WORK/deb/usr/bin" \
         "$WORK/deb/usr/share/applications" \
         "$WORK/deb/usr/share/icons/hicolor/512x512/apps" \
         "$WORK/deb/usr/share/doc/kvm-fpr"
cp "$BIN" "$WORK/deb/usr/bin/kvm-fpr"
cp "$ROOT/packaging/kvm-fpr.desktop" "$WORK/deb/usr/share/applications/"
cp "$ROOT/packaging/kvm-fpr.png" "$WORK/deb/usr/share/icons/hicolor/512x512/apps/kvm-fpr.png"
cp "$ROOT/packaging/DEBIAN/control" "$WORK/deb/DEBIAN/control" 2>/dev/null || true
cp "$ROOT/packaging/DEBIAN/postinst" "$WORK/deb/DEBIAN/postinst" 2>/dev/null || true
chmod 755 "$WORK/deb/usr/bin/kvm-fpr" "$WORK/deb/DEBIAN/postinst" 2>/dev/null || true
dpkg-deb --build --root-owner-group "$WORK/deb" "$DEB"

echo "==> [5/5] 打包 .AppImage"
if [ ! -x "$WORK/appimagetool" ]; then
    curl -L -o "$WORK/appimagetool" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$WORK/appimagetool"
fi
if [ ! -f "$WORK/runtime-x86_64" ]; then
    curl -L -o "$WORK/runtime-x86_64" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
fi
rm -rf "$WORK/AppDir"
mkdir -p "$WORK/AppDir/usr/bin"
cp "$BIN" "$WORK/AppDir/usr/bin/kvm-fpr"
cp "$ROOT/packaging/kvm-fpr.desktop" "$WORK/AppDir/kvm-fpr.desktop"
cp "$ROOT/packaging/kvm-fpr.png" "$WORK/AppDir/kvm-fpr.png"
cat > "$WORK/AppDir/AppRun" <<'APPRUN'
#!/bin/sh
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="$HERE/usr/bin:$PATH"
exec "$HERE/usr/bin/kvm-fpr" "$@"
APPRUN
chmod +x "$WORK/AppDir/AppRun" "$WORK/AppDir/usr/bin/kvm-fpr"
"$WORK/appimagetool" --runtime-file "$WORK/runtime-x86_64" "$WORK/AppDir" "$APPIMAGE"

echo ""
echo "完成！产物："
ls -lh "$DEB" "$APPIMAGE"
