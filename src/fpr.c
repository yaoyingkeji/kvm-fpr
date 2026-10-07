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
 * fpr.c - 后端逻辑：通过 virsh/libvirt 生成全新硬件指纹
 */

#include "fpr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>

/*---------------------------------------------------------------------------*/

typedef struct _run_result_t
{
    int status;   /* pclose 返回值（含 WEXITSTATUS 语义） */
    char *out;    /* 命令输出（malloc，调用方释放） */
} RunResult;

/*---------------------------------------------------------------------------*/

static void i_run_free(RunResult *r)
{
    if (r->out != NULL)
        free(r->out);
    r->out = NULL;
    r->status = 0;
}

/*---------------------------------------------------------------------------*/

/* 去掉输出中的干扰行（如 Deepin sudo 的"验证成功"） */
static void i_strip_noise(char *s)
{
    char *src = s;
    char *dst = s;
    char *line;
    while (*src != '\0')
    {
        line = src;
        while (*src != '\0' && *src != '\n')
            src++;
        {
            size_t len = (size_t)(src - line);
            /* "验证成功" 为 4 个 UTF-8 中文字符，共 12 字节 */
            int noise = (len == 12 && strncmp(line, "\xe9\xaa\x8c\xe8\xaf\x81\xe6\x88\x90\xe5\x8a\x9f", 12) == 0);
            int sudo_prompt = (len >= 7 && strncmp(line, "[sudo] ", 7) == 0);
            if (!noise && !sudo_prompt)
            {
                memmove(dst, line, len);
                dst += len;
                if (*src == '\n')
                    *dst++ = '\n';
            }
        }
        if (*src == '\n')
            src++;
    }
    *dst = '\0';
}

/*---------------------------------------------------------------------------*/

/* 执行命令，捕获 stdout+stderr；如需 sudo 则自动注入密码 */
static RunResult i_run_sudo(const char *pass, const char *cmd)
{
    RunResult r;
    char *cmdline;
    FILE *f;
    char chunk[4096];
    size_t cap, len, n;

    r.out = NULL;
    r.status = 0;

    if (pass == NULL || pass[0] == '\0' || geteuid() == 0)
    {
        cmdline = (char *)malloc(strlen(cmd) + 16);
        snprintf(cmdline, strlen(cmd) + 16, "LC_ALL=C %s", cmd);
    }
    else
    {
        /* 转义单引号：' -> '\'' */
        size_t plen = strlen(pass);
        size_t need = plen * 4 + strlen(cmd) + 80;
        size_t j = 0;
        size_t i;
        cmdline = (char *)malloc(need);
        memcpy(cmdline, "echo '", 6);
        j = 6;
        for (i = 0; i < plen; ++i)
        {
            if (pass[i] == '\'')
            {
                cmdline[j++] = '\'';
                cmdline[j++] = '\\';
                cmdline[j++] = '\'';
                cmdline[j++] = '\'';
            }
            else
            {
                cmdline[j++] = pass[i];
            }
        }
        cmdline[j++] = '\'';
        cmdline[j++] = ' ';
        cmdline[j++] = '|';
        cmdline[j++] = ' ';
        cmdline[j++] = 's';
        cmdline[j++] = 'u';
        cmdline[j++] = 'd';
        cmdline[j++] = 'o';
        cmdline[j++] = ' ';
        cmdline[j++] = '-';
        cmdline[j++] = 'S';
        cmdline[j++] = ' ';
        cmdline[j++] = '-';
        cmdline[j++] = 'p';
        cmdline[j++] = ' ';
        cmdline[j++] = '\'';
        cmdline[j++] = '\'';
        cmdline[j++] = ' ';
        snprintf(cmdline + j, need - j, "LC_ALL=C %s 2>&1", cmd);
    }

    f = popen(cmdline, "r");
    if (f == NULL)
    {
        r.status = -1;
        r.out = strdup("(无法执行命令)");
        free(cmdline);
        return r;
    }

    cap = 8192;
    len = 0;
    r.out = (char *)malloc(cap);
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        if (len + n + 1 > cap)
        {
            while (len + n + 1 > cap)
                cap *= 2;
            r.out = (char *)realloc(r.out, cap);
        }
        memcpy(r.out + len, chunk, n);
        len += n;
    }
    r.out[len] = '\0';
    r.status = pclose(f);
    free(cmdline);
    i_strip_noise(r.out);
    return r;
}

/*---------------------------------------------------------------------------*/

/* 去掉输出末尾的空白字符 */
static void i_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/*---------------------------------------------------------------------------*/

static int i_run_ok(const RunResult *r)
{
    if (r->status < 0)
        return 0;
    if (WIFEXITED(r->status) && WEXITSTATUS(r->status) == 0)
        return 1;
    return 0;
}

/*---------------------------------------------------------------------------*/

int fpr_list_vms(const char *sudo_pass, char ***names, int *count, FprLogFn log, void *ctx)
{
    RunResult r;
    char *p, *save;
    int n = 0;
    int cap = 8;

    (void)log;
    (void)ctx;
    *names = NULL;
    *count = 0;

    r = i_run_sudo(sudo_pass, "virsh list --all --name");
    if (!i_run_ok(&r))
    {
        if (r.out != NULL)
            i_trim(r.out);
        if (log != NULL)
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "无法获取虚拟机列表：%s", r.out != NULL ? r.out : "virsh 不可用");
            log(ctx, msg);
        }
        i_run_free(&r);
        return -1;
    }

    *names = (char **)malloc(sizeof(char *) * cap);
    p = strtok_r(r.out, "\n", &save);
    while (p != NULL)
    {
        i_trim(p);
        if (p[0] != '\0')
        {
            if (n >= cap)
            {
                cap *= 2;
                *names = (char **)realloc(*names, sizeof(char *) * cap);
            }
            (*names)[n++] = strdup(p);
        }
        p = strtok_r(NULL, "\n", &save);
    }
    *count = n;
    i_run_free(&r);
    return 0;
}

/*---------------------------------------------------------------------------*/

void fpr_free_names(char **names, int count)
{
    int i;
    if (names == NULL)
        return;
    for (i = 0; i < count; ++i)
        free(names[i]);
    free(names);
}

/* 日志回调辅助（前向声明，定义见后） */
static void i_logf(FprLogFn log, void *ctx, const char *fmt, ...);

/*---------------------------------------------------------------------------*/

/* 随机数：优先 /dev/urandom，失败则回退 rand() */
static void i_rand_bytes(unsigned char *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f != NULL)
    {
        if (fread(buf, 1, n, f) == n)
        {
            fclose(f);
            return;
        }
        fclose(f);
    }
    {
        static int seeded = 0;
        size_t i;
        if (!seeded)
        {
            srand((unsigned int)(time(NULL) ^ getpid()));
            seeded = 1;
        }
        for (i = 0; i < n; ++i)
            buf[i] = (unsigned char)(rand() & 0xFF);
    }
}

