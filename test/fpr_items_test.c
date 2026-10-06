/*
 * fpr_items_test.c - 指纹条目 API 测试驱动
 * 编译：gcc -O2 -I src -o fpr_items_test src/fpr.c test/fpr_items_test.c
 * 用法：./fpr_items_test <虚拟机名> [sudo密码]
 * 说明：读取当前指纹 -> 随机新值 -> 手动修改为固定值 -> 应用
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
    FprItem **items = NULL;
    int count = 0;
    int rc, i;

    rc = fpr_get_items(pass, vm, &items, &count, mylog, NULL);
    if (rc != 0)
        return 1;

    printf("== 条目数: %d ==\n", count);
    for (i = 0; i < count; ++i)
        printf("  [type=%d] %s: %s -> %s\n",
               items[i]->type, items[i]->name,
               items[i]->current, items[i]->newval);

    /* 手动修改：UUID 和所有 MAC 改为固定值（模拟用户自定义） */
    for (i = 0; i < count; ++i)
    {
        if (items[i]->type == FPR_TYPE_UUID)
            strcpy(items[i]->newval, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
        else if (items[i]->type == FPR_TYPE_MAC)
            strcpy(items[i]->newval, "52:54:99:88:77:66");
    }

    printf("== 应用自定义值 ==\n");
    rc = fpr_apply_items(pass, vm, items, count, 1, 0, mylog, NULL);
    fpr_free_items(items, count);
    printf("== rc=%d ==\n", rc);
    return rc;
}
