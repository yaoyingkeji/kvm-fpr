#!/bin/bash
# ============================================================
# kvm-fpr GTK / Qt 版本打包脚本
# 功能：把 gtk/ 与 qt/ 版编译产物打包为 .deb 和 .AppImage
#
# 作者：彭刚要
# 邮箱：pgy866@163.com | yaoying@yaoying.vip
# 主页：www.yaoying.vip
#
# Copyright (C) 2026 彭刚要
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 用法：./build_gtk_qt.sh [版本号]   (默认 1.0.0)
# 前置：先运行 gtk/build_gtk.sh 与 qt/build_qt.sh 生成二进制
# ============================================================
set -e

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/build/pkgs"
ICON="$ROOT/packaging/kvm-fpr.png"

# appimagetool / runtime 缓存
if [ ! -x "$ROOT/build/appimagetool" ]; then
    curl -L -o "$ROOT/build/appimagetool" \
        https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
    chmod +x "$ROOT/build/appimagetool"
fi
if [ ! -f "$ROOT/build/runtime-x86_64" ]; then
    curl -L -o "$ROOT/build/runtime-x86_64" \
        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64
fi

mkdir -p "$WORK"

# ---------------- 打包一个变体 ----------------
# $1 = 变体名 (gtk|qt)   $2 = 二进制名   $3 = desktop 文件   $4 = deb 依赖
pack_variant() {
    local V=$1 BIN=$2 DESK=$3 DEPS=$4
    local BINPATH="$ROOT/$V/$BIN"
    echo "==> 打包 $V 版 ($BIN)"

    if [ ! -x "$BINPATH" ]; then
        echo "错误: 未找到 $BINPATH ，请先运行 $V/build_$V.sh"
        exit 1
    fi

    # ---- .deb ----
    local D="$WORK/${BIN}_${VERSION}_amd64"
    rm -rf "$D"
    mkdir -p "$D/DEBIAN" "$D/usr/bin" "$D/usr/share/applications" \
             "$D/usr/share/icons/hicolor/512x512/apps" "$D/usr/share/doc/${BIN}"
    cp "$BINPATH" "$D/usr/bin/$BIN"
    cp "$DESK" "$D/usr/share/applications/"
    cp "$ICON" "$D/usr/share/icons/hicolor/512x512/apps/${BIN}.png"
    cat > "$D/DEBIAN/control" <<EOF
Package: $BIN
Version: $VERSION
Section: utils
Priority: optional
Architecture: amd64
Depends: $DEPS
Recommends: libguestfs-tools
Maintainer: 彭刚要 <pgy866@163.com>
Homepage: http://www.yaoying.vip
Description: KVM VM hardware fingerprint refresher (${V} version)
 A graphical tool (C ${V} GUI, GPLv3) that refreshes an existing KVM
 virtual machine's hardware fingerprint like re-cloning: it regenerates
 the domain UUID, all NIC MAC addresses, SMBIOS serials and (optionally)
 the guest machine-id, giving the VM a brand-new fingerprint.
EOF
    cat > "$D/usr/share/doc/${BIN}/README" <<EOF
kvm-fpr (${V} 版) - KVM 虚拟机硬件指纹刷新工具
版权: 彭刚要 | 协议: GPLv3 | 项目: https://github.com/yaoyingkeji/kvm-fpr
用法: 选择虚拟机 -> 输入 sudo 密码 -> 点击"刷新硬件指纹 / 机器码"
备份恢复: sudo virsh define ~/kvm-fpr-backups/<备份文件>
EOF
    chmod 755 "$D/usr/bin/$BIN"
    dpkg-deb --build --root-owner-group "$D" "$ROOT/${BIN}_${VERSION}_amd64.deb" >/dev/null
    echo "   .deb 完成: $ROOT/${BIN}_${VERSION}_amd64.deb"

    # ---- .AppImage ----
    local A="$WORK/${BIN}.AppDir"
    rm -rf "$A"
    mkdir -p "$A/usr/bin"
    cp "$BINPATH" "$A/usr/bin/$BIN"
    cp "$DESK" "$A/${BIN}.desktop"
    cp "$ICON" "$A/${BIN}.png"
    cat > "$A/AppRun" <<EOF
#!/bin/sh
SELF=\$(readlink -f "\$0")
HERE=\${SELF%/*}
export PATH="\$HERE/usr/bin:\$PATH"
exec "\$HERE/usr/bin/$BIN" "\$@"
EOF
    chmod +x "$A/AppRun" "$A/usr/bin/$BIN"
    "$ROOT/build/appimagetool" --runtime-file "$ROOT/build/runtime-x86_64" "$A" "$ROOT/${BIN}-${VERSION}-x86_64.AppImage" >/dev/null
    echo "   .AppImage 完成: $ROOT/${BIN}-${VERSION}-x86_64.AppImage"
}

pack_variant gtk    kvm-fpr-gtk    "$ROOT/gtk/kvm-fpr-gtk.desktop"    "libgtk-3-0 (>= 3.10), libvirt-clients, python3, sudo"
pack_variant qt     kvm-fpr-qt     "$ROOT/qt/kvm-fpr-qt.desktop"     "libqt5widgets5 (>= 5.5), libvirt-clients, python3, sudo"

echo ""
echo "完成！GTK / Qt 版安装包："
ls -lh "$ROOT"/kvm-fpr-gtk_*.deb "$ROOT"/kvm-fpr-gtk-*.AppImage \
       "$ROOT"/kvm-fpr-qt_*.deb  "$ROOT"/kvm-fpr-qt-*.AppImage