/* 生成全新 UUID（8-4-4-4-12，RFC4122 v4） */
static void i_gen_uuid(char *out, size_t size)
{
    unsigned char b[16];
    i_rand_bytes(b, sizeof(b));
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    snprintf(out, size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

/* 生成全新 MAC（保留 KVM 厂商前缀 52:54） */
static void i_gen_mac(char *out, size_t size)
{
    unsigned char b[4];
    i_rand_bytes(b, sizeof(b));
    snprintf(out, size, "52:54:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3]);
}

/* 生成全新序列号（n 位随机十六进制） */
static void i_gen_serial(char *out, size_t size, int n)
{
    unsigned char *b;
    int i;
    if (n <= 0 || n > 64)
        n = 16;
    b = (unsigned char *)malloc((size_t)n);
    i_rand_bytes(b, (size_t)n);
    for (i = 0; i < n; ++i)
        snprintf(out + i * 2, size - (size_t)(i * 2), "%02x", b[i]);
    free(b);
}

/*---------------------------------------------------------------------------*/

/* 在 xml 中查找第 nth 个 open..close 之间的内容（malloc，调用方释放） */
static char *i_between(const char *xml, const char *open, const char *close, int nth)
{
    const char *p = xml;
    int k = 0;
    while (p != NULL && (p = strstr(p, open)) != NULL)
    {
        if (k == nth)
        {
            const char *q = strstr(p + strlen(open), close);
            if (q != NULL)
            {
                size_t n = (size_t)(q - (p + strlen(open)));
                char *s = (char *)malloc(n + 1);
                memcpy(s, p + strlen(open), n);
                s[n] = '\0';
                return s;
            }
            return NULL;
        }
        k++;
        p += strlen(open);
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/

void fpr_free_items(FprItem **items, int count)
{
    int i;
    if (items == NULL)
        return;
    for (i = 0; i < count; ++i)
    {
        if (items[i] != NULL)
        {
            if (items[i]->name != NULL)
                free(items[i]->name);
            if (items[i]->current != NULL)
                free(items[i]->current);
            if (items[i]->newval != NULL)
                free(items[i]->newval);
            free(items[i]);
        }
    }
    free(items);
}

/*---------------------------------------------------------------------------*/

static void i_add_item(FprItem ***items, int *count, int *cap,
                       FprType type, const char *name,
                       const char *current, const char *newval)
{
    FprItem *elem;
    if (*count >= *cap)
    {
        *cap = (*cap == 0) ? 8 : *cap * 2;
        *items = (FprItem **)realloc(*items, sizeof(FprItem *) * (size_t)*cap);
    }
    elem = (FprItem *)malloc(sizeof(FprItem));
    elem->type = type;
    elem->name = strdup(name);
    elem->current = strdup(current != NULL ? current : "");
    elem->newval = strdup(newval != NULL ? newval : "");
    (*items)[*count] = elem;
    *count += 1;
}

/*---------------------------------------------------------------------------*/

void fpr_regen_values(FprItem **items, int count)
{
    int i;
    char tmp[64];
    if (items == NULL)
        return;
    for (i = 0; i < count; ++i)
    {
        if (items[i] == NULL)
            continue;
        switch (items[i]->type)
        {
        case FPR_TYPE_UUID:
        case FPR_TYPE_SYSTEM_UUID:
        case FPR_TYPE_PRODUCT_UUID:
            i_gen_uuid(tmp, sizeof(tmp));
            break;
        case FPR_TYPE_MAC:
            i_gen_mac(tmp, sizeof(tmp));
            break;
        case FPR_TYPE_SERIAL:
        case FPR_TYPE_PRODUCT_SERIAL:
            i_gen_serial(tmp, sizeof(tmp), 16);
            break;
        default:
            continue;
        }
        free(items[i]->newval);
        items[i]->newval = strdup(tmp);
    }
}

/*---------------------------------------------------------------------------*/

int fpr_get_items(const char *sudo_pass, const char *vm,
                  FprItem ***items, int *count,
                  FprLogFn log, void *ctx)
{
    RunResult r;
    char buf[1024];
    char tmp[64];
    FprItem **arr = NULL;
    int n = 0;
    int cap = 0;
    int i;

    *items = NULL;
    *count = 0;

    snprintf(buf, sizeof(buf), "virsh dumpxml %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r) || r.out == NULL)
    {
        i_logf(log, ctx, "[错误] 无法获取虚拟机 %s 的配置：%s", vm, r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }

    /* 辅助：读取 sysinfo 段内 entry（存在→值，缺失→"无"） */
    /* 域 UUID */
    {
        char *cur = i_between(r.out, "<uuid>", "</uuid>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_uuid(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_UUID, "域 UUID", cur, tmp);
        }
        else
        {
            i_gen_uuid(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_UUID, "域 UUID", "无", tmp);
        }
        free(cur);
    }

    /* 固定 SMBIOS system 段（品牌信息） */
    {
        char *block = i_between(r.out, "<system>", "</system>", 0);
        char *cur;
        cur = (block != NULL) ? i_between(block, "<entry name='manufacturer'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_MANUFACTURER, "品牌-制造商", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_MANUFACTURER, "品牌-制造商", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='product'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_PRODUCT, "品牌-产品型号", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_PRODUCT, "品牌-产品型号", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='version'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_VERSION, "品牌-版本", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_VERSION, "品牌-版本", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='serial'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_SERIAL, "品牌-系统序列号", cur, tmp);
        }
        else
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_SERIAL, "品牌-系统序列号", "无", tmp);
        }
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='sku'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_SKU, "品牌-SKU", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_SKU, "品牌-SKU", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='family'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_FAMILY, "品牌-产品系列", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYS_FAMILY, "品牌-产品系列", "无", "");
        free(cur);
        free(block);
    }

    /* 固定 主板（baseBoard） */
    {
        char *block = i_between(r.out, "<baseBoard>", "</baseBoard>", 0);
        char *cur;
        cur = (block != NULL) ? i_between(block, "<entry name='manufacturer'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_MANUFACTURER, "主板-制造商", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_MANUFACTURER, "主板-制造商", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='product'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_PRODUCT, "主板-产品型号", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_PRODUCT, "主板-产品型号", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='serial'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_SERIAL, "主板-序列号", cur, tmp);
        }
        else
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_BOARD_SERIAL, "主板-序列号", "无", tmp);
        }
        free(cur);
        free(block);
    }

    /* 固定 机箱（chassis） */
    {
        char *block = i_between(r.out, "<chassis>", "</chassis>", 0);
        char *cur;
        cur = (block != NULL) ? i_between(block, "<entry name='manufacturer'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
            i_add_item(&arr, &n, &cap, FPR_TYPE_CHASSIS_MANUFACTURER, "机箱-制造商", cur, cur);
        else
            i_add_item(&arr, &n, &cap, FPR_TYPE_CHASSIS_MANUFACTURER, "机箱-制造商", "无", "");
        free(cur);
        cur = (block != NULL) ? i_between(block, "<entry name='serial'>", "</entry>", 0) : NULL;
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_CHASSIS_SERIAL, "机箱-序列号", cur, tmp);
        }
        else
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_CHASSIS_SERIAL, "机箱-序列号", "无", tmp);
        }
        free(cur);
        free(block);
    }

    /* 动态：各网卡 MAC（缺失显示"无"，可手动填写） */
    for (i = 0;; ++i)
    {
        char *cur = i_between(r.out, "<mac address='", "'/>", i);
        if (cur == NULL)
            break;
        {
            char nm[64];
            snprintf(nm, sizeof(nm), "网卡 %d MAC", i + 1);
            if (cur[0] != '\0')
            {
                i_gen_mac(tmp, sizeof(tmp));
                i_add_item(&arr, &n, &cap, FPR_TYPE_MAC, nm, cur, tmp);
            }
            else
            {
                i_gen_mac(tmp, sizeof(tmp));
                i_add_item(&arr, &n, &cap, FPR_TYPE_MAC, nm, "无", tmp);
            }
        }
        free(cur);
    }

    /* 动态：各磁盘序列号（第 i 个 <disk type=...> 元素，缺失显示"无"） */
    for (i = 0;; ++i)
    {
        const char *p = r.out;
        int k = 0;
        char *block = NULL;
        char *cur;
        char nm[64];
        while ((p = strstr(p, "<disk type")) != NULL)
        {
            if (k == i)
            {
                const char *q = strstr(p, "</disk>");
                if (q != NULL)
                {
                    size_t len = (size_t)(q - p + 7);
                    block = (char *)malloc(len + 1);
                    memcpy(block, p, len);
                    block[len] = '\0';
                }
                break;
            }
            k++;
            p += 10;
        }
        if (block == NULL)
            break;
        snprintf(nm, sizeof(nm), "磁盘 %d 序列号", i + 1);
        cur = i_between(block, "<serial>", "</serial>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_DISK_SERIAL, nm, cur, tmp);
        }
        else
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_DISK_SERIAL, nm, "无", tmp);
        }
        free(cur);
        free(block);
    }

    i_run_free(&r);
    *items = arr;
    *count = n;
    return 0;
}

