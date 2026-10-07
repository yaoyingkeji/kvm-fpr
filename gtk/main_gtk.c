/*
 * kvm-fpr - KVM 虚拟机硬件指纹刷新工具（GTK4 版）
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
 * 支持 X11 / Wayland（GTK4 自动选择后端），适配 Deepin 25
 * 编译：gcc -I../src main_gtk.c ../src/fpr.c ../src/theme.c $(pkg-config --cflags --libs gtk4) -o kvm-fpr-gtk
 */

#include <gtk/gtk.h>
#include "fpr.h"
#include "theme.h"
#include "icon_data.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#define MAX_ROWS 24

typedef struct _app_t App;

struct _app_t
{
    GtkWidget *window;
    GtkComboBoxText *combo;
    GtkEntry *entry_pass;
    GtkCheckButton *chk_force;
    GtkCheckButton *chk_guest;
    GtkLabel *label_state;
    GtkTextView *textview;
    GtkGrid *tbl_grid;
    GtkWidget *row_name[MAX_ROWS];
    GtkWidget *row_cur[MAX_ROWS];
    GtkWidget *row_new[MAX_ROWS];
    char **vm_names;
    int vm_count;
    FprItem **items;
    int item_count;
    GtkComboBoxText *pop_tpl;
    FprTemplate **tpls;
    int tpl_count;
    GtkCheckButton *chk_kvm;
    GtkCheckButton *chk_hypervisor;
    GtkCheckButton *chk_vmport;
    GtkCheckButton *chk_hv_vendor;
    GtkEntry *entry_hv_vendor;
    char **bk_paths;
    char **bk_times;
    int bk_count;
    int bk_vm_index;
    GtkComboBoxText *pop_theme;
    FprTheme **themes;
    int theme_count;
    char xml_path[512];   /* 系统编辑器导出的 XML 路径 */
};

static App *g_app;
static void i_OnHelp(GtkButton *button, gpointer user_data);
static void i_OnAbout(GtkButton *button, gpointer user_data);


/*---------------------------------------------------------------------------*/

static void i_dialog_close(GtkDialog *dlg, int response, gpointer data);

/*---------------------------------------------------------------------------*/

static void i_append_log(App *app, const char *fmt, ...)
{
    GtkTextBuffer *buf;
    GtkTextIter iter;
    char text[2048];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    buf = gtk_text_view_get_buffer(app->textview);
    gtk_text_buffer_get_end_iter(buf, &iter);
    gtk_text_buffer_insert(buf, &iter, text, -1);
    gtk_text_buffer_insert(buf, &iter, "\n", -1);
}

/*---------------------------------------------------------------------------*/

static void i_log_cb(void *ctx, const char *msg)
{
    App *app = (App *)ctx;
    i_append_log(app, "%s", msg);
}

/*---------------------------------------------------------------------------*/

static void i_free_vms(App *app)
{
    if (app->vm_names != NULL)
    {
        fpr_free_names(app->vm_names, app->vm_count);
        app->vm_names = NULL;
        app->vm_count = 0;
    }
}

/*---------------------------------------------------------------------------*/

static void i_free_items(App *app)
{
    if (app->items != NULL)
    {
        fpr_free_items(app->items, app->item_count);
        app->items = NULL;
        app->item_count = 0;
    }
}

/*---------------------------------------------------------------------------*/

static void i_load_items(App *app)
{
    int sel;
    int i;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
        return;

    i_free_items(app);
    i_append_log(app, ">> 正在读取虚拟机 %s 的硬件指纹 ...", app->vm_names[sel]);
    if (fpr_get_items(gtk_editable_get_text(GTK_EDITABLE(app->entry_pass)),
                      app->vm_names[sel], &app->items, &app->item_count,
                      i_log_cb, app) != 0)
        return;

    for (i = 0; i < MAX_ROWS; ++i)
    {
        if (i < app->item_count && app->items[i] != NULL)
        {
            gtk_label_set_text(GTK_LABEL(app->row_name[i]), app->items[i]->name);
            gtk_editable_set_text(GTK_EDITABLE(app->row_cur[i]), app->items[i]->current);
            gtk_editable_set_text(GTK_EDITABLE(app->row_new[i]), app->items[i]->newval);
            gtk_widget_set_visible(app->row_name[i], TRUE);
            gtk_widget_set_visible(app->row_cur[i], TRUE);
            gtk_widget_set_visible(app->row_new[i], TRUE);
        }
        else
        {
            gtk_widget_set_visible(app->row_name[i], FALSE);
            gtk_widget_set_visible(app->row_cur[i], FALSE);
            gtk_widget_set_visible(app->row_new[i], FALSE);
        }
    }
    i_append_log(app, ">> 已生成 %d 项硬件指纹（右侧可手动修改）", app->item_count);
}

