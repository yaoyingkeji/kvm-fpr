#!/bin/bash
# ============================================================
# kvm-fpr Qt5 版一键构建脚本
#
# 作者：彭刚要
# 邮箱：pgy866@163.com | yaoying@yaoying.vip
# 主页：www.yaoying.vip
#
# Copyright (C) 2026 彭刚要
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 用法：./build_qt.sh
# 依赖：qt6-base-dev（qmake6）
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

qmake6 main_qt.pro 2>/dev/null || QT_SELECT=qt6 qmake main_qt.pro
make -j"$(nproc)" 2>/dev/null || QT_SELECT=qt6 make -j"$(nproc)"

echo "构建完成：$ROOT/kvm-fpr-qt"