/*---------------------------------------------------------------------------*/

/* 内嵌的 python 指纹生成脚本（生成新 UUID/MAC/SMBIOS） */
static const char *kCloneScript =
    "#!/usr/bin/env python3\n"
    "import sys, re\n"
    "orig = sys.argv[1]\n"
    "dst = sys.argv[2]\n"
    "mapfile = sys.argv[3]\n"
    "xml = open(orig, encoding='utf-8').read()\n"
    "rules = []\n"
    "for line in open(mapfile, encoding='utf-8'):\n"
    "    line = line.rstrip('\\n')\n"
    "    if '\\t' in line:\n"
    "        t, v = line.split('\\t', 1)\n"
    "        rules.append((t, v))\n"
    "def ensure_entry(xml, seg, name, v):\n"
    "    m = re.search('<' + seg + '>.*?</' + seg + '>', xml, re.S)\n"
    "    if m:\n"
    "        block = m.group(0)\n"
    "        e = re.search(\"<entry name='%s'>[^<]*</entry>\" % name, block)\n"
    "        if e:\n"
    "            block = block[:e.start()] + \"<entry name='%s'>%s</entry>\" % (name, v) + block[e.end():]\n"
    "        else:\n"
    "            block = block.replace('</' + seg + '>', \"<entry name='%s'>%s</entry>\" % (name, v) + '</' + seg + '>', 1)\n"
    "        return xml[:m.start()] + block + xml[m.end():]\n"
    "    newseg = '<' + seg + '><entry name=\\'' + name + '\\'>' + v + '</entry></' + seg + '>'\n"
    "    sm = re.search('<sysinfo[^>]*>.*?</sysinfo>', xml, re.S)\n"
    "    if sm:\n"
    "        return xml[:sm.start()] + sm.group(0)[:-10] + newseg + '</sysinfo>' + xml[sm.end():]\n"
    "    sysinfo = \"<sysinfo type='smbios'>\" + newseg + '</sysinfo>'\n"
    "    if '<clock' in xml:\n"
    "        return xml.replace('<clock', sysinfo + '<clock', 1)\n"
    "    return xml.replace('</features>', '</features>' + sysinfo, 1)\n"
    "# 1) 域 UUID\n"
    "for t, v in rules:\n"
    "    if t == 'uuid':\n"
    "        if '<uuid>' in xml:\n"
    "            xml = re.sub(r'<uuid>[^<]*</uuid>', lambda m: '<uuid>' + v + '</uuid>', xml, count=1)\n"
    "        else:\n"
    "            xml = xml.replace('<memory', '<uuid>' + v + '</uuid><memory', 1)\n"
    "# 2) 网卡 MAC / 磁盘序列号\n"
    "macs = [v for t, v in rules if t.startswith('mac')]\n"
    "if macs:\n"
    "    it = iter(macs)\n"
    "    xml = re.sub(r\"<mac address='[^']*'/>\", lambda m: \"<mac address='%s'/>\" % next(it), xml)\n"
    "dserials = [v for t, v in rules if t.startswith('diskserial')]\n"
    "if dserials:\n"
    "    it = iter(dserials)\n"
    "    def disk_repl(m):\n"
    "        block = m.group(0)\n"
    "        v = next(it)\n"
    "        e = re.search('<serial>[^<]*</serial>', block)\n"
    "        if e:\n"
    "            return block[:e.start()] + '<serial>' + v + '</serial>' + block[e.end():]\n"
    "        return block.replace('</disk>', '<serial>' + v + '</serial></disk>', 1)\n"
    "    xml = re.sub(r'<disk type=.*?</disk>', disk_repl, xml, flags=re.S)\n"
    "# 3) SMBIOS system / baseBoard / chassis（缺失自动插入）\n"
    "for t, v in rules:\n"
    "    if t == 'serial':\n"
    "        xml = ensure_entry(xml, 'system', 'serial', v)\n"
    "    elif t.startswith('sys'):\n"
    "        xml = ensure_entry(xml, 'system', t[3:], v)\n"
    "    elif t.startswith('board'):\n"
    "        xml = ensure_entry(xml, 'baseBoard', t[5:], v)\n"
    "    elif t.startswith('chassis'):\n"
    "        xml = ensure_entry(xml, 'chassis', t[7:], v)\n"
    "# 4) 防虚拟机检测设置\n"
    "for t, v in rules:\n"
    "    if t == 'avoid_kvmhidden':\n"
    "        if '<kvm>' not in xml:\n"
    "            xml = xml.replace('</features>', \"<kvm><hidden state='on'/></kvm>\" + '</features>', 1)\n"
    "        elif '<hidden' not in xml:\n"
    "            xml = xml.replace('</kvm>', \"<hidden state='on'/></kvm>\", 1)\n"
    "    elif t == 'avoid_hypervisor':\n"
    "        if \"<feature policy='disable' name='hypervisor'\" not in xml:\n"
    "            xml = xml.replace('</cpu>', \"<feature policy='disable' name='hypervisor'/>\" + '</cpu>', 1)\n"
    "    elif t == 'avoid_vmport':\n"
    "        if '<vmport' not in xml:\n"
    "            xml = xml.replace('</features>', \"<vmport state='off'/>\" + '</features>', 1)\n"
    "    elif t == 'avoid_hypervvendor':\n"
    "        if '<hyperv' in xml and 'vendor_id' in xml:\n"
    "            xml = re.sub(r\"<vendor_id state='on' value='[^']*'/>\", \"<vendor_id state='on' value='%s'/>\" % v, xml, count=1)\n"
    "        elif '<hyperv' in xml:\n"
    "            xml = xml.replace('</hyperv>', \"<vendor_id state='on' value='%s'/>\" % v + '</hyperv>', 1)\n"
    "        else:\n"
    "            xml = xml.replace('</features>', \"<hyperv mode='custom'><vendor_id state='on' value='%s'/></hyperv>\" % v + '</features>', 1)\n"
    "open(dst, 'w', encoding='utf-8').write(xml)\n"
    "print('OK')\n";

