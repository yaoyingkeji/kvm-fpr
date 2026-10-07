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
 * theme.c - 主题系统实现
 */

#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*---------------------------------------------------------------------------*/

/* 内置主题皮肤设计（参考 Deepin DTK 风格，前景/背景对比协调） */
static const FprTheme kBuiltinThemes[] = {
    /* 星云蓝（默认 · Deepin DDE 浅色风格 · 青蓝主色） */
    { (char *)"星云蓝",
      "#F6F8FB", "#2B2F36", "#0084FF", "#FFFFFF",
      "#FFFFFF", "#2B2F36", "#EDF1F6", "#0084FF" },
    /* 暗夜黑（Deepin DDE 深色风格 · 亮蓝主色） */
    { (char *)"暗夜黑",
      "#202327", "#E6E9ED", "#1E9EFF", "#FFFFFF",
      "#2A2E33", "#E6E9ED", "#2A2E33", "#66C7FF" },
    /* 青翠绿（浅色 · 翠绿） */
    { (char *)"青翠绿",
      "#F3F9F5", "#1D3527", "#27A35C", "#FFFFFF",
      "#FFFFFF", "#1D3527", "#E1EFE6", "#1E8E4E" },
    /* 樱粉（浅色 · 玫粉） */
    { (char *)"樱粉",
      "#FDF4F8", "#45253A", "#E0538C", "#FFFFFF",
      "#FFFFFF", "#45253A", "#F7DFEA", "#C03569" },
    /* 晨曦金（浅色 · 暖金） */
    { (char *)"晨曦金",
      "#FBF6ED", "#4A3A20", "#C8861F", "#FFFFFF",
      "#FFFFFF", "#4A3A20", "#F3E6CE", "#9E6713" }
};

/*---------------------------------------------------------------------------*/

const char *theme_system_name(void)
{
    return "跟随系统";
}

/*---------------------------------------------------------------------------*/

int theme_detect_dark(void)
{
    char cmd[256];
    char *out = NULL;
    size_t cap = 0, len = 0;
    FILE *f;
    int dark = 0;

    snprintf(cmd, sizeof(cmd),
             "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null || "
             "gsettings get org.gtk.Settings.ColorChooser custom-colors 2>/dev/null || true");
    f = popen(cmd, "r");
    if (f == NULL)
        return -1;
    len = 0;
    cap = 128;
    out = (char *)malloc(cap);
    {
        char chunk[256];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        {
            if (len + n + 1 > cap)
            {
                cap = len + n + 1;
                out = (char *)realloc(out, cap);
            }
            memcpy(out + len, chunk, n);
            len += n;
        }
        out[len] = '\0';
    }
    pclose(f);
    if (strstr(out, "prefer-dark") != NULL || strstr(out, "prefer_dark") != NULL)
        dark = 1;
    free(out);
    return dark;
}

/*---------------------------------------------------------------------------*/

static FprTheme *i_theme_copy(const FprTheme *t)
{
    FprTheme *n = (FprTheme *)calloc(1, sizeof(FprTheme));
    if (n == NULL)
        return NULL;
    n->name = strdup(t->name);
    n->bg = strdup(t->bg);
    n->fg = strdup(t->fg);
    n->accent = strdup(t->accent);
    n->accent_fg = strdup(t->accent_fg);
    n->input_bg = strdup(t->input_bg);
    n->input_fg = strdup(t->input_fg);
    n->header_bg = strdup(t->header_bg);
    n->link = strdup(t->link);
    return n;
}

/*---------------------------------------------------------------------------*/

static void i_theme_free_one(FprTheme *t)
{
    if (t == NULL)
        return;
    free(t->name);
    free(t->bg);
    free(t->fg);
    free(t->accent);
    free(t->accent_fg);
    free(t->input_bg);
    free(t->input_fg);
    free(t->header_bg);
    free(t->link);
    free(t);
}

/*---------------------------------------------------------------------------*/

static char *i_theme_config_path(char *buf, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL)
        home = "/tmp";
    snprintf(buf, size, "%s/.config/kvm-fpr/themes.conf", home);
    return buf;
}

/*---------------------------------------------------------------------------*/

static int i_read_line(char *line, size_t cap, FILE *f)
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

