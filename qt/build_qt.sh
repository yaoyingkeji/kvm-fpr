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
# 依赖：qtbase5-dev（qmake / qtchooser）
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

QT_SELECT=qt5 qmake main_qt.pro
QT_SELECT=qt5 make -j"$(nproc)"

echo "构建完成：$ROOT/kvm-fpr-qt"
