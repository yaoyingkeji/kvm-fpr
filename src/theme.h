/*
 * kvm-fpr - KVM 虚拟机硬件指纹刷新工具
 *
 * 作者：彭刚要
 * 邮箱：pgy866@163.com | yaoying@yaoying.vip
 * 主页：www.yaoying.vip
 *
 * Copyright (C) 2026 彭刚要
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * theme.h - 主题系统（内置皮肤 / 跟随系统 / 用户自建）
 */

#ifndef KVM_FPR_THEME_H
#define KVM_FPR_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/* 主题：颜色均为 #RRGGBB 格式 */
typedef struct _fpr_theme_t
{
    char *name;        /* 主题名 */
    char *bg;          /* 窗口背景 */
    char *fg;          /* 前景/文字 */
    char *accent;      /* 主色（按钮/强调） */
    char *accent_fg;   /* 主色上的文字 */
    char *input_bg;    /* 输入框背景 */
    char *input_fg;    /* 输入框文字 */
    char *header_bg;   /* 表头背景 */
    char *link;        /* 链接色 */
} FprTheme;

/* 加载全部主题（内置 + 用户自定义 ~/.config/kvm-fpr/themes.conf）
 * 返回数组用 theme_free_all 释放 */
int theme_load_all(FprTheme ***themes, int *count);

/* 释放主题数组 */
void theme_free_all(FprTheme **themes, int count);

/* 保存用户自建主题（追加到 themes.conf，重名则覆盖） */
int theme_save_user(const FprTheme *t);

/* 检测系统亮暗模式：返回 1 暗色、0 亮色、-1 未知 */
int theme_detect_dark(void);

/* 返回"跟随系统"主题名（常量串） */
const char *theme_system_name(void);

#ifdef __cplusplus
}
#endif

#endif /* KVM_FPR_THEME_H */
