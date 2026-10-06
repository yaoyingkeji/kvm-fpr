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

/* 硬件指纹条目类型（供应用时区分 XML 替换规则） */
typedef enum _fpr_type_t
{
    FPR_TYPE_UUID = 0,          /* 域 UUID */
    FPR_TYPE_MAC,               /* 网卡 MAC */
    FPR_TYPE_SERIAL,            /* SMBIOS serial */
    FPR_TYPE_SYSTEM_UUID,       /* SMBIOS system-uuid */
    FPR_TYPE_PRODUCT_UUID,      /* SMBIOS product-uuid */
    FPR_TYPE_PRODUCT_SERIAL     /* SMBIOS product-serial */
} FprType;

/* 硬件指纹条目：name 为条目名，current 为当前值，newval 为新值（可编辑） */
typedef struct _fpr_item_t
{
    FprType type;   /* 条目类型 */
    char *name;     /* 条目名，如"域 UUID"、"网卡 1 MAC"、"SMBIOS serial" */
    char *current;  /* 当前值（只读展示） */
    char *newval;   /* 新值（自动随机生成，GUI 支持手动修改） */
} FprItem;

/* 列出所有虚拟机（virsh list --all --name）
 * 成功返回 0，并把名称写入 names（调用方需用 fpr_free_names 释放） */
int fpr_list_vms(const char *sudo_pass, char ***names, int *count, FprLogFn log, void *ctx);

/* 释放 fpr_list_vms 返回的名称数组 */
void fpr_free_names(char **names, int count);

/* 读取指定虚拟机的当前硬件指纹，并为每一项自动随机生成新值：
 *  - 域 UUID、各网卡 MAC 地址、SMBIOS 序列号/UUID 等（仅存在的条目）
 *  - items 由调用方用 fpr_free_items 释放
 * 返回 0 成功，非 0 失败 */
int fpr_get_items(const char *sudo_pass, const char *vm,
                  FprItem ***items, int *count,
                  FprLogFn log, void *ctx);

/* 释放 fpr_get_items 返回的条目数组 */
void fpr_free_items(FprItem **items, int count);

/* 仅为已有条目重新随机生成新值（current 不变），供"重新随机"按钮使用 */
void fpr_regen_values(FprItem **items, int count);

/* 应用新的硬件指纹（items 中的 newval 应用到虚拟机，等同"重新克隆"）：
 *  - sudo_pass 为 sudo 密码（可为空，若当前用户为 root 则无需密码）
 *  - force_off: 1 表示运行中强制关机，0 表示先尝试优雅关机
 *  - reset_guest: 1 表示重置 guest 内部 machine-id（需要 virt-customize）
 * 返回 0 成功，非 0 失败 */
int fpr_apply_items(const char *sudo_pass, const char *vm,
                    FprItem **items, int count,
                    int force_off, int reset_guest,
                    FprLogFn log, void *ctx);

/* 便捷刷新：自动随机生成全部新指纹并应用（等价 fpr_get_items + fpr_apply_items） */
int fpr_refresh(const char *sudo_pass, const char *vm,
                int force_off, int reset_guest,
                FprLogFn log, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KVM_FPR_H */