/*---------------------------------------------------------------------------*/

/* 从 domblklist 输出中提取第一个真实磁盘路径 */
static char *i_first_disk(const char *domblk)
{
    char *copy = strdup(domblk != NULL ? domblk : "");
    char *save = NULL;
    char *tok;
    char *result = NULL;

    tok = strtok_r(copy, "\n", &save);
    while (tok != NULL)
    {
        char *space;
        i_trim(tok);
        if (tok[0] == '\0' || strstr(tok, "Target") != NULL || strstr(tok, "---") != NULL)
        {
            tok = strtok_r(NULL, "\n", &save);
            continue;
        }
        space = strchr(tok, ' ');
        if (space != NULL)
        {
            char *src = space;
            while (*src == ' ')
                src++;
            if (src[0] == '/' && strstr(src, "/") != NULL)
            {
                result = strdup(src);
                break;
            }
        }
        tok = strtok_r(NULL, "\n", &save);
    }
    free(copy);
    return result;
}

/*---------------------------------------------------------------------------*/

/* 将文件复制一份（用于备份） */
static int i_copy_file(const char *src, const char *dst)
{
    FILE *fin, *fout;
    char buf[8192];
    size_t n;

    fin = fopen(src, "rb");
    if (fin == NULL)
        return -1;
    fout = fopen(dst, "wb");
    if (fout == NULL)
    {
        fclose(fin);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
        fwrite(buf, 1, n, fout);
    fclose(fin);
    fclose(fout);
    return 0;
}

/*---------------------------------------------------------------------------*/

/* 把内嵌的 python 指纹生成脚本写到临时文件 */
static int i_write_script(const char *path)
{
    FILE *f = fopen(path, "w");
    size_t len;
    if (f == NULL)
        return -1;
    len = strlen(kCloneScript);
    if (fwrite(kCloneScript, 1, len, f) != len)
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/*---------------------------------------------------------------------------*/

static void i_logf(FprLogFn log, void *ctx, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    if (log == NULL)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log(ctx, buf);
}

/*---------------------------------------------------------------------------*/

int fpr_apply_items_ext(const char *sudo_pass, const char *vm,
                        FprItem **items, int count,
                        unsigned int avoid_flags, const char *hyperv_vendor,
                        int force_off, int reset_guest,
                        FprLogFn log, void *ctx)
{
    char buf[2048];
    char orig[512], newxml[512], script[512], mapfile[512];
    char backup[1024], backup_dir[512];
    char ts[64];
    time_t now;
    struct tm *lt;
    RunResult r;
    int autostart = 0;
    int has_nvram = 0;
    int rc = -1;
    int undef_ok = 0;
    int i;

    now = time(NULL);
    lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", lt);

    snprintf(script, sizeof(script), "/tmp/kvmfpr_apply.py");
    snprintf(mapfile, sizeof(mapfile), "/tmp/kvmfpr_%s_%s.map", vm, ts);

    /* 1. 检查虚拟机状态 */
    snprintf(buf, sizeof(buf), "virsh domstate %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 无法获取虚拟机 %s 的状态：%s", vm, r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }
    if (r.out)
        i_trim(r.out);
    i_logf(log, ctx, ">> 虚拟机 %s 当前状态：%s", vm, r.out ? r.out : "?");

    {
        int is_off = (r.out && strstr(r.out, "shut off") != NULL);
        i_run_free(&r);
        if (!is_off)
        {
            if (force_off)
            {
                i_logf(log, ctx, ">> 强制关闭虚拟机 %s ...", vm);
                snprintf(buf, sizeof(buf), "virsh destroy %s", vm);
                r = i_run_sudo(sudo_pass, buf);
                if (!i_run_ok(&r))
                {
                    i_logf(log, ctx, "[错误] 强制关机失败：%s", r.out ? r.out : "?");
                    i_run_free(&r);
                    return -1;
                }
                i_run_free(&r);
                sleep(2);
                i_logf(log, ctx, ">> 已强制关闭");
            }
            else
            {
                i_logf(log, ctx, ">> 正在优雅关机（最长等待 90 秒）...");
                snprintf(buf, sizeof(buf), "virsh shutdown %s", vm);
                r = i_run_sudo(sudo_pass, buf);
                i_run_free(&r);
                for (i = 0; i < 90; ++i)
                {
                    sleep(1);
                    snprintf(buf, sizeof(buf), "virsh domstate %s", vm);
                    r = i_run_sudo(sudo_pass, buf);
                    if (r.out)
                        i_trim(r.out);
                    if (r.out && strstr(r.out, "shut off") != NULL)
                    {
                        i_run_free(&r);
                        break;
                    }
                    i_run_free(&r);
                }
                if (i >= 90)
                {
                    i_logf(log, ctx, "[错误] 等待关机超时，请勾选“强制关机”重试");
                    return -1;
                }
                i_logf(log, ctx, ">> 虚拟机已关机");
            }
        }
    }

    /* 2. 导出当前 XML 配置 */
    snprintf(orig, sizeof(orig), "/tmp/kvmfpr_%s_%s_orig.xml", vm, ts);
    snprintf(newxml, sizeof(newxml), "/tmp/kvmfpr_%s_%s_new.xml", vm, ts);

    snprintf(buf, sizeof(buf), "virsh dumpxml %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 导出配置失败：%s", r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }
    {
        FILE *f = fopen(orig, "w");
        if (f != NULL)
        {
            fwrite(r.out, 1, strlen(r.out), f);
            fclose(f);
        }
        else
        {
            i_run_free(&r);
            i_logf(log, ctx, "[错误] 无法写入临时文件 %s", orig);
            return -1;
        }
        if (strstr(r.out, "<nvram") != NULL)
            has_nvram = 1;
    }
    i_run_free(&r);
    i_logf(log, ctx, ">> 已导出当前配置：%s", orig);

    /* 3. 备份原始配置到用户主目录 */
    snprintf(backup_dir, sizeof(backup_dir), "%s/kvm-fpr-backups",
             getenv("HOME") != NULL ? getenv("HOME") : "/tmp");
    mkdir(backup_dir, 0700);
    snprintf(backup, sizeof(backup), "%s/%s_%s_orig.xml", backup_dir, vm, ts);
    if (i_copy_file(orig, backup) == 0)
        i_logf(log, ctx, ">> 原始配置已备份：%s", backup);

    /* 4. 记录开机自启状态 */
    snprintf(buf, sizeof(buf), "virsh dominfo %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (r.out && strstr(r.out, "Autostart: enable") != NULL)
        autostart = 1;
    i_run_free(&r);
    if (autostart)
        i_logf(log, ctx, ">> 检测到开机自启已开启，刷新后将恢复");

    /* 5. 生成替换映射并运行应用脚本 */
    if (i_write_script(script) != 0)
    {
        i_logf(log, ctx, "[错误] 无法写入应用脚本");
        return -1;
    }
    {
        FILE *f = fopen(mapfile, "w");
        int mac_idx = 0;
        int disk_idx = 0;
        if (f == NULL)
        {
            i_logf(log, ctx, "[错误] 无法写入映射文件 %s", mapfile);
            return -1;
        }
        for (i = 0; i < count; ++i)
        {
            if (items[i]->newval == NULL || items[i]->newval[0] == '\0')
                continue;
            switch (items[i]->type)
            {
            case FPR_TYPE_UUID:
                fprintf(f, "uuid\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_MAC:
                fprintf(f, "mac%d\t%s\n", mac_idx++, items[i]->newval);
                break;
            case FPR_TYPE_SERIAL:
                fprintf(f, "serial\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYSTEM_UUID:
                fprintf(f, "systemuuid\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_PRODUCT_UUID:
                fprintf(f, "productuuid\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_PRODUCT_SERIAL:
                fprintf(f, "productserial\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYS_MANUFACTURER:
                fprintf(f, "sysmanufacturer\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYS_PRODUCT:
                fprintf(f, "sysproduct\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYS_VERSION:
                fprintf(f, "sysversion\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYS_SKU:
                fprintf(f, "syssku\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_SYS_FAMILY:
                fprintf(f, "sysfamily\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_BOARD_MANUFACTURER:
                fprintf(f, "boardmanufacturer\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_BOARD_PRODUCT:
                fprintf(f, "boardproduct\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_BOARD_SERIAL:
                fprintf(f, "boardserial\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_CHASSIS_MANUFACTURER:
                fprintf(f, "chassismanufacturer\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_CHASSIS_SERIAL:
                fprintf(f, "chassisserial\t%s\n", items[i]->newval);
                break;
            case FPR_TYPE_DISK_SERIAL:
                fprintf(f, "diskserial%d\t%s\n", disk_idx++, items[i]->newval);
                break;
            default:
                break;
            }
        }
        /* 防虚拟机检测设置 */
        if (avoid_flags & FPR_AVOID_KVM_HIDDEN)
            fprintf(f, "avoid_kvmhidden\t1\n");
        if (avoid_flags & FPR_AVOID_HYPERVISOR)
            fprintf(f, "avoid_hypervisor\t1\n");
        if (avoid_flags & FPR_AVOID_VMPORT)
            fprintf(f, "avoid_vmport\t1\n");
        if ((avoid_flags & FPR_AVOID_HYPERV_VENDOR) && hyperv_vendor != NULL && hyperv_vendor[0] != '\0')
            fprintf(f, "avoid_hypervvendor\t%s\n", hyperv_vendor);
        fclose(f);
    }
    snprintf(buf, sizeof(buf), "python3 %s %s %s %s", script, orig, newxml, mapfile);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 应用脚本执行失败：%s", r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }
    i_run_free(&r);

    /* 打印新旧指纹对比 */
    i_logf(log, ctx, ">> 新指纹清单：");
    for (i = 0; i < count; ++i)
    {
        const char *nv = items[i]->newval != NULL ? items[i]->newval : "";
        i_logf(log, ctx, "    %s：%s", items[i]->name, nv);
    }
    i_logf(log, ctx, ">> 新配置已生成：%s", newxml);

    /* 6. 解除旧定义并加载新定义 */
    {
        const char *attempts[3];
        attempts[0] = "virsh undefine %s";
        attempts[1] = "virsh undefine %s --managed-save";
        attempts[2] = "virsh undefine %s --nvram --managed-save";
        for (i = 0; i < 3 && !undef_ok; ++i)
        {
            snprintf(buf, sizeof(buf), attempts[i], vm);
            r = i_run_sudo(sudo_pass, buf);
            if (i_run_ok(&r))
            {
                undef_ok = 1;
                i_logf(log, ctx, ">> 已移除旧定义（方式 %d）", i + 1);
            }
            i_run_free(&r);
        }
    }
    if (!undef_ok)
    {
        i_logf(log, ctx, "[错误] 移除旧定义失败，请检查虚拟机状态。备份位于：%s", backup);
        return -1;
    }

    snprintf(buf, sizeof(buf), "virsh define %s", newxml);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 加载新配置失败：%s", r.out ? r.out : "?");
        i_logf(log, ctx, "[恢复] 可执行：sudo virsh define %s", backup);
        i_run_free(&r);
        return -1;
    }
    i_run_free(&r);
    i_logf(log, ctx, ">> 新配置已生效！");

    /* 7. 恢复开机自启 */
    if (autostart)
    {
        snprintf(buf, sizeof(buf), "virsh autostart %s", vm);
        r = i_run_sudo(sudo_pass, buf);
        if (i_run_ok(&r))
            i_logf(log, ctx, ">> 已恢复开机自启");
        i_run_free(&r);
    }

    /* 8. 可选：重置 guest 内部 machine-id */
    if (reset_guest)
    {
        RunResult rc2;
        char *disk = NULL;
        rc2 = i_run_sudo(sudo_pass, "command -v virt-customize");
        if (i_run_ok(&rc2) && rc2.out != NULL && rc2.out[0] != '\0')
        {
            i_run_free(&rc2);
            snprintf(buf, sizeof(buf), "virsh domblklist %s", vm);
            rc2 = i_run_sudo(sudo_pass, buf);
            if (i_run_ok(&rc2))
                disk = i_first_disk(rc2.out);
            i_run_free(&rc2);
            if (disk != NULL)
            {
                i_logf(log, ctx, ">> 正在重置 guest 内部 machine-id（磁盘：%s，可能需要数分钟）...", disk);
                snprintf(buf, sizeof(buf),
                         "virt-customize -a %s --run-command 'rm -f /etc/machine-id /var/lib/dbus/machine-id; systemd-machine-id-setup'",
                         disk);
                rc2 = i_run_sudo(sudo_pass, buf);
                if (i_run_ok(&rc2))
                    i_logf(log, ctx, ">> guest machine-id 已重置（全新机器码）");
                else
                    i_logf(log, ctx, "[警告] guest machine-id 重置失败：%s", rc2.out ? rc2.out : "?");
                i_run_free(&rc2);
                free(disk);
            }
            else
            {
                i_logf(log, ctx, "[警告] 未找到可用的磁盘设备，跳过 guest machine-id 重置");
            }
        }
        else
        {
            i_run_free(&rc2);
            i_logf(log, ctx, "[提示] 未安装 virt-customize，跳过 guest 内部 machine-id 重置");
            i_logf(log, ctx, "[提示] 手动重置：sudo virt-customize -a <磁盘> --run-command 'rm -f /etc/machine-id /var/lib/dbus/machine-id; systemd-machine-id-setup'");
        }
    }

    i_logf(log, ctx, "======================================================");
    i_logf(log, ctx, "[完成] 虚拟机 %s 硬件指纹已刷新：", vm);
    i_logf(log, ctx, "  - 备份文件：%s", backup);
    if (has_nvram)
        i_logf(log, ctx, "  - 注意：该虚拟机含 UEFI nvram，已随之重建");
    rc = 0;
    return rc;
}

/*---------------------------------------------------------------------------*/

int fpr_apply_items(const char *sudo_pass, const char *vm,
                    FprItem **items, int count,
                    int force_off, int reset_guest,
                    FprLogFn log, void *ctx)
{
    return fpr_apply_items_ext(sudo_pass, vm, items, count, 0, NULL,
                               force_off, reset_guest, log, ctx);
}

/*---------------------------------------------------------------------------*/

int fpr_refresh(const char *sudo_pass, const char *vm,
                int force_off, int reset_guest,
                FprLogFn log, void *ctx)
{
    FprItem **items = NULL;
    int count = 0;
    int rc;

    if (fpr_get_items(sudo_pass, vm, &items, &count, log, ctx) != 0)
        return -1;
    rc = fpr_apply_items(sudo_pass, vm, items, count, force_off, reset_guest, log, ctx);
    fpr_free_items(items, count);
    return rc;
}

/*---------------------------------------------------------------------------*/
/* 品牌伪装模板 */

/* 内置模板 */
static const FprTemplate kBuiltinTemplates[] = {
    { "联想 Lenovo",
      "LENOVO", "ThinkPad X1 Carbon", "ThinkPad X1 Carbon 7th", "PF1R2XYZ", "LENOVO_MT_20QD", "ThinkPad",
      "LENOVO", "20QDS01T00", "PF0K4B3A",
      "LENOVO", "X1CARBON7" },
    { "戴尔 Dell",
      "Dell Inc.", "XPS 15 7590", "1.11.1", "ABC1234567", "XPS", "XPS",
      "Dell Inc.", "0T8J3Y", "CNXPS15002",
      "Dell Inc.", "XPS7590" },
    { "惠普 HP",
      "HP", "HP EliteBook 840 G6", "Type1ProductConfigId", "CND8C01234", "SKU8C01234", "103C_5336AN",
      "HP", "8079", "PHTRC01234",
      "HP", "ELITE840G6" },
    { "华硕 ASUS",
      "ASUS", "ROG Zephyrus G14", "1.0.0", "M0ABCDEFGH", "GA401IV", "ROG",
      "ASUSTeK COMPUTER INC.", "GA401IV", "N0ABCDEFGH",
      "ASUS", "GA401IV" },
    { "宏碁 Acer",
      "Acer", "Predator Helios 300", "V1.10", "NHQABCDEF", "PH315-52", "Predator",
      "Acer", "FH5BV", "NHQABCDEF",
      "Acer", "HELIOS300" },
    { "小米 Xiaomi",
      "Xiaomi", "Redmi Book Pro 15", "RMAAP0100", "XMBP15ABC", "TM2110", "Redmi Book",
      "Xiaomi", "RMAAP0100", "XMBP15ABC",
      "Xiaomi", "REDMIBOOK15" }
};

/*---------------------------------------------------------------------------*/

void fpr_free_templates(FprTemplate **tpls, int count)
{
    int i;
    if (tpls == NULL)
        return;
    for (i = 0; i < count; ++i)
    {
        if (tpls[i] == NULL)
            continue;
        free(tpls[i]->name);
        free(tpls[i]->sys_manufacturer);
        free(tpls[i]->sys_product);
        free(tpls[i]->sys_version);
        free(tpls[i]->sys_serial);
        free(tpls[i]->sys_sku);
        free(tpls[i]->sys_family);
        free(tpls[i]->board_manufacturer);
        free(tpls[i]->board_product);
        free(tpls[i]->board_serial);
        free(tpls[i]->chassis_manufacturer);
        free(tpls[i]->chassis_serial);
        free(tpls[i]);
    }
    free(tpls);
}

/*---------------------------------------------------------------------------*/

static void i_tpl_free_one(FprTemplate *t); /* 前向声明 */

static FprTemplate *i_tpl_copy(const FprTemplate *t)
{
    FprTemplate *n = (FprTemplate *)calloc(1, sizeof(FprTemplate));
    if (n == NULL)
        return NULL;
    n->name = strdup(t->name != NULL ? t->name : "");
    n->sys_manufacturer = strdup(t->sys_manufacturer != NULL ? t->sys_manufacturer : "");
    n->sys_product = strdup(t->sys_product != NULL ? t->sys_product : "");
    n->sys_version = strdup(t->sys_version != NULL ? t->sys_version : "");
    n->sys_serial = strdup(t->sys_serial != NULL ? t->sys_serial : "");
    n->sys_sku = strdup(t->sys_sku != NULL ? t->sys_sku : "");
    n->sys_family = strdup(t->sys_family != NULL ? t->sys_family : "");
    n->board_manufacturer = strdup(t->board_manufacturer != NULL ? t->board_manufacturer : "");
    n->board_product = strdup(t->board_product != NULL ? t->board_product : "");
    n->board_serial = strdup(t->board_serial != NULL ? t->board_serial : "");
    n->chassis_manufacturer = strdup(t->chassis_manufacturer != NULL ? t->chassis_manufacturer : "");
    n->chassis_serial = strdup(t->chassis_serial != NULL ? t->chassis_serial : "");
    return n;
}

/*---------------------------------------------------------------------------*/

static char *i_tpl_config_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL)
        home = "/tmp";
    snprintf(buf, size, "%s/.config/kvm-fpr/templates.conf", home);
    return buf;
}

/*---------------------------------------------------------------------------*/

static int i_tpl_read_line(char *line, size_t cap, FILE *f)
{
    size_t n;
    if (fgets(line, (int)cap, f) == NULL)
        return -1;
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        line[--n] = '\0';
    return 0;
}

/*---------------------------------------------------------------------------*/

static FprTemplate *i_tpl_parse_file(void)
{
    char path[512];
    FILE *f;
    FprTemplate *cur = NULL;
    char line[1024];
    char *p;

    i_tpl_config_path(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL)
        return NULL;
    cur = (FprTemplate *)calloc(1, sizeof(FprTemplate));
    while (i_tpl_read_line(line, sizeof(line), f) == 0)
    {
        if (line[0] == '[')
        {
            p = strchr(line, ']');
            if (p != NULL)
            {
                *p = '\0';
                free(cur->name);
                cur->name = strdup(line + 1);
            }
            continue;
        }
        p = strchr(line, '=');
        if (p == NULL)
            continue;
        *p = '\0';
        if (strcmp(line, "sys_manufacturer") == 0) { free(cur->sys_manufacturer); cur->sys_manufacturer = strdup(p + 1); }
        else if (strcmp(line, "sys_product") == 0) { free(cur->sys_product); cur->sys_product = strdup(p + 1); }
        else if (strcmp(line, "sys_version") == 0) { free(cur->sys_version); cur->sys_version = strdup(p + 1); }
        else if (strcmp(line, "sys_serial") == 0) { free(cur->sys_serial); cur->sys_serial = strdup(p + 1); }
        else if (strcmp(line, "sys_sku") == 0) { free(cur->sys_sku); cur->sys_sku = strdup(p + 1); }
        else if (strcmp(line, "sys_family") == 0) { free(cur->sys_family); cur->sys_family = strdup(p + 1); }
        else if (strcmp(line, "board_manufacturer") == 0) { free(cur->board_manufacturer); cur->board_manufacturer = strdup(p + 1); }
        else if (strcmp(line, "board_product") == 0) { free(cur->board_product); cur->board_product = strdup(p + 1); }
        else if (strcmp(line, "board_serial") == 0) { free(cur->board_serial); cur->board_serial = strdup(p + 1); }
        else if (strcmp(line, "chassis_manufacturer") == 0) { free(cur->chassis_manufacturer); cur->chassis_manufacturer = strdup(p + 1); }
        else if (strcmp(line, "chassis_serial") == 0) { free(cur->chassis_serial); cur->chassis_serial = strdup(p + 1); }
    }
    fclose(f);
    return cur;
}

/*---------------------------------------------------------------------------*/

int fpr_list_templates(FprTemplate ***tpls, int *count)
{
    FprTemplate **arr;
    int n = 0;
    size_t i;

    *tpls = NULL;
    *count = 0;
    arr = (FprTemplate **)malloc(sizeof(FprTemplate *) * (sizeof(kBuiltinTemplates) / sizeof(kBuiltinTemplates[0]) + 1));
    for (i = 0; i < sizeof(kBuiltinTemplates) / sizeof(kBuiltinTemplates[0]); ++i)
        arr[n++] = i_tpl_copy(&kBuiltinTemplates[i]);

    {
        FprTemplate *usr = i_tpl_parse_file();
        if (usr != NULL && usr->name != NULL && usr->name[0] != '\0')
            arr[n++] = usr;
        else if (usr != NULL)
            i_tpl_free_one(usr);
    }
    *tpls = arr;
    *count = n;
    return 0;
}

/*---------------------------------------------------------------------------*/

static void i_tpl_free_one(FprTemplate *t)
{
    if (t == NULL)
        return;
    free(t->name);
    free(t->sys_manufacturer);
    free(t->sys_product);
    free(t->sys_version);
    free(t->sys_serial);
    free(t->sys_sku);
    free(t->sys_family);
    free(t->board_manufacturer);
    free(t->board_product);
    free(t->board_serial);
    free(t->chassis_manufacturer);
    free(t->chassis_serial);
    free(t);
}

/*---------------------------------------------------------------------------*/

void fpr_apply_template(FprItem **items, int count, const FprTemplate *tpl)
{
    int i;
    if (items == NULL || tpl == NULL)
        return;
    for (i = 0; i < count && items[i] != NULL; ++i)
    {
        const char *v = NULL;
        switch (items[i]->type)
        {
        case FPR_TYPE_SYS_MANUFACTURER: v = tpl->sys_manufacturer; break;
        case FPR_TYPE_SYS_PRODUCT:      v = tpl->sys_product; break;
        case FPR_TYPE_SYS_VERSION:      v = tpl->sys_version; break;
        case FPR_TYPE_SERIAL:           v = tpl->sys_serial; break;
        case FPR_TYPE_SYS_SKU:          v = tpl->sys_sku; break;
        case FPR_TYPE_SYS_FAMILY:       v = tpl->sys_family; break;
        case FPR_TYPE_BOARD_MANUFACTURER: v = tpl->board_manufacturer; break;
        case FPR_TYPE_BOARD_PRODUCT:    v = tpl->board_product; break;
        case FPR_TYPE_BOARD_SERIAL:     v = tpl->board_serial; break;
        case FPR_TYPE_CHASSIS_MANUFACTURER: v = tpl->chassis_manufacturer; break;
        case FPR_TYPE_CHASSIS_SERIAL:   v = tpl->chassis_serial; break;
        default: break;
        }
        if (v != NULL && v[0] != '\0')
        {
            free(items[i]->newval);
            items[i]->newval = strdup(v);
        }
    }
}

/*---------------------------------------------------------------------------*/

int fpr_save_template(const FprTemplate *tpl)
{
    char path[512];
    char dir[256];
    FILE *f;
    const char *home = getenv("HOME");
    if (home == NULL)
        home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/.config/kvm-fpr", home);
    mkdir(dir, 0700);
    i_tpl_config_path(path, sizeof(path));

    /* 若已存在同名模板，先剔除（简单实现：重写整文件） */
    f = fopen(path, "a");
    if (f == NULL)
        return -1;
    fprintf(f, "\n[%s]\n", tpl->name != NULL ? tpl->name : "未命名模板");
    fprintf(f, "sys_manufacturer=%s\n", tpl->sys_manufacturer != NULL ? tpl->sys_manufacturer : "");
    fprintf(f, "sys_product=%s\n", tpl->sys_product != NULL ? tpl->sys_product : "");
    fprintf(f, "sys_version=%s\n", tpl->sys_version != NULL ? tpl->sys_version : "");
    fprintf(f, "sys_serial=%s\n", tpl->sys_serial != NULL ? tpl->sys_serial : "");
    fprintf(f, "sys_sku=%s\n", tpl->sys_sku != NULL ? tpl->sys_sku : "");
    fprintf(f, "sys_family=%s\n", tpl->sys_family != NULL ? tpl->sys_family : "");
    fprintf(f, "board_manufacturer=%s\n", tpl->board_manufacturer != NULL ? tpl->board_manufacturer : "");
    fprintf(f, "board_product=%s\n", tpl->board_product != NULL ? tpl->board_product : "");
    fprintf(f, "board_serial=%s\n", tpl->board_serial != NULL ? tpl->board_serial : "");
    fprintf(f, "chassis_manufacturer=%s\n", tpl->chassis_manufacturer != NULL ? tpl->chassis_manufacturer : "");
    fprintf(f, "chassis_serial=%s\n", tpl->chassis_serial != NULL ? tpl->chassis_serial : "");
    fclose(f);
    return 0;
}

/*---------------------------------------------------------------------------*/
/* 备份历史与恢复 */

void fpr_free_backups(char **paths, char **times, int count)
{
    int i;
    if (paths == NULL)
        return;
    for (i = 0; i < count; ++i)
    {
        free(paths[i]);
        if (times != NULL)
            free(times[i]);
    }
    free(paths);
    if (times != NULL)
        free(times);
}

/*---------------------------------------------------------------------------*/

int fpr_list_backups(const char *vm, char ***paths, char ***times, int *count)
{
    char dir[512];
    DIR *d;
    struct dirent *ent;
    char **p;
    char **t;
    int n = 0;
    int cap = 8;
    const char *home = getenv("HOME");
    size_t vmlen;

    *paths = NULL;
    *times = NULL;
    *count = 0;
    if (home == NULL)
        home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/kvm-fpr-backups", home);
    d = opendir(dir);
    if (d == NULL)
        return 0;
    vmlen = strlen(vm);
    p = (char **)malloc(sizeof(char *) * (size_t)cap);
    t = (char **)malloc(sizeof(char *) * (size_t)cap);

    while ((ent = readdir(d)) != NULL)
    {
        char *e = ent->d_name;
        if (strncmp(e, vm, vmlen) != 0 || strstr(e, "_orig.xml") == NULL)
            continue;
        if (n >= cap)
        {
            cap *= 2;
            p = (char **)realloc(p, sizeof(char *) * (size_t)cap);
            t = (char **)realloc(t, sizeof(char *) * (size_t)cap);
        }
        p[n] = (char *)malloc(strlen(dir) + strlen(e) + 2);
        sprintf(p[n], "%s/%s", dir, e);
        /* 时间戳：<vm>_<YYYYMMDD_HHMMSS>_orig.xml */
        t[n] = strdup(e + vmlen + 1);
        n += 1;
    }
    closedir(d);
    *paths = p;
    *times = t;
    *count = n;
    return 0;
}

/*---------------------------------------------------------------------------*/

int fpr_restore_backup(const char *sudo_pass, const char *vm,
                       const char *backup_path, FprLogFn log, void *ctx)
{
    RunResult r;
    char buf[2048];

    i_logf(log, ctx, ">> 正在恢复备份：%s", backup_path);

    /* 若当前存在定义则移除 */
    snprintf(buf, sizeof(buf), "virsh list --all --name");
    r = i_run_sudo(sudo_pass, buf);
    if (r.out != NULL && strstr(r.out, vm) != NULL)
    {
        i_run_free(&r);
        snprintf(buf, sizeof(buf), "virsh undefine %s --nvram --managed-save", vm);
        r = i_run_sudo(sudo_pass, buf);
        if (!i_run_ok(&r))
        {
            snprintf(buf, sizeof(buf), "virsh undefine %s", vm);
            i_run_free(&r);
            r = i_run_sudo(sudo_pass, buf);
        }
        i_run_free(&r);
        i_logf(log, ctx, ">> 已移除当前定义");
    }
    else
    {
        i_run_free(&r);
    }

    snprintf(buf, sizeof(buf), "virsh define %s", backup_path);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 恢复失败：%s", r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }
    i_run_free(&r);
    i_logf(log, ctx, "[完成] 已恢复到备份 %s", backup_path);
    return 0;
}

/*---------------------------------------------------------------------------*/

int fpr_delete_backup(const char *backup_path)
{
    if (remove(backup_path) == 0)
        return 0;
    return -1;
}

/*---------------------------------------------------------------------------*/
/* 查看文件（XML 编辑查看） */

int fpr_dumpxml(const char *sudo_pass, const char *vm, char **xml_out,
                FprLogFn log, void *ctx)
{
    RunResult r;
    char buf[2048];

    *xml_out = NULL;
    snprintf(buf, sizeof(buf), "virsh dumpxml %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r) || r.out == NULL)
    {
        i_logf(log, ctx, "[错误] 无法获取虚拟机 %s 的配置：%s", vm, r.out ? r.out : "?");
        i_run_free(&r);
        return -1;
    }
    *xml_out = r.out;
    return 0;
}

/*---------------------------------------------------------------------------*/

int fpr_apply_xml(const char *sudo_pass, const char *vm, const char *xml_text,
                  FprLogFn log, void *ctx)
{
    RunResult r;
    char buf[2048];
    char tmp[512];
    char backup[1024], backup_dir[512];
    char ts[64];
    time_t now;
    struct tm *lt;
    FILE *f;
    int undef_ok = 0;
    int i;

    if (xml_text == NULL || xml_text[0] == '\0')
    {
        i_logf(log, ctx, "[错误] XML 内容为空");
        return -1;
    }

    now = time(NULL);
    lt = localtime(&now);
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", lt);
    snprintf(tmp, sizeof(tmp), "/tmp/kvmfpr_%s_%s_manual.xml", vm, ts);

    /* 1. 备份当前配置 */
    snprintf(backup_dir, sizeof(backup_dir), "%s/kvm-fpr-backups",
             getenv("HOME") != NULL ? getenv("HOME") : "/tmp");
    mkdir(backup_dir, 0700);
    snprintf(backup, sizeof(backup), "%s/%s_%s_orig.xml", backup_dir, vm, ts);
    snprintf(buf, sizeof(buf), "virsh dumpxml %s", vm);
    r = i_run_sudo(sudo_pass, buf);
    if (i_run_ok(&r) && r.out != NULL)
    {
        f = fopen(backup, "w");
        if (f != NULL)
        {
            fwrite(r.out, 1, strlen(r.out), f);
            fclose(f);
            i_logf(log, ctx, ">> 已备份当前配置：%s", backup);
        }
    }
    i_run_free(&r);

    /* 2. 写编辑后的 XML 到临时文件 */
    f = fopen(tmp, "w");
    if (f == NULL)
    {
        i_logf(log, ctx, "[错误] 无法写入临时文件 %s", tmp);
        return -1;
    }
    fwrite(xml_text, 1, strlen(xml_text), f);
    fclose(f);

    /* 3. 移除现有定义 */
    {
        const char *attempts[3];
        attempts[0] = "virsh undefine %s";
        attempts[1] = "virsh undefine %s --managed-save";
        attempts[2] = "virsh undefine %s --nvram --managed-save";
        for (i = 0; i < 3 && !undef_ok; ++i)
        {
            snprintf(buf, sizeof(buf), attempts[i], vm);
            r = i_run_sudo(sudo_pass, buf);
            if (i_run_ok(&r))
                undef_ok = 1;
            i_run_free(&r);
        }
    }
    if (!undef_ok)
    {
        i_logf(log, ctx, "[错误] 移除旧定义失败，已中止");
        return -1;
    }

    /* 4. 加载编辑后的配置 */
    snprintf(buf, sizeof(buf), "virsh define %s", tmp);
    r = i_run_sudo(sudo_pass, buf);
    if (!i_run_ok(&r))
    {
        i_logf(log, ctx, "[错误] 加载修改后的配置失败：%s", r.out ? r.out : "?");
        i_logf(log, ctx, "[恢复] 可执行：sudo virsh define %s", backup);
        i_run_free(&r);
        return -1;
    }
    i_run_free(&r);
    i_logf(log, ctx, "[完成] 已应用编辑后的 XML 配置");
    return 0;
}

/*---------------------------------------------------------------------------*/

int fpr_export_xml(const char *sudo_pass, const char *vm, const char *path,
                   FprLogFn log, void *ctx)
{
    char *xml = NULL;
    FILE *f;
    if (fpr_dumpxml(sudo_pass, vm, &xml, log, ctx) != 0)
        return -1;
    f = fopen(path, "w");
    if (f == NULL)
    {
        i_logf(log, ctx, "[错误] 无法写入文件 %s", path);
        free(xml);
        return -1;
    }
    fwrite(xml, 1, strlen(xml), f);
    fclose(f);
    free(xml);
    return 0;
}
