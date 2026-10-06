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
    FPR_TYPE_SERIAL,            /* SMBIOS serial（system） */
    FPR_TYPE_SYSTEM_UUID,       /* SMBIOS system-uuid */
    FPR_TYPE_PRODUCT_UUID,      /* SMBIOS product-uuid */
    FPR_TYPE_PRODUCT_SERIAL,    /* SMBIOS product-serial */
    FPR_TYPE_SYS_MANUFACTURER,  /* SMBIOS system manufacturer（品牌） */
    FPR_TYPE_SYS_PRODUCT,       /* SMBIOS system product */
    FPR_TYPE_SYS_VERSION,       /* SMBIOS system version */
    FPR_TYPE_SYS_SKU,           /* SMBIOS system sku */
    FPR_TYPE_SYS_FAMILY,        /* SMBIOS system family */
    FPR_TYPE_BOARD_MANUFACTURER,/* 主板 manufacturer */
    FPR_TYPE_BOARD_PRODUCT,     /* 主板 product */
    FPR_TYPE_BOARD_SERIAL,      /* 主板 serial */
    FPR_TYPE_CHASSIS_MANUFACTURER, /* 机箱 manufacturer */
    FPR_TYPE_CHASSIS_SERIAL,    /* 机箱 serial */
    FPR_TYPE_DISK_SERIAL        /* 磁盘序列号（每个磁盘一条） */
} FprType;

/* 防虚拟机检测设置（可按位组合） */
enum _fpr_avoid_t
{
    FPR_AVOID_KVM_HIDDEN   = 1 << 0,  /* <kvm><hidden state='on'/></kvm> */
    FPR_AVOID_HYPERVISOR   = 1 << 1,  /* <feature policy='disable' name='hypervisor'/> */
    FPR_AVOID_VMPORT       = 1 << 2,  /* <vmport state='off'/> */
    FPR_AVOID_HYPERV_VENDOR = 1 << 3  /* <hyperv><vendor_id state='on' value='...'/></hyperv> */
};

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

/* 应用新指纹 + 防虚拟机检测设置：
 *  - avoid_flags: FPR_AVOID_* 位组合（0 表示不注入）
 *  - hyperv_vendor: hyperv vendor_id 值（≤12 字符，可为 NULL）
 * 其余同 fpr_apply_items */
int fpr_apply_items_ext(const char *sudo_pass, const char *vm,
                        FprItem **items, int count,
                        unsigned int avoid_flags, const char *hyperv_vendor,
                        int force_off, int reset_guest,
                        FprLogFn log, void *ctx);

/*---------------------------------------------------------------------------*/
/* 品牌伪装模板 */

typedef struct _fpr_template_t
{
    char *name;                /* 模板名 */
    char *sys_manufacturer;    /* SMBIOS system manufacturer */
    char *sys_product;         /* system product */
    char *sys_version;         /* system version */
    char *sys_serial;          /* system serial */
    char *sys_sku;             /* system sku */
    char *sys_family;          /* system family */
    char *board_manufacturer;  /* baseBoard manufacturer */
    char *board_product;       /* baseBoard product */
    char *board_serial;        /* baseBoard serial */
    char *chassis_manufacturer;/* chassis manufacturer */
    char *chassis_serial;      /* chassis serial */
} FprTemplate;

/* 列出全部品牌模板（内置 + 用户自定义）。返回的数组用 fpr_free_templates 释放 */
int fpr_list_templates(FprTemplate ***tpls, int *count);

/* 释放模板数组 */
void fpr_free_templates(FprTemplate **tpls, int count);

/* 把模板值应用到指纹条目（匹配 FPR_TYPE_* 类型填入 newval） */
void fpr_apply_template(FprItem **items, int count, const FprTemplate *tpl);

/* 保存用户自定义模板（写入 ~/.config/kvm-fpr/templates.conf） */
int fpr_save_template(const FprTemplate *tpl);

/*---------------------------------------------------------------------------*/
/* 备份历史与恢复 */

/* 列出指定虚拟机的备份（路径与时间戳）。返回数组用 fpr_free_backups 释放 */
int fpr_list_backups(const char *vm, char ***paths, char ***times, int *count);

void fpr_free_backups(char **paths, char **times, int count);

/* 恢复备份：undefine 现有定义并 define 备份文件 */
int fpr_restore_backup(const char *sudo_pass, const char *vm,
                       const char *backup_path, FprLogFn log, void *ctx);

/* 删除备份文件 */
int fpr_delete_backup(const char *backup_path);

/* 导出虚拟机的当前 XML 配置（dumpxml 内容，调用方用 free 释放） */
int fpr_dumpxml(const char *sudo_pass, const char *vm, char **xml_out,
                FprLogFn log, void *ctx);

/* 应用编辑后的 XML 配置：先备份再 undefine+define（编辑查看功能用） */
int fpr_apply_xml(const char *sudo_pass, const char *vm, const char *xml_text,
                  FprLogFn log, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KVM_FPR_H */
