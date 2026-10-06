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

    /* 域 UUID */
    {
        char *cur = i_between(r.out, "<uuid>", "</uuid>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_uuid(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_UUID, "域 UUID", cur, tmp);
        }
        free(cur);
    }

    /* 各网卡 MAC */
    for (i = 0;; ++i)
    {
        char *cur = i_between(r.out, "<mac address='", "'/>", i);
        if (cur == NULL)
            break;
        if (cur[0] != '\0')
        {
            char nm[64];
            snprintf(nm, sizeof(nm), "网卡 %d MAC", i + 1);
            i_gen_mac(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_MAC, nm, cur, tmp);
        }
        free(cur);
    }

    /* SMBIOS serial（<serial> 无属性形式，位于 sysinfo 段） */
    {
        char *cur = i_between(r.out, "<entry name='serial'>", "</entry>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_SERIAL, "SMBIOS serial", cur, tmp);
        }
        free(cur);
    }

    /* SMBIOS system-uuid */
    {
        char *cur = i_between(r.out, "<entry name='system-uuid'>", "</entry>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_uuid(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_SYSTEM_UUID, "SMBIOS system-uuid", cur, tmp);
        }
        free(cur);
    }

    /* SMBIOS product-uuid */
    {
        char *cur = i_between(r.out, "<entry name='product-uuid'>", "</entry>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_uuid(tmp, sizeof(tmp));
            i_add_item(&arr, &n, &cap, FPR_TYPE_PRODUCT_UUID, "SMBIOS product-uuid", cur, tmp);
        }
        free(cur);
    }

    /* SMBIOS product-serial */
    {
        char *cur = i_between(r.out, "<entry name='product-serial'>", "</entry>", 0);
        if (cur != NULL && cur[0] != '\0')
        {
            i_gen_serial(tmp, sizeof(tmp), 16);
            i_add_item(&arr, &n, &cap, FPR_TYPE_PRODUCT_SERIAL, "SMBIOS product-serial", cur, tmp);
        }
        free(cur);
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
    "for t, v in rules:\n"
    "    if t == 'uuid':\n"
    "        xml = re.sub(r'<uuid>[^<]*</uuid>', lambda m: '<uuid>' + v + '</uuid>', xml, count=1)\n"
    "macs = [v for t, v in rules if t.startswith('mac')]\n"
    "if macs:\n"
    "    it = iter(macs)\n"
    "    xml = re.sub(r\"<mac address='[^']*'/>\", lambda m: \"<mac address='%s'/>\" % next(it), xml)\n"
    "for t, v in rules:\n"
    "    if t == 'serial':\n"
    "        xml = re.sub(r\"<entry name='serial'>[^<]*</entry>\", lambda m: \"<entry name='serial'>\" + v + \"</entry>\", xml, count=1)\n"
    "    elif t == 'systemuuid':\n"
    "        xml = re.sub(r\"<entry name='system-uuid'>[^<]*</entry>\", lambda m: \"<entry name='system-uuid'>\" + v + \"</entry>\", xml, count=1)\n"
    "    elif t == 'productuuid':\n"
    "        xml = re.sub(r\"<entry name='product-uuid'>[^<]*</entry>\", lambda m: \"<entry name='product-uuid'>\" + v + \"</entry>\", xml, count=1)\n"
    "    elif t == 'productserial':\n"
    "        xml = re.sub(r\"<entry name='product-serial'>[^<]*</entry>\", lambda m: \"<entry name='product-serial'>\" + v + \"</entry>\", xml, count=1)\n"
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

int fpr_apply_items(const char *sudo_pass, const char *vm,
                    FprItem **items, int count,
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
            default:
                break;
            }
        }
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