static void i_parse_user_themes(FprTheme ***arr, int *n, int *cap)
{
    char path[512];
    FILE *f;
    FprTheme *cur = NULL;
    char line[512];
    char *p;

    i_theme_config_path(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL)
        return;
    cur = (FprTheme *)calloc(1, sizeof(FprTheme));
    while (i_read_line(line, sizeof(line), f) == 0)
    {
        if (line[0] == '[')
        {
            p = strchr(line, ']');
            if (p != NULL)
            {
                *p = '\0';
                free(cur->name);
                cur->name = strdup(line + 1);
                /* 每遇到新节，先保存上一个完成的主题 */
                if (cur->name != NULL && cur->name[0] != '\0' && cur->bg != NULL)
                {
                    if (*n >= *cap)
                    {
                        *cap = (*cap == 0) ? 4 : *cap * 2;
                        *arr = (FprTheme **)realloc(*arr, sizeof(FprTheme *) * (size_t)*cap);
                    }
                    (*arr)[(*n)++] = cur;
                    cur = (FprTheme *)calloc(1, sizeof(FprTheme));
                }
            }
            continue;
        }
        p = strchr(line, '=');
        if (p == NULL)
            continue;
        *p = '\0';
        if (strcmp(line, "bg") == 0) { free(cur->bg); cur->bg = strdup(p + 1); }
        else if (strcmp(line, "fg") == 0) { free(cur->fg); cur->fg = strdup(p + 1); }
        else if (strcmp(line, "accent") == 0) { free(cur->accent); cur->accent = strdup(p + 1); }
        else if (strcmp(line, "accent_fg") == 0) { free(cur->accent_fg); cur->accent_fg = strdup(p + 1); }
        else if (strcmp(line, "input_bg") == 0) { free(cur->input_bg); cur->input_bg = strdup(p + 1); }
        else if (strcmp(line, "input_fg") == 0) { free(cur->input_fg); cur->input_fg = strdup(p + 1); }
        else if (strcmp(line, "header_bg") == 0) { free(cur->header_bg); cur->header_bg = strdup(p + 1); }
        else if (strcmp(line, "link") == 0) { free(cur->link); cur->link = strdup(p + 1); }
    }
    fclose(f);
    if (cur != NULL && cur->name != NULL && cur->name[0] != '\0' && cur->bg != NULL)
    {
        if (*n >= *cap)
        {
            *cap = (*cap == 0) ? 4 : *cap * 2;
            *arr = (FprTheme **)realloc(*arr, sizeof(FprTheme *) * (size_t)*cap);
        }
        (*arr)[(*n)++] = cur;
    }
    else
    {
        i_theme_free_one(cur);
    }
}

/*---------------------------------------------------------------------------*/

int theme_load_all(FprTheme ***themes, int *count)
{
    FprTheme **arr = NULL;
    int n = 0;
    int cap = 0;
    size_t i;

    /* 跟随系统（动态） */
    cap = 8;
    arr = (FprTheme **)malloc(sizeof(FprTheme *) * (size_t)cap);
    {
        FprTheme *sys = i_theme_copy(&kBuiltinThemes[0]);
        sys->name = strdup(theme_system_name());
        arr[n++] = sys;
    }
    /* 内置皮肤 */
    for (i = 0; i < sizeof(kBuiltinThemes) / sizeof(kBuiltinThemes[0]); ++i)
        arr[n++] = i_theme_copy(&kBuiltinThemes[i]);
    /* 用户自建 */
    i_parse_user_themes(&arr, &n, &cap);

    *themes = arr;
    *count = n;
    return 0;
}

/*---------------------------------------------------------------------------*/

void theme_free_all(FprTheme **themes, int count)
{
    int i;
    if (themes == NULL)
        return;
    for (i = 0; i < count; ++i)
        i_theme_free_one(themes[i]);
    free(themes);
}

/*---------------------------------------------------------------------------*/

int theme_save_user(const FprTheme *t)
{
    char path[512];
    char dir[256];
    FILE *f;
    const char *home = getenv("HOME");
    if (home == NULL)
        home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/.config/kvm-fpr", home);
    mkdir(dir, 0700);
    i_theme_config_path(path, sizeof(path));
    f = fopen(path, "a");
    if (f == NULL)
        return -1;
    fprintf(f, "\n[%s]\n", t->name != NULL ? t->name : "未命名主题");
    fprintf(f, "bg=%s\n", t->bg != NULL ? t->bg : "");
    fprintf(f, "fg=%s\n", t->fg != NULL ? t->fg : "");
    fprintf(f, "accent=%s\n", t->accent != NULL ? t->accent : "");
    fprintf(f, "accent_fg=%s\n", t->accent_fg != NULL ? t->accent_fg : "");
    fprintf(f, "input_bg=%s\n", t->input_bg != NULL ? t->input_bg : "");
    fprintf(f, "input_fg=%s\n", t->input_fg != NULL ? t->input_fg : "");
    fprintf(f, "header_bg=%s\n", t->header_bg != NULL ? t->header_bg : "");
    fprintf(f, "link=%s\n", t->link != NULL ? t->link : "");
    fclose(f);
    return 0;
}
