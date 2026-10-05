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
 * 本程序为自由软件：您可依据自由软件基金会发布的 GNU 通用公共许可证
 * 第 3 版（或更高版本）的条款重新分发和/或修改本程序。
 *
 * fpr.h - 后端逻辑接口
 */

#ifndef KVM_FPR_H
#define KVM_FPR_H

#ifdef __cplusplus
extern "C" {
#endif

/* 日志回调：ctx 为用户上下文，msg 为要输出的文本 */
typedef void (*FprLogFn)(void *ctx, const char *msg);

/* 列出所有虚拟机（virsh list --all --name）
 * 成功返回 0，并把名称写入 names（调用方需用 fpr_free_names 释放） */
int fpr_list_vms(const char *sudo_pass, char ***names, int *count, FprLogFn log, void *ctx);

/* 释放 fpr_list_vms 返回的名称数组 */
void fpr_free_names(char **names, int count);

/* 刷新指定虚拟机的硬件指纹：
 *  - 生成全新 UUID、MAC 地址、SMBIOS 序列号
 *  - sudo_pass 为 sudo 密码（可为空，若当前用户为 root 则无需密码）
 *  - force_off: 1 表示运行中强制关机，0 表示先尝试优雅关机
 *  - reset_guest: 1 表示重置 guest 内部 machine-id（需要 virt-customize）
 * 返回 0 成功，非 0 失败 */
int fpr_refresh(const char *sudo_pass, const char *vm,
                int force_off, int reset_guest,
                FprLogFn log, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KVM_FPR_H */