/*---------------------------------------------------------------------------*/

static void i_OnList(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    const char *pass;
    char **names = NULL;
    int count = 0;
    int i;
    (void)button;

    pass = gtk_editable_get_text(GTK_EDITABLE(app->entry_pass));
    i_free_vms(app);
    gtk_combo_box_text_remove_all(app->combo);
    i_append_log(app, ">> 正在刷新虚拟机列表 ...");

    if (fpr_list_vms(pass, &names, &count, i_log_cb, app) != 0)
    {
        i_append_log(app, "[提示] 请检查 sudo 密码是否正确，或当前用户是否在 libvirt 用户组");
        return;
    }

    app->vm_names = names;
    app->vm_count = count;
    if (count == 0)
    {
        i_append_log(app, "[提示] 没有找到任何虚拟机");
        gtk_label_set_text(app->label_state, "无虚拟机");
        return;
    }
    for (i = 0; i < count; ++i)
        gtk_combo_box_text_append_text(app->combo, app->vm_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo), 0);
    gtk_label_set_text(app->label_state, "已选择");
    i_append_log(app, ">> 共找到 %d 台虚拟机", count);
    i_load_items(app);
}

/*---------------------------------------------------------------------------*/

static void i_OnComboChanged(GtkComboBox *combo, gpointer user_data)
{
    App *app = (App *)user_data;
    (void)combo;
    i_load_items(app);
}

/*---------------------------------------------------------------------------*/

static void i_OnTplChanged(GtkComboBox *combo, gpointer user_data)
{
    App *app = (App *)user_data;
    int sel;
    int i;
    (void)combo;
    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->pop_tpl));
    if (app->items == NULL || app->item_count <= 0)
        return;
    if (sel < 0 || sel >= app->tpl_count)
        return;
    fpr_apply_template(app->items, app->item_count, app->tpls[sel]);
    for (i = 0; i < app->item_count; ++i)
        gtk_editable_set_text(GTK_EDITABLE(app->row_new[i]), app->items[i]->newval);
    i_append_log(app, ">> 已应用品牌模板：%s", app->tpls[sel]->name);
}

/*---------------------------------------------------------------------------*/

static void i_OnRegen(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    int i;
    (void)button;
    if (app->items == NULL || app->item_count <= 0)
    {
        i_append_log(app, "[提示] 请先选择虚拟机并加载指纹");
        return;
    }
    fpr_regen_values(app->items, app->item_count);
    for (i = 0; i < app->item_count; ++i)
        gtk_editable_set_text(GTK_EDITABLE(app->row_new[i]), app->items[i]->newval);
    i_append_log(app, ">> 已重新随机生成 %d 项新指纹", app->item_count);
}

/*---------------------------------------------------------------------------*/

