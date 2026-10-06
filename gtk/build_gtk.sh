#!/bin/bash
# ============================================================
# kvm-fpr GTK3 版一键构建脚本
#
# 作者：彭刚要
# 邮箱：pgy866@163.com | yaoying@yaoying.vip
# 主页：www.yaoying.vip
#
# Copyright (C) 2026 彭刚要
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 用法：./build_gtk.sh
# 依赖：libgtk-3-dev
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/kvm-fpr-gtk"

gcc -O2 -Wall -I"$ROOT/../src" "$ROOT/main_gtk.c" "$ROOT/../src/fpr.c" \
    $(pkg-config --cflags --libs gtk+-3.0) -o "$OUT"

echo "构建完成：$OUT"
