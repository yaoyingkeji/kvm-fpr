/*
 * kvm-fpr - KVM 虚拟机硬件指纹刷新工具（GTK3 版）
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
 * 编译：gcc -I../src main_gtk.c ../src/fpr.c $(pkg-config --cflags --libs gtk+-3.0) -o kvm-fpr-gtk
 */

#include <gtk/gtk.h>
#include "fpr.h"
#include "icon_data.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#define MAX_ROWS 20

typedef struct _app_t App;

struct _app_t
{
    GtkWidget *window;
    GtkWidget *combo;
    GtkWidget *entry_pass;
    GtkWidget *chk_force;
    GtkWidget *chk_guest;
    GtkWidget *label_state;
    GtkWidget *textview;
    GtkWidget *tbl_grid;
    GtkWidget *row_name[MAX_ROWS];
    GtkWidget *row_cur[MAX_ROWS];
    GtkWidget *row_new[MAX_ROWS];
    char **vm_names;
    int vm_count;
    FprItem **items;
    int item_count;
};

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

    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->textview));
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
    if (fpr_get_items(gtk_entry_get_text(GTK_ENTRY(app->entry_pass)),
                      app->vm_names[sel], &app->items, &app->item_count,
                      i_log_cb, app) != 0)
        return;

    for (i = 0; i < MAX_ROWS; ++i)
    {
        if (i < app->item_count && app->items[i] != NULL)
        {
            gtk_label_set_text(GTK_LABEL(app->row_name[i]), app->items[i]->name);
            gtk_entry_set_text(GTK_ENTRY(app->row_cur[i]), app->items[i]->current);
            gtk_entry_set_text(GTK_ENTRY(app->row_new[i]), app->items[i]->newval);
            gtk_widget_show(app->row_name[i]);
            gtk_widget_show(app->row_cur[i]);
            gtk_widget_show(app->row_new[i]);
        }
        else
        {
            gtk_widget_hide(app->row_name[i]);
            gtk_widget_hide(app->row_cur[i]);
            gtk_widget_hide(app->row_new[i]);
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

    pass = gtk_entry_get_text(GTK_ENTRY(app->entry_pass));
    i_free_vms(app);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->combo));
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
        gtk_label_set_text(GTK_LABEL(app->label_state), "无虚拟机");
        return;
    }
    for (i = 0; i < count; ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo), app->vm_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo), 0);
    gtk_label_set_text(GTK_LABEL(app->label_state), "已选择");
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
        gtk_entry_set_text(GTK_ENTRY(app->row_new[i]), app->items[i]->newval);
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
        const char *txt = gtk_entry_get_text(GTK_ENTRY(app->row_new[i]));
        if (txt == NULL || txt[0] == '\0')
        {
            i_append_log(app, "[错误] 第 %d 项新值为空，请填写或点击「重新随机」", i + 1);
            return;
        }
        free(app->items[i]->newval);
        app->items[i]->newval = strdup(txt);
    }

    vm = app->vm_names[sel];
    pass = gtk_entry_get_text(GTK_ENTRY(app->entry_pass));

    i_append_log(app, "======================================================");
    i_append_log(app, ">> 开始应用新指纹：%s", vm);
    rc = fpr_apply_items(pass, vm, app->items, app->item_count,
                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_force)),
                         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_guest)),
                         i_log_cb, app);
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
        "   · 也可直接手动修改右侧任意项；\n"
        "5. 按需勾选：强制关机 / 重置 guest machine-id；\n"
        "6. 点击「应用新指纹」开始刷新；\n"
        "7. 刷新前自动备份原配置到 ~/kvm-fpr-backups/。\n"
        "\n"
        "注意事项：\n"
        "· 刷新会短暂关闭虚拟机，请先保存 guest 内工作；\n"
        "· 恢复备份：sudo virsh define ~/kvm-fpr-backups/备份文件；\n"
        "· 版权：彭刚要，协议：GPLv3。");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_container_add(GTK_CONTAINER(content), label);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
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
    gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);

    label = gtk_label_new("kvm-fpr v1.0.1\nKVM 硬件指纹刷新工具");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    label = gtk_label_new("版权：© 2026 彭刚要");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"mailto:pgy866@163.com\">pgy866@163.com</a>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"mailto:yaoying@yaoying.vip\">yaoying@yaoying.vip</a>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "<a href=\"http://www.yaoying.vip\">www.yaoying.vip</a>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
        "项目主页：<a href=\"https://github.com/yaoyingkeji/kvm-fpr\">https://github.com/yaoyingkeji/kvm-fpr</a>");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    label = gtk_label_new("协议：GPLv3 (GNU General Public License v3)");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(content), hbox);
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(loader);
}