static void i_OnApply(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    const char *pass;
    const char *vm;
    int sel;
    int rc;
    int i;
    (void)button;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
    {
        i_append_log(app, "[错误] 请先选择一台虚拟机");
        return;
    }
    if (app->items == NULL || app->item_count <= 0)
    {
        i_append_log(app, "[错误] 请先点击「刷新列表」加载指纹");
        return;
    }

    for (i = 0; i < app->item_count; ++i)
    {
        const char *txt = gtk_editable_get_text(GTK_EDITABLE(app->row_new[i]));
        if (txt == NULL || txt[0] == '\0')
        {
            i_append_log(app, "[错误] 第 %d 项新值为空，请填写或点击「重新随机」", i + 1);
            return;
        }
        free(app->items[i]->newval);
        app->items[i]->newval = strdup(txt);
    }

    vm = app->vm_names[sel];
    pass = gtk_editable_get_text(GTK_EDITABLE(app->entry_pass));

    {
        unsigned int avoid = 0;
        const char *hv = NULL;
        if (gtk_check_button_get_active(app->chk_kvm))
            avoid |= FPR_AVOID_KVM_HIDDEN;
        if (gtk_check_button_get_active(app->chk_hypervisor))
            avoid |= FPR_AVOID_HYPERVISOR;
        if (gtk_check_button_get_active(app->chk_vmport))
            avoid |= FPR_AVOID_VMPORT;
        if (gtk_check_button_get_active(app->chk_hv_vendor))
        {
            hv = gtk_editable_get_text(GTK_EDITABLE(app->entry_hv_vendor));
            if (hv == NULL || hv[0] == '\0')
            {
                i_append_log(app, "[错误] 已勾选 HyperV 厂商 ID，请填写厂商 ID（≤12 字符）");
                return;
            }
            avoid |= FPR_AVOID_HYPERV_VENDOR;
        }
        i_append_log(app, "======================================================");
        i_append_log(app, ">> 开始应用新指纹：%s", vm);
        rc = fpr_apply_items_ext(pass, vm, app->items, app->item_count, avoid, hv,
                                 gtk_check_button_get_active(app->chk_force),
                                 gtk_check_button_get_active(app->chk_guest),
                                 i_log_cb, app);
    }
    if (rc == 0)
    {
        i_append_log(app, ">> 应用成功！");
        i_load_items(app);
    }
    else
    {
        i_append_log(app, ">> 应用失败，请查看上方错误信息");
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnBackupRestore(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    GtkWidget *dlg = (GtkWidget *)g_object_get_data(G_OBJECT(button), "bkdlg");
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(g_object_get_data(G_OBJECT(dlg), "bkcombo"));
    int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(cb));
    (void)button;
    if (sel < 0 || sel >= app->bk_count)
    {
        i_append_log(app, "[错误] 请先选择一条备份");
        return;
    }
    i_append_log(app, "======================================================");
    if (fpr_restore_backup(gtk_editable_get_text(GTK_EDITABLE(app->entry_pass)),
                           app->vm_names[app->bk_vm_index],
                           app->bk_paths[sel], i_log_cb, app) == 0)
        i_append_log(app, ">> 恢复成功，可关闭对话框后重新加载指纹");
}

/*---------------------------------------------------------------------------*/

static void i_OnBackupDelete(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    GtkWidget *dlg = (GtkWidget *)g_object_get_data(G_OBJECT(button), "bkdlg");
    GtkComboBoxText *cb = GTK_COMBO_BOX_TEXT(g_object_get_data(G_OBJECT(dlg), "bkcombo"));
    int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(cb));
    (void)button;
    if (sel < 0 || sel >= app->bk_count)
    {
        i_append_log(app, "[错误] 请先选择一条备份");
        return;
    }
    if (fpr_delete_backup(app->bk_paths[sel]) == 0)
        i_append_log(app, ">> 已删除备份：%s", app->bk_times[sel]);
    else
        i_append_log(app, "[错误] 删除备份失败");
}

/*---------------------------------------------------------------------------*/

static void i_OnBackups(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    GtkWidget *dlg;
    GtkWidget *content;
    GtkWidget *vbox;
    GtkWidget *combo;
    GtkWidget *hbox;
    GtkWidget *btn;
    int sel;
    int i;
    (void)button;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
    {
        i_append_log(app, "[错误] 请先选择虚拟机");
        return;
    }
    if (app->bk_paths != NULL)
    {
        fpr_free_backups(app->bk_paths, app->bk_times, app->bk_count);
        app->bk_paths = NULL;
        app->bk_times = NULL;
        app->bk_count = 0;
    }
    app->bk_vm_index = sel;
    fpr_list_backups(app->vm_names[sel], &app->bk_paths, &app->bk_times, &app->bk_count);

    dlg = gtk_dialog_new_with_buttons("备份历史", GTK_WINDOW(app->window),
                                      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                      "关闭", GTK_RESPONSE_CLOSE, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(content), vbox);

    combo = gtk_combo_box_text_new();
    for (i = 0; i < app->bk_count; ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), app->bk_times[i]);
    if (app->bk_count > 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    g_object_set_data(G_OBJECT(dlg), "bkcombo", combo);
    gtk_box_append(GTK_BOX(vbox), combo);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    btn = gtk_button_new_with_label("恢复所选备份");
    g_object_set_data(G_OBJECT(btn), "bkdlg", dlg);
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnBackupRestore), app);
    gtk_box_append(GTK_BOX(hbox), btn);
    btn = gtk_button_new_with_label("删除所选备份");
    g_object_set_data(G_OBJECT(btn), "bkdlg", dlg);
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnBackupDelete), app);
    gtk_box_append(GTK_BOX(hbox), btn);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    g_signal_connect(dlg, "response", G_CALLBACK(i_dialog_close), NULL);
    gtk_widget_show(dlg);
    g_object_unref(dlg);
}

/*---------------------------------------------------------------------------*/
/* 查看文件：调用系统文本编辑器 */

