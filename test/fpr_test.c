/*
 * fpr_test.c - kvm-fpr 后端逻辑独立测试驱动（无需 GUI 环境）
 *
 * 作者：彭刚要
 * 邮箱：pgy866@163.com | yaoying@yaoying.vip
 * 主页：www.yaoying.vip
 *
 * Copyright (C) 2026 彭刚要
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 编译：gcc -O2 -I src -o fpr_test src/fpr.c test/fpr_test.c
 * 用法：./fpr_test <虚拟机名> [sudo密码]
 */

#include "fpr.h"
#include <stdio.h>
#include <string.h>

static void mylog(void *ctx, const char *msg)
{
    (void)ctx;
    printf("[LOG] %s\n", msg);
}

int main(int argc, char **argv)
{
    const char *vm = argc > 1 ? argv[1] : "test-vm";
    const char *pass = argc > 2 ? argv[2] : "";
    int rc;

    printf("== 开始刷新虚拟机 %s ==\n", vm);
    rc = fpr_refresh(pass, vm, 1, 0, mylog, NULL);
    printf("== rc=%d ==\n", rc);
    return rc;
}