/*---------------------------------------------------------------------------*/

static void i_OnDestroy(GtkWidget *widget, gpointer user_data)
{
    App *app = (App *)user_data;
    (void)widget;
    i_free_vms(app);
    i_free_items(app);
    gtk_main_quit();
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    App *app;
    GtkWidget *vbox;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *btn;
    GtkWidget *sw;
    int i;

    gtk_init(&argc, &argv);

    app = g_malloc0(sizeof(App));
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "KVM 硬件指纹刷新工具");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 720, 640);
    gtk_window_set_position(GTK_WINDOW(app->window), GTK_WIN_POS_CENTER);
    g_signal_connect(app->window, "destroy", G_CALLBACK(i_OnDestroy), app);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
    gtk_container_add(GTK_CONTAINER(app->window), vbox);

    /* 虚拟机行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    label = gtk_label_new("虚拟机：");
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    app->combo = gtk_combo_box_text_new();
    gtk_widget_set_hexpand(app->combo, TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->combo, 1, 0, 1, 1);
    g_signal_connect(app->combo, "changed", G_CALLBACK(i_OnComboChanged), app);
    btn = gtk_button_new_with_label("刷新列表");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnList), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* 密码行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    label = gtk_label_new("sudo 密码：");
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    app->entry_pass = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app->entry_pass), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_pass), "输入 sudo 密码（root 用户可留空）");
    gtk_widget_set_hexpand(app->entry_pass, TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->entry_pass, 1, 0, 1, 1);
    app->label_state = gtk_label_new("状态：未选择");
    gtk_grid_attach(GTK_GRID(grid), app->label_state, 3, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* 选项行 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    app->chk_force = gtk_check_button_new_with_label("运行中则强制关机");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_force), TRUE);
    gtk_grid_attach(GTK_GRID(grid), app->chk_force, 0, 0, 1, 1);
    app->chk_guest = gtk_check_button_new_with_label("重置 guest 内部 machine-id");
    gtk_grid_attach(GTK_GRID(grid), app->chk_guest, 1, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* 按钮行：应用新指纹 | 重新随机 | 使用说明 | 关于 */
    grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    btn = gtk_button_new_with_label("应用新指纹");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnApply), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 0, 0, 1, 1);
    btn = gtk_button_new_with_label("重新随机");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnRegen), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 1, 0, 1, 1);
    btn = gtk_button_new_with_label("使用说明");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnHelp), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 2, 0, 1, 1);
    btn = gtk_button_new_with_label("关于");
    g_signal_connect(btn, "clicked", G_CALLBACK(i_OnAbout), app);
    gtk_grid_attach(GTK_GRID(grid), btn, 3, 0, 1, 1);
    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* 指纹表格：表头 + 20 行 */
    app->tbl_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(app->tbl_grid), 6);
    gtk_grid_set_row_spacing(GTK_GRID(app->tbl_grid), 2);
    label = gtk_label_new("硬件指纹项");
    gtk_grid_attach(GTK_GRID(app->tbl_grid), label, 0, 0, 1, 1);
    label = gtk_label_new("当前值（只读）");
    gtk_grid_attach(GTK_GRID(app->tbl_grid), label, 1, 0, 1, 1);
    label = gtk_label_new("新值（自动随机生成，可手动修改）");
    gtk_grid_attach(GTK_GRID(app->tbl_grid), label, 2, 0, 1, 1);
    for (i = 0; i < MAX_ROWS; ++i)
    {
        app->row_name[i] = gtk_label_new("");
        gtk_grid_attach(GTK_GRID(app->tbl_grid), app->row_name[i], 0, i + 1, 1, 1);
        app->row_cur[i] = gtk_entry_new();
        gtk_editable_set_editable(GTK_EDITABLE(app->row_cur[i]), FALSE);
        gtk_grid_attach(GTK_GRID(app->tbl_grid), app->row_cur[i], 1, i + 1, 1, 1);
        app->row_new[i] = gtk_entry_new();
        gtk_grid_attach(GTK_GRID(app->tbl_grid), app->row_new[i], 2, i + 1, 1, 1);
        gtk_widget_hide(app->row_name[i]);
        gtk_widget_hide(app->row_cur[i]);
        gtk_widget_hide(app->row_new[i]);
    }
    gtk_box_pack_start(GTK_BOX(vbox), app->tbl_grid, FALSE, FALSE, 0);

    /* 日志区 */
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    app->textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->textview), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(sw), app->textview);
    gtk_widget_set_hexpand(sw, TRUE);
    gtk_widget_set_vexpand(sw, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

    gtk_widget_show_all(app->window);
    gtk_main();
    g_free(app);
    return 0;
}