static void i_OnViewOpenEditor(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    int sel;
    char cmd[1024];
    const char *home = getenv("HOME");
    (void)button;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
    {
        i_append_log(app, "[错误] 请先选择虚拟机");
        return;
    }
    if (home == NULL)
        home = "/tmp";
    snprintf(app->xml_path, sizeof(app->xml_path),
             "%s/.cache/kvm-fpr/%s.xml", home, app->vm_names[sel]);
    mkdir(home, 0700);
    snprintf(cmd, sizeof(cmd), "%s/.cache/kvm-fpr", home);
    mkdir(cmd, 0700);

    if (fpr_export_xml(gtk_editable_get_text(GTK_EDITABLE(app->entry_pass)),
                       app->vm_names[sel], app->xml_path, i_log_cb, app) != 0)
        return;
    i_append_log(app, ">> XML 已导出：%s，正在用系统文本编辑器打开 ...", app->xml_path);
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", app->xml_path);
    system(cmd);
    i_append_log(app, ">> 请在系统编辑器中修改并保存，然后点击「保存并应用」");
}

/*---------------------------------------------------------------------------*/

static void i_OnViewApply(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    int sel;
    FILE *f;
    char *text = NULL;
    long size;
    (void)button;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
        return;
    if (app->xml_path[0] == '\0')
    {
        i_append_log(app, "[错误] 请先点击「用系统编辑器打开」");
        return;
    }
    f = fopen(app->xml_path, "rb");
    if (f == NULL)
    {
        i_append_log(app, "[错误] 无法读取 %s", app->xml_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0)
    {
        text = (char *)malloc((size_t)size + 1);
        if (fread(text, 1, (size_t)size, f) == (size_t)size)
            text[size] = '\0';
        else
        {
            free(text);
            text = NULL;
        }
    }
    fclose(f);
    if (text == NULL)
    {
        i_append_log(app, "[错误] 读取 XML 失败");
        return;
    }
    i_append_log(app, "======================================================");
    if (fpr_apply_xml(gtk_editable_get_text(GTK_EDITABLE(app->entry_pass)),
                      app->vm_names[sel], text, i_log_cb, app) == 0)
        i_append_log(app, ">> 修改已应用，请重新加载指纹");
    free(text);
}

/*---------------------------------------------------------------------------*/

static void i_OnViewXml(GtkButton *button, gpointer user_data)
{
    App *app = (App *)user_data;
    GtkWidget *dlg;
    GtkWidget *content;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *btn;
    GtkWidget *sw;
    GtkWidget *view;
    char *xml = NULL;
    int sel;
    (void)button;

    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo));
    if (sel < 0 || sel >= app->vm_count)
    {
        i_append_log(app, "[错误] 请先选择虚拟机");
        return;
    }
    if (fpr_dumpxml(gtk_editable_get_text(GTK_EDITABLE(app->entry_pass)),
                    app->vm_names[sel], &xml, i_log_cb, app) != 0)
        return;

    dlg = gtk_dialog_new_with_buttons("查看 / 编辑 XML 配置", GTK_WINDOW(app->window),
                                      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                      "关闭", GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 720, 560);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(content), vbox);

    sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_text_buffer_set_text(buf, xml != NULL ? xml : "", -1);
    }
    g_free(xml);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), view);
    gtk_box_append(GTK_BOX(vbox), sw);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    btn = gtk_button_new_with_label("用系统编辑器打开");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnViewOpenEditor), app);
    gtk_box_append(GTK_BOX(hbox), btn);
    btn = gtk_button_new_with_label("保存并应用");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnViewApply), app);
    gtk_box_append(GTK_BOX(hbox), btn);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
    g_signal_connect(dlg, "response", G_CALLBACK(i_dialog_close), NULL);
    gtk_widget_show(dlg);
    g_object_unref(dlg);
}

/*---------------------------------------------------------------------------*/
/* 主题 */

static void i_apply_theme(App *app, const FprTheme *t)
{
    char css[2048];
    static GtkCssProvider *provider = NULL;
    (void)app;

    if (t == NULL)
        return;

    /* 复用同一 provider（load 替换内容），避免多次切换叠加冲突 */
    if (provider == NULL)
    {
        provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                   provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
    }

    snprintf(css, sizeof(css),
        "window { background-color: %s; color: %s; }\n"
        "entry { background-color: %s; color: %s; caret-color: %s; border-radius: 7px; padding: 5px 10px; border: 1px solid %s; }\n"
        "entry:focus { border-color: %s; box-shadow: 0 0 0 2px alpha(%s, 0.25); }\n"
        "entry selection { background-color: %s; color: %s; }\n"
        "entry placeholder, entry::placeholder-text { color: alpha(%s, 0.5); }\n"
        "textview { background-color: %s; color: %s; border-radius: 7px; }\n"
        "button { background-color: %s; background-image: none; color: %s; border-radius: 8px; padding: 6px 14px; border: none; font-weight: 500; }\n"
        "button:hover { box-shadow: inset 0 0 0 2px alpha(%s, 0.35); }\n"
        "button:active { opacity: 0.8; }\n"
        "label { color: %s; }\n"
        "checkbutton, checkbutton label { color: %s; }\n"
        "combobox, combobox button { background-color: %s; background-image: none; color: %s; border-radius: 7px; }\n"
        "combobox button:hover { box-shadow: inset 0 0 0 2px alpha(%s, 0.4); }\n"
        "popover { background-color: %s; color: %s; border-radius: 8px; }\n"
        "popover list, popover row { background-color: %s; color: %s; }\n"
        "popover row:hover { background-color: alpha(%s, 0.18); color: %s; }\n"
        "popover row:selected { background-color: %s; color: %s; }\n"
        "treeview { background-color: %s; color: %s; }\n"
        "a { color: %s; }\n",
        t->bg, t->fg,
        t->input_bg, t->input_fg, t->accent, t->header_bg,
        t->accent, t->accent,
        t->accent, t->accent_fg,
        t->input_fg,
        t->input_bg, t->input_fg,
        t->accent, t->accent_fg,
        t->accent_fg,
        t->fg, t->fg,
        t->input_bg, t->input_fg,
        t->accent,
        t->input_bg, t->input_fg,
        t->input_bg, t->input_fg,
        t->accent, t->input_fg,
        t->accent, t->accent_fg,
        t->input_bg, t->input_fg, t->link);

    gtk_css_provider_load_from_string(provider, css);
}

/*---------------------------------------------------------------------------*/

static void i_OnThemeChanged(GtkComboBox *combo, gpointer user_data)
{
    App *app = (App *)user_data;
    int sel;
    (void)combo;
    sel = gtk_combo_box_get_active(GTK_COMBO_BOX(app->pop_theme));
    if (sel < 0 || sel >= app->theme_count)
        return;
    if (strcmp(app->themes[sel]->name, theme_system_name()) == 0)
    {
        /* 跟随系统：亮/暗 */
        int dark = theme_detect_dark();
        const char *name = dark ? "暗夜黑" : "星云蓝";
        int i;
        for (i = 0; i < app->theme_count; ++i)
        {
            if (strcmp(app->themes[i]->name, name) == 0)
            {
                i_apply_theme(app, app->themes[i]);
                break;
            }
        }
        i_append_log(app, ">> 主题：跟随系统（%s）", dark ? "暗色" : "亮色");
    }
    else
    {
        i_apply_theme(app, app->themes[sel]);
        i_append_log(app, ">> 已应用主题：%s", app->themes[sel]->name);
    }
}

/*---------------------------------------------------------------------------*/

static void i_dialog_close(GtkDialog *dlg, int response, gpointer data)
{
    (void)response;
    (void)data;
    g_object_unref(dlg);
}

/*---------------------------------------------------------------------------*/

static void i_menu_popdown(GtkButton *button, gpointer user_data)
{
    GtkPopover *pop = (GtkPopover *)g_object_get_data(G_OBJECT(button), "mpop");
    (void)user_data;
    if (pop != NULL)
        gtk_popover_popdown(pop);
}

static void i_menu_help(GtkButton *button, gpointer user_data)
{
    i_menu_popdown(button, NULL);
    i_OnHelp(button, user_data);
}

static void i_menu_about(GtkButton *button, gpointer user_data)
{
    i_menu_popdown(button, NULL);
    i_OnAbout(button, user_data);
}

static void i_menu_quit(GtkButton *button, gpointer user_data)
{
    i_menu_popdown(button, NULL);
    (void)user_data;
    g_application_quit(G_APPLICATION(g_object_get_data(G_OBJECT(g_app->window), "gapp")));
}

/*---------------------------------------------------------------------------*/

static void i_OnHelp(GtkButton *button, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *label;
    (void)button;
    (void)user_data;

    dialog = gtk_dialog_new_with_buttons("使用说明", GTK_WINDOW(((App *)user_data)->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "确定", GTK_RESPONSE_OK, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new(
        "kvm-fpr 使用说明\n"
        "\n"
        "1. 点击「刷新列表」加载本机所有虚拟机；\n"
        "2. 在下拉框中选择要刷新的虚拟机；\n"
        "3. 输入 sudo 密码（root 用户可留空）；\n"
        "4. 指纹表格左列为当前值（只读），右列为新值：\n"
        "   · 点击「重新随机」自动生成全新指纹；\n"
        "   · 缺失项显示「无」，可直接手动填写；\n"
        "   · 选择「品牌模板」一键伪装品牌信息；\n"
        "5. 按需勾选防检测选项（隐藏 KVM/hypervisor、vmport、HyperV 厂商 ID）；\n"
        "6. 点击「应用新指纹」开始刷新；\n"
        "7. 「查看文件」调用系统文本编辑器编辑 XML；「备份历史」可恢复/删除备份。\n"
        "\n"
        "注意事项：\n"
        "· 刷新会短暂关闭虚拟机，请先保存 guest 内工作；\n"
        "· 恢复备份：sudo virsh define ~/kvm-fpr-backups/备份文件；\n"
        "· 版权：彭刚要，协议：GPLv3。");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_append(GTK_BOX(content), label);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(i_dialog_close), NULL);
    gtk_widget_show(dialog);
    g_object_unref(dialog);
}

/*---------------------------------------------------------------------------*/

static void i_OnAbout(GtkButton *button, gpointer user_data)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *hbox;
    GtkWidget *vbox;
    GtkWidget *img;
    GtkWidget *label;
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf;
    (void)button;
    (void)user_data;

    dialog = gtk_dialog_new_with_buttons("关于 kvm-fpr", GTK_WINDOW(((App *)user_data)->window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "确定", GTK_RESPONSE_OK, NULL);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    loader = gdk_pixbuf_loader_new_with_type("png", NULL);
    gdk_pixbuf_loader_write(loader, kIconData, kIconDataSize, NULL);
    gdk_pixbuf_loader_close(loader, NULL);
    pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    img = gtk_image_new_from_pixbuf(pixbuf);
    gtk_box_append(GTK_BOX(hbox), img);

    label = gtk_label_new("kvm-fpr v1.1.1\nKVM 硬件指纹刷新工具 (GTK4)");
    gtk_box_append(GTK_BOX(vbox), label);
    label = gtk_label_new("版权：© 2026 彭刚要");
    gtk_box_append(GTK_BOX(vbox), label);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"mailto:pgy866@163.com\">pgy866@163.com</a>");
    gtk_box_append(GTK_BOX(vbox), label);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"mailto:yaoying@yaoying.vip\">yaoying@yaoying.vip</a>");
    gtk_box_append(GTK_BOX(vbox), label);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"http://www.yaoying.vip\">www.yaoying.vip</a>");
    gtk_box_append(GTK_BOX(vbox), label);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "项目主页：<a href=\"https://github.com/yaoyingkeji/kvm-fpr\">https://github.com/yaoyingkeji/kvm-fpr</a>");
    gtk_box_append(GTK_BOX(vbox), label);
    label = gtk_label_new("协议：GPLv3 (GNU General Public License v3)\nGUI：GTK4（X11 / Wayland），适配 Deepin 25");
    gtk_box_append(GTK_BOX(vbox), label);

    gtk_box_append(GTK_BOX(hbox), vbox);
    gtk_box_append(GTK_BOX(content), hbox);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(i_dialog_close), NULL);
    gtk_widget_show(dialog);
    g_object_unref(dialog);
    g_object_unref(loader);
}

/*---------------------------------------------------------------------------*/

static void i_OnDestroy(GtkWidget *widget, gpointer user_data)
{
    App *app = (App *)user_data;
    (void)widget;
    if (app->bk_paths != NULL)
        fpr_free_backups(app->bk_paths, app->bk_times, app->bk_count);
    if (app->tpls != NULL)
        fpr_free_templates(app->tpls, app->tpl_count);
    if (app->themes != NULL)
        theme_free_all(app->themes, app->theme_count);
    i_free_vms(app);
    i_free_items(app);
    g_object_unref(app->window);
    g_application_quit(G_APPLICATION(g_object_get_data(G_OBJECT(app->window), "gapp")));
}

/*---------------------------------------------------------------------------*/

static void i_activate(GtkApplication *gapp, gpointer user_data)
{
    App *app;
    GtkWidget *vbox;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *btn;
    GtkWidget *sw;
    int i;
    (void)user_data;

    app = g_malloc0(sizeof(App));
    g_app = app;

    app->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(app->window), "KVM 硬件指纹刷新工具");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 800, 840);
    gtk_application_add_window(gapp, GTK_WINDOW(app->window));
    g_object_set_data(G_OBJECT(app->window), "gapp", gapp);
    g_signal_connect(app->window, "close-request", G_CALLBACK(i_OnDestroy), app);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(vbox, 8);
    gtk_widget_set_margin_bottom(vbox, 8);
    gtk_widget_set_margin_start(vbox, 8);
    gtk_widget_set_margin_end(vbox, 8);
    gtk_window_set_child(GTK_WINDOW(app->window), vbox);

    /* 顶部工具栏（Deepin DDE 风格：图标 + 标题 + 菜单） */
    {
        GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GdkPixbufLoader *ld = gdk_pixbuf_loader_new_with_type("png", NULL);
        GdkPixbuf *pb = NULL;
        GtkWidget *icon = NULL;
        GtkWidget *title;
        GtkWidget *menu_btn;
        GtkWidget *pop;
        GtkWidget *pbox;
        GtkWidget *mi;

        gdk_pixbuf_loader_write(ld, kIconData, kIconDataSize, NULL);
        gdk_pixbuf_loader_close(ld, NULL);
        pb = gdk_pixbuf_loader_get_pixbuf(ld);
        if (pb != NULL)
        {
            GdkPixbuf *small = gdk_pixbuf_scale_simple(pb, 28, 28, GDK_INTERP_BILINEAR);
            icon = gtk_image_new_from_pixbuf(small);
            g_object_unref(small);
            gtk_box_append(GTK_BOX(toolbar), icon);
        }
        g_object_unref(ld);

        title = gtk_label_new("KVM 硬件指纹刷新工具");
        gtk_widget_set_margin_start(title, 4);
        gtk_box_append(GTK_BOX(toolbar), title);

        {
            GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_hexpand(spacer, TRUE);
            gtk_box_append(GTK_BOX(toolbar), spacer);
        }

        menu_btn = gtk_menu_button_new();
        gtk_menu_button_set_label(GTK_MENU_BUTTON(menu_btn), "菜单");
        pop = gtk_popover_new();
        pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        mi = gtk_button_new_with_label("使用说明");
        g_object_set_data(G_OBJECT(mi), "mpop", pop);
        g_signal_connect(mi, "clicked", G_CALLBACK(i_menu_help), app);
        gtk_box_append(GTK_BOX(pbox), mi);
        mi = gtk_button_new_with_label("关于");
        g_object_set_data(G_OBJECT(mi), "mpop", pop);
        g_signal_connect(mi, "clicked", G_CALLBACK(i_menu_about), app);
        gtk_box_append(GTK_BOX(pbox), mi);
        mi = gtk_button_new_with_label("退出");
        g_object_set_data(G_OBJECT(mi), "mpop", pop);
        g_signal_connect(mi, "clicked", G_CALLBACK(i_menu_quit), app);
        gtk_box_append(GTK_BOX(pbox), mi);
        gtk_popover_set_child(GTK_POPOVER(pop), pbox);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_btn), pop);
        gtk_box_append(GTK_BOX(toolbar), menu_btn);

        gtk_box_append(GTK_BOX(vbox), toolbar);
    }

    /* 虚拟机行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("虚拟机："), 0, 0, 1, 1);
    app->combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_hexpand(GTK_WIDGET(app->combo), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->combo), 1, 0, 1, 1);
    g_signal_connect(app->combo, "changed", G_CALLBACK(i_OnComboChanged), app);
    btn = gtk_button_new_with_label("刷新列表");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnList), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 密码行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("sudo 密码："), 0, 0, 1, 1);
    app->entry_pass = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_visibility(app->entry_pass, FALSE);
    gtk_entry_set_placeholder_text(app->entry_pass, "输入 sudo 密码（root 用户可留空）");
    gtk_widget_set_hexpand(GTK_WIDGET(app->entry_pass), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->entry_pass), 1, 0, 1, 1);
    app->label_state = GTK_LABEL(gtk_label_new("状态：未选择"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->label_state), 3, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 选项行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    app->chk_force = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("运行中则强制关机"));
    gtk_check_button_set_active(app->chk_force, TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_force), 0, 0, 1, 1);
    app->chk_guest = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("重置 guest 内部 machine-id"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_guest), 1, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 品牌模板 + 主题行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("品牌模板："), 0, 0, 1, 1);
    app->pop_tpl = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    g_signal_connect(app->pop_tpl, "changed", G_CALLBACK(i_OnTplChanged), app);
    gtk_widget_set_hexpand(GTK_WIDGET(app->pop_tpl), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->pop_tpl), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("主题："), 2, 0, 1, 1);
    app->pop_theme = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    g_signal_connect(app->pop_theme, "changed", G_CALLBACK(i_OnThemeChanged), app);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->pop_theme), 3, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 防检测行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    app->chk_kvm = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("隐藏 KVM 标志"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_kvm), 0, 0, 1, 1);
    app->chk_hypervisor = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("隐藏 hypervisor CPU 标志"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_hypervisor), 1, 0, 1, 1);
    app->chk_vmport = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("禁用 VMWare 端口"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_vmport), 2, 0, 1, 1);
    app->chk_hv_vendor = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("HyperV 厂商 ID"));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->chk_hv_vendor), 3, 0, 1, 1);
    app->entry_hv_vendor = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(app->entry_hv_vendor, "如 Microsofit（≤12 字符）");
    gtk_widget_set_hexpand(GTK_WIDGET(app->entry_hv_vendor), TRUE);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(app->entry_hv_vendor), 4, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 按钮行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    btn = gtk_button_new_with_label("应用新指纹");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnApply), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
    btn = gtk_button_new_with_label("重新随机");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnRegen), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 1, 0, 1, 1);
    btn = gtk_button_new_with_label("备份历史");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnBackups), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 0, 1, 1);
    btn = gtk_button_new_with_label("查看文件");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnViewXml), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 3, 0, 1, 1);
    btn = gtk_button_new_with_label("使用说明");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnHelp), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 4, 0, 1, 1);
    btn = gtk_button_new_with_label("关于");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnAbout), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 5, 0, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);

    /* 指纹表格 */
    app->tbl_grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_column_spacing(app->tbl_grid, 6);
    gtk_grid_set_row_spacing(app->tbl_grid, 2);
    gtk_grid_attach(app->tbl_grid, gtk_label_new("硬件指纹项"), 0, 0, 1, 1);
    gtk_grid_attach(app->tbl_grid, gtk_label_new("当前值（只读）"), 1, 0, 1, 1);
    gtk_grid_attach(app->tbl_grid, gtk_label_new("新值（自动随机生成，可手动修改）"), 2, 0, 1, 1);
    for (i = 0; i < MAX_ROWS; ++i)
    {
        app->row_name[i] = gtk_label_new("");
        gtk_grid_attach(app->tbl_grid, app->row_name[i], 0, i + 1, 1, 1);
        app->row_cur[i] = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(app->row_cur[i]), FALSE);
        gtk_grid_attach(app->tbl_grid, app->row_cur[i], 1, i + 1, 1, 1);
        app->row_new[i] = gtk_entry_new();
        gtk_grid_attach(app->tbl_grid, app->row_new[i], 2, i + 1, 1, 1);
        gtk_widget_set_visible(app->row_name[i], FALSE);
        gtk_widget_set_visible(app->row_cur[i], FALSE);
        gtk_widget_set_visible(app->row_new[i], FALSE);
    }
    gtk_box_append(GTK_BOX(vbox), GTK_WIDGET(app->tbl_grid));

    /* 日志区 */
    sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    app->textview = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(app->textview, FALSE);
    gtk_text_view_set_wrap_mode(app->textview, GTK_WRAP_WORD);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), GTK_WIDGET(app->textview));
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_append(GTK_BOX(vbox), sw);

    /* 填充模板与主题 */
    fpr_list_templates(&app->tpls, &app->tpl_count);
    for (i = 0; i < app->tpl_count; ++i)
        gtk_combo_box_text_append_text(app->pop_tpl, app->tpls[i]->name);
    if (app->tpl_count > 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->pop_tpl), 0);

    theme_load_all(&app->themes, &app->theme_count);
    for (i = 0; i < app->theme_count; ++i)
        gtk_combo_box_text_append_text(app->pop_theme, app->themes[i]->name);
    gtk_window_present(GTK_WINDOW(app->window));
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    GtkApplication *gapp;
    int status;

    gapp = gtk_application_new("com.yaoying.kvmfpr", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gapp, "activate", G_CALLBACK(i_activate), NULL);
    status = g_application_run(G_APPLICATION(gapp), argc, argv);
    g_object_unref(gapp);
    return status;
}
