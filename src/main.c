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
 * main.c - NAppGUI 图形界面
 */

#include "nappgui.h"
#include "fpr.h"
#include "icon_data.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

typedef struct _app_t App;

struct _app_t
{
    Window *window;
    PopUp *pop_vm;
    Edit *edit_pass;
    Button *chk_force;
    Button *chk_guest;
    Button *btn_list;
    Button *btn_go;
    Label *lbl_state;
    TextView *log;
    char **vm_names;
    int vm_count;
    /* 硬件指纹条目 */
    FprItem **items;
    int item_count;
    Layout *tbl;               /* 指纹表格布局 */
    Label *row_name[20];       /* 条目名 */
    Edit *row_cur[20];         /* 当前值（只读） */
    Edit *row_new[20];         /* 新值（可编辑） */
    int max_rows;
    /* 品牌模板 */
    PopUp *pop_tpl;
    FprTemplate **tpls;
    int tpl_count;
    /* 防虚拟机检测 */
    Button *chk_kvm;
    Button *chk_hypervisor;
    Button *chk_vmport;
    Button *chk_hv_vendor;
    Edit *edit_hv_vendor;
    /* 备份历史对话框状态 */
    char **bk_paths;
    char **bk_times;
    int bk_count;
    int bk_vm_index;
    ListBox *bk_list;
    Edit *xml_editor;   /* 查看文件对话框编辑器 */
};

static void i_OnHelp(App *app, Event *e); /* 前向声明 */
static void i_load_items(App *app);      /* 前向声明 */

/*---------------------------------------------------------------------------*/

static void i_log(App *app, const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    String *s;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s = str_printf("%s\n", buf);
    textview_writef(app->log, tc(s));
    str_destroy(&s);
}

/*---------------------------------------------------------------------------*/

static void i_log_ctx(void *ctx, const char *msg)
{
    App *app = (App *)ctx;
    i_log(app, msg);
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

static void i_OnList(App *app, Event *e)
{
    char **names = NULL;
    int count = 0;
    int i;
    const char *pass = edit_get_text(app->edit_pass);

    unref(e);
    i_free_vms(app);
    popup_clear(app->pop_vm);
    i_log(app, ">> 正在刷新虚拟机列表 ...");

    if (fpr_list_vms(pass, &names, &count, i_log_ctx, app) != 0)
    {
        i_log(app, "[提示] 请检查 sudo 密码是否正确，或当前用户是否在 libvirt 用户组");
        return;
    }

    app->vm_names = names;
    app->vm_count = count;
    if (count == 0)
    {
        i_log(app, "[提示] 没有找到任何虚拟机");
        label_text(app->lbl_state, "无虚拟机");
        return;
    }
    for (i = 0; i < count; ++i)
        popup_add_elem(app->pop_vm, app->vm_names[i], NULL);
    popup_selected(app->pop_vm, 0);
    i_log(app, ">> 共找到 %d 台虚拟机", count);
    i_load_items(app);
}

/*---------------------------------------------------------------------------*/

/* 选择虚拟机时加载其指纹 */
static void i_OnSelect(App *app, Event *e)
{
    unref(e);
    i_load_items(app);
}

/*---------------------------------------------------------------------------*/

/* 加载选中虚拟机的当前指纹与自动生成的新指纹到表格 */
static void i_load_items(App *app)
{
    int sel;
    int i;

    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
        return;

    if (app->items != NULL)
    {
        fpr_free_items(app->items, app->item_count);
        app->items = NULL;
        app->item_count = 0;
    }

    i_log(app, ">> 正在读取虚拟机 %s 的硬件指纹 ...", app->vm_names[sel]);
    if (fpr_get_items(edit_get_text(app->edit_pass), app->vm_names[sel],
                      &app->items, &app->item_count, i_log_ctx, app) != 0)
        return;

    /* 填充表格 */
    for (i = 0; i < app->max_rows; ++i)
    {
        if (i < app->item_count && app->items[i] != NULL)
        {
            label_text(app->row_name[i], app->items[i]->name);
            edit_text(app->row_cur[i], app->items[i]->current);
            edit_text(app->row_new[i], app->items[i]->newval);
            layout_show_row(app->tbl, i + 1, TRUE);
        }
        else
        {
            layout_show_row(app->tbl, i + 1, FALSE);
        }
    }
    i_log(app, ">> 已生成 %d 项硬件指纹（右侧可手动修改）", app->item_count);
    window_update(app->window);
}

/*---------------------------------------------------------------------------*/

/* 重新随机生成新指纹（右侧列） */
static void i_OnRegen(App *app, Event *e)
{
    int i;
    unref(e);
    if (app->items == NULL || app->item_count <= 0)
    {
        i_log(app, "[提示] 请先选择虚拟机并加载指纹");
        return;
    }
    fpr_regen_values(app->items, app->item_count);
    for (i = 0; i < app->item_count; ++i)
        edit_text(app->row_new[i], app->items[i]->newval);
    i_log(app, ">> 已重新随机生成 %d 项新指纹", app->item_count);
}

/*---------------------------------------------------------------------------*/

/* 品牌模板选择：应用模板到指纹表格（右列） */
static void i_OnTplSelect(App *app, Event *e)
{
    int sel;
    int i;
    unref(e);
    sel = (int)popup_get_selected(app->pop_tpl);
    if (app->items == NULL || app->item_count <= 0)
    {
        i_log(app, "[提示] 请先选择虚拟机并加载指纹");
        return;
    }
    if (sel < 0 || sel >= app->tpl_count)
        return;
    fpr_apply_template(app->items, app->item_count, app->tpls[sel]);
    for (i = 0; i < app->item_count; ++i)
        edit_text(app->row_new[i], app->items[i]->newval);
    i_log(app, ">> 已应用品牌模板：%s（可继续手动修改右侧）", app->tpls[sel]->name);
}

/*---------------------------------------------------------------------------*/

/* 备份历史对话框：恢复 / 删除 */
static void i_OnBackupRestore(App *app, Event *e)
{
    int sel = -1;
    int i;
    const char *pass;
    unref(e);
    if (app->bk_list == NULL)
        return;
    for (i = 0; i < app->bk_count; ++i)
    {
        if (listbox_selected(app->bk_list, (uint32_t)i))
        {
            sel = i;
            break;
        }
    }
    if (sel < 0)
    {
        i_log(app, "[错误] 请先选择一条备份");
        return;
    }
    pass = edit_get_text(app->edit_pass);
    i_log(app, "======================================================");
    if (fpr_restore_backup(pass, app->vm_names[app->bk_vm_index],
                           app->bk_paths[sel], i_log_ctx, app) == 0)
        i_log(app, ">> 恢复成功，可关闭对话框后重新加载指纹");
}

/*---------------------------------------------------------------------------*/

static void i_OnBackupDelete(App *app, Event *e)
{
    int sel = -1;
    int i;
    unref(e);
    if (app->bk_list == NULL)
        return;
    for (i = 0; i < app->bk_count; ++i)
    {
        if (listbox_selected(app->bk_list, (uint32_t)i))
        {
            sel = i;
            break;
        }
    }
    if (sel < 0)
    {
        i_log(app, "[错误] 请先选择一条备份");
        return;
    }
    if (fpr_delete_backup(app->bk_paths[sel]) == 0)
        i_log(app, ">> 已删除备份：%s", app->bk_times[sel]);
    else
        i_log(app, "[错误] 删除备份失败");
}

/*---------------------------------------------------------------------------*/

static void i_OnBackupsClose(Window *window, Event *e)
{
    unref(e);
    window_stop_modal(window, ekGUI_CLOSE_BUTTON);
}

/*---------------------------------------------------------------------------*/

static void i_OnViewClose(Window *window, Event *e)
{
    unref(e);
    window_stop_modal(window, ekGUI_CLOSE_BUTTON);
}

/*---------------------------------------------------------------------------*/

static void i_OnViewSave(App *app, Event *e)
{
    int sel;
    const char *pass;
    unref(e);
    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
        return;
    pass = edit_get_text(app->edit_pass);
    i_log(app, "======================================================");
    if (fpr_apply_xml(pass, app->vm_names[sel],
                      edit_get_text(app->xml_editor), i_log_ctx, app) == 0)
        i_log(app, ">> 修改已应用，请重新加载指纹");
    else
        i_log(app, ">> 应用失败，请查看上方错误信息");
}

/*---------------------------------------------------------------------------*/

static void i_OnViewXml(App *app, Event *e)
{
    Window *dlg;
    Panel *panel;
    Layout *layout;
    Edit *editor;
    Button *btn_save;
    Button *btn_close;
    int sel;
    char *xml = NULL;

    unref(e);
    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
    {
        i_log(app, "[错误] 请先选择虚拟机");
        return;
    }
    if (fpr_dumpxml(edit_get_text(app->edit_pass), app->vm_names[sel],
                    &xml, i_log_ctx, app) != 0)
        return;

    dlg = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE);
    panel = panel_create();
    layout = layout_create(1, 3);
    editor = edit_multiline();
    edit_text(editor, xml != NULL ? xml : "");
    free(xml);
    app->xml_editor = editor;
    btn_save = button_push();
    button_text(btn_save, "保存并应用");
    button_OnClick(btn_save, listener(app, i_OnViewSave, App));
    btn_close = button_push();
    button_text(btn_close, "关闭");
    button_OnClick(btn_close, listener(dlg, i_OnViewClose, Window));

    layout_edit(layout, editor, 0, 0);
    layout_button(layout, btn_save, 0, 1);
    layout_button(layout, btn_close, 0, 2);
    layout_halign(layout, 0, 1, ekLEFT);
    layout_halign(layout, 0, 2, ekRIGHT);
    layout_vsize(layout, 0, 460);
    layout_hexpand(layout, 0);
    layout_margin(layout, 8);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 4);

    panel_layout(panel, layout);
    window_panel(dlg, panel);
    window_title(dlg, "查看 / 编辑 XML 配置");
    window_client_size(dlg, s2df(680, 540));
    window_origin(dlg, v2df(200, 120));
    window_modal(dlg, app->window);
    window_destroy(&dlg);
    app->xml_editor = NULL;
}

/*---------------------------------------------------------------------------*/

static void i_OnBackups(App *app, Event *e)
{
    Window *dlg;
    Panel *panel;
    Layout *layout;
    Label *tip;
    ListBox *list;
    Button *btn_restore;
    Button *btn_delete;
    Button *btn_close;
    int sel;
    int i;

    unref(e);
    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
    {
        i_log(app, "[错误] 请先选择虚拟机");
        return;
    }
    /* 释放上一次对话框的备份列表 */
    if (app->bk_paths != NULL)
    {
        fpr_free_backups(app->bk_paths, app->bk_times, app->bk_count);
        app->bk_paths = NULL;
        app->bk_times = NULL;
        app->bk_count = 0;
    }
    app->bk_vm_index = sel;
    fpr_list_backups(app->vm_names[sel], &app->bk_paths, &app->bk_times, &app->bk_count);

    dlg = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE);
    panel = panel_create();
    layout = layout_create(1, 4);
    list = listbox_create();
    app->bk_list = list;
    tip = label_create();
    label_text(tip, "选择备份后点「恢复」即可回滚到该时间点（原配置自动备份到 ~/kvm-fpr-backups/）");
    listbox_size(list, s2df(460, 160));
    for (i = 0; i < app->bk_count; ++i)
        listbox_add_elem(list, app->bk_times[i], NULL);
    if (app->bk_count > 0)
        listbox_select(list, 0, TRUE);
    btn_restore = button_push();
    button_text(btn_restore, "恢复所选备份");
    button_OnClick(btn_restore, listener(app, i_OnBackupRestore, App));
    btn_delete = button_push();
    button_text(btn_delete, "删除所选备份");
    button_OnClick(btn_delete, listener(app, i_OnBackupDelete, App));
    btn_close = button_push();
    button_text(btn_close, "关闭");
    button_OnClick(btn_close, listener(dlg, i_OnBackupsClose, Window));

    layout_label(layout, tip, 0, 0);
    layout_listbox(layout, list, 0, 1);
    layout_button(layout, btn_restore, 0, 2);
    layout_button(layout, btn_delete, 0, 3);
    layout_button(layout, btn_close, 0, 4);
    layout_halign(layout, 0, 2, ekLEFT);
    layout_halign(layout, 0, 3, ekLEFT);
    layout_halign(layout, 0, 4, ekRIGHT);
    layout_margin(layout, 10);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 8);
    layout_vmargin(layout, 2, 4);
    layout_vmargin(layout, 3, 4);

    panel_layout(panel, layout);
    window_panel(dlg, panel);
    window_title(dlg, "备份历史");
    window_origin(dlg, v2df(240, 160));
    window_modal(dlg, app->window);
    window_destroy(&dlg);
    app->bk_list = NULL;
}

/*---------------------------------------------------------------------------*/

/* 应用（可手动修改过的）新指纹 */
static void i_OnApply(App *app, Event *e)
{
    const char *pass;
    const char *vm;
    int sel;
    int rc;
    int i;

    unref(e);
    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
    {
        i_log(app, "[错误] 请先选择一台虚拟机");
        return;
    }
    if (app->items == NULL || app->item_count <= 0)
    {
        i_log(app, "[错误] 请先点击「刷新列表」加载指纹");
        return;
    }

    /* 收集右侧编辑框内容为新值 */
    for (i = 0; i < app->item_count; ++i)
    {
        const char *txt = edit_get_text(app->row_new[i]);
        if (txt == NULL || txt[0] == '\0')
        {
            i_log(app, "[错误] 第 %d 项新值为空，请填写或点击「重新随机」", i + 1);
            return;
        }
        free(app->items[i]->newval);
        app->items[i]->newval = strdup(txt);
    }

    vm = app->vm_names[sel];
    pass = edit_get_text(app->edit_pass);

    /* 收集防虚拟机检测选项 */
    {
        unsigned int avoid = 0;
        const char *hv = NULL;
        if (button_get_state(app->chk_kvm) == ekGUI_ON)
            avoid |= FPR_AVOID_KVM_HIDDEN;
        if (button_get_state(app->chk_hypervisor) == ekGUI_ON)
            avoid |= FPR_AVOID_HYPERVISOR;
        if (button_get_state(app->chk_vmport) == ekGUI_ON)
            avoid |= FPR_AVOID_VMPORT;
        if (button_get_state(app->chk_hv_vendor) == ekGUI_ON)
        {
            hv = edit_get_text(app->edit_hv_vendor);
            if (hv == NULL || hv[0] == '\0')
            {
                i_log(app, "[错误] 已勾选 HyperV 厂商 ID，请填写厂商 ID（≤12 字符）");
                return;
            }
            avoid |= FPR_AVOID_HYPERV_VENDOR;
        }

        i_log(app, "======================================================");
        i_log(app, ">> 开始应用新指纹：%s", vm);
        rc = fpr_apply_items_ext(pass, vm, app->items, app->item_count,
                                 avoid, hv,
                                 button_get_state(app->chk_force) == ekGUI_ON,
                                 button_get_state(app->chk_guest) == ekGUI_ON,
                                 i_log_ctx, app);
    }
    if (rc == 0)
    {
        i_log(app, ">> 应用成功！如需继续可再次「重新随机」后应用");
        i_load_items(app); /* 刷新为当前新值 */
    }
    else
    {
        i_log(app, ">> 应用失败，请查看上方错误信息");
    }
}

/*---------------------------------------------------------------------------*/

static void i_OnClose(App *app, Event *e)
{
    osapp_finish();
    unref(app);
    unref(e);
}

/*---------------------------------------------------------------------------*/

static void i_OnAboutClose(Window *window, Event *e)
{
    unref(e);
    window_stop_modal(window, ekGUI_CLOSE_BUTTON);
}

/*---------------------------------------------------------------------------*/

static void i_OpenUrl(const char *url)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
    system(cmd);
}

/*---------------------------------------------------------------------------*/

static void i_OnOpenHome(Window *window, Event *e)
{
    unref(window);
    unref(e);
    i_OpenUrl("http://www.yaoying.vip");
}

/*---------------------------------------------------------------------------*/

static void i_OnOpenProject(Window *window, Event *e)
{
    unref(window);
    unref(e);
    i_OpenUrl("https://github.com/yaoyingkeji/kvm-fpr");
}

/*---------------------------------------------------------------------------*/

static void i_OnMail1(Window *window, Event *e)
{
    unref(window);
    unref(e);
    i_OpenUrl("mailto:pgy866@163.com");
}

/*---------------------------------------------------------------------------*/

static void i_OnMail2(Window *window, Event *e)
{
    unref(window);
    unref(e);
    i_OpenUrl("mailto:yaoying@yaoying.vip");
}

/*---------------------------------------------------------------------------*/

static void i_OnHelpClose(Window *window, Event *e)
{
    unref(e);
    window_stop_modal(window, ekGUI_CLOSE_BUTTON);
}

/*---------------------------------------------------------------------------*/

static void i_OnHelp(App *app, Event *e)
{
    Window *help;
    Panel *panel;
    Layout *layout;
    Label *text;
    Button *ok;

    unref(e);
    help = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE);
    panel = panel_create();
    layout = layout_create(1, 2);

    text = label_create();
    label_multiline(text, TRUE);
    label_text(text,
        "kvm-fpr 使用说明\n"
        "\n"
        "1. 点击「刷新列表」加载本机所有虚拟机；\n"
        "2. 在下拉框中选择要刷新的虚拟机；\n"
        "3. 输入 sudo 密码（root 用户可留空）；\n"
        "4. 按需勾选选项：\n"
        "   · 运行中则强制关机（否则先优雅关机）；\n"
        "   · 重置 guest 内部 machine-id（需 virt-customize）；\n"
        "5. 点击「刷新硬件指纹 / 机器码」开始刷新；\n"
        "6. 刷新前自动备份原配置到 ~/kvm-fpr-backups/；\n"
        "7. 完成后在日志区查看新旧 UUID / MAC 对比。\n"
        "\n"
        "注意事项：\n"
        "· 刷新会短暂关闭虚拟机，请先保存 guest 内工作；\n"
        "· 恢复备份：sudo virsh define ~/kvm-fpr-backups/备份文件；\n"
        "· 版权：彭刚要，协议：GPLv3。");
    ok = button_push();
    button_text(ok, "确定");
    button_OnClick(ok, listener(help, i_OnHelpClose, Window));

    layout_label(layout, text, 0, 0);
    layout_button(layout, ok, 0, 1);
    layout_halign(layout, 0, 1, ekCENTER);
    layout_margin(layout, 12);
    layout_vmargin(layout, 0, 10);

    panel_layout(panel, layout);
    window_panel(help, panel);
    window_title(help, "使用说明");
    window_origin(help, v2df(240, 160));
    window_modal(help, app->window);
    window_destroy(&help);
}

/*---------------------------------------------------------------------------*/

static void i_OnAbout(App *app, Event *e)
{
    Window *about;
    Panel *panel;
    Layout *layout;
    ImageView *icon;
    Image *img;
    Label *title, *copyright, *mail1, *mail2, *web, *project, *lic;
    Button *ok;

    unref(e);
    about = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE);
    panel = panel_create();
    layout = layout_create(2, 8);

    icon = imageview_create();
    imageview_size(icon, s2df(72, 72));
    img = image_from_data(kIconData, kIconDataSize);
    if (img != NULL)
        imageview_image(icon, img);

    title = label_create();
    label_text(title, "kvm-fpr v1.1.0\nKVM 硬件指纹刷新工具");
    copyright = label_create();
    label_text(copyright, "版权：© 2026 彭刚要");
    mail1 = label_create();
    label_text(mail1, "pgy866@163.com");
    label_color(mail1, color_rgb(0x1a, 0x73, 0xe8));
    label_style_over(mail1, ekFUNDERLINE);
    label_color_over(mail1, color_rgb(0x0b, 0x3d, 0x91));
    label_OnClick(mail1, listener(about, i_OnMail1, Window));
    mail2 = label_create();
    label_text(mail2, "yaoying@yaoying.vip");
    label_color(mail2, color_rgb(0x1a, 0x73, 0xe8));
    label_style_over(mail2, ekFUNDERLINE);
    label_color_over(mail2, color_rgb(0x0b, 0x3d, 0x91));
    label_OnClick(mail2, listener(about, i_OnMail2, Window));
    web = label_create();
    label_text(web, "www.yaoying.vip");
    label_color(web, color_rgb(0x1a, 0x73, 0xe8));
    label_style_over(web, ekFUNDERLINE);
    label_color_over(web, color_rgb(0x0b, 0x3d, 0x91));
    label_OnClick(web, listener(about, i_OnOpenHome, Window));
    project = label_create();
    label_text(project, "项目主页：https://github.com/yaoyingkeji/kvm-fpr");
    label_color(project, color_rgb(0x1a, 0x73, 0xe8));
    label_style_over(project, ekFUNDERLINE);
    label_color_over(project, color_rgb(0x0b, 0x3d, 0x91));
    label_OnClick(project, listener(about, i_OnOpenProject, Window));
    lic = label_create();
    label_text(lic, "协议：GPLv3 (GNU General Public License v3)");

    ok = button_push();
    button_text(ok, "确定");
    button_OnClick(ok, listener(about, i_OnAboutClose, Window));

    layout_imageview(layout, icon, 0, 0);
    layout_label(layout, title, 1, 0);
    layout_label(layout, copyright, 1, 1);
    layout_label(layout, mail1, 1, 2);
    layout_label(layout, mail2, 1, 3);
    layout_label(layout, web, 1, 4);
    layout_label(layout, project, 1, 5);
    layout_label(layout, lic, 1, 6);
    layout_button(layout, ok, 0, 7);
    layout_halign(layout, 0, 7, ekCENTER);
    layout_valign(layout, 1, 0, ekTOP);
    layout_margin(layout, 14);
    layout_hmargin(layout, 0, 12);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 4);
    layout_vmargin(layout, 2, 4);
    layout_vmargin(layout, 3, 4);
    layout_vmargin(layout, 4, 4);
    layout_vmargin(layout, 5, 4);
    layout_vmargin(layout, 6, 12);

    panel_layout(panel, layout);
    window_panel(about, panel);
    window_title(about, "关于 kvm-fpr");
    window_origin(about, v2df(260, 200));
    window_modal(about, app->window);
    window_destroy(&about);
}

/*---------------------------------------------------------------------------*/

static Panel *i_panel(App *app)
{
    Panel *panel = panel_create();
    Layout *root = layout_create(1, 3);
    Layout *top = layout_create(6, 7);
    Label *l1 = label_create();
    Label *l2 = label_create();
    int i;

    app->max_rows = 20;

    /* 虚拟机选择行 */
    label_text(l1, "虚拟机：");
    app->pop_vm = popup_create();
    popup_list_height(app->pop_vm, 12);
    popup_OnSelect(app->pop_vm, listener(app, i_OnSelect, App));
    app->btn_list = button_push();
    button_text(app->btn_list, "刷新列表");
    button_OnClick(app->btn_list, listener(app, i_OnList, App));

    layout_label(top, l1, 0, 0);
    layout_popup(top, app->pop_vm, 1, 0);
    layout_button(top, app->btn_list, 2, 0);

    /* sudo 密码行 */
    label_text(l2, "sudo 密码：");
    app->edit_pass = edit_create();
    edit_passmode(app->edit_pass, TRUE);
    edit_phtext(app->edit_pass, "输入 sudo 密码（root 用户可留空）");
    app->lbl_state = label_create();
    label_text(app->lbl_state, "状态：未选择");

    layout_label(top, l2, 0, 1);
    layout_edit(top, app->edit_pass, 1, 1);
    layout_label(top, app->lbl_state, 3, 1);

    /* 选项行 */
    app->chk_force = button_check();
    button_text(app->chk_force, "运行中则强制关机");
    button_state(app->chk_force, ekGUI_ON);
    app->chk_guest = button_check();
    button_text(app->chk_guest, "重置 guest 内部 machine-id");

    layout_button(top, app->chk_force, 0, 2);
    layout_button(top, app->chk_guest, 1, 2);

    /* 品牌模板行 */
    {
        Label *l3 = label_create();
        label_text(l3, "品牌模板：");
        app->pop_tpl = popup_create();
        popup_list_height(app->pop_tpl, 10);
        popup_OnSelect(app->pop_tpl, listener(app, i_OnTplSelect, App));
        layout_label(top, l3, 0, 3);
        layout_popup(top, app->pop_tpl, 1, 3);
        layout_halign(top, 0, 3, ekLEFT);
    }

    /* 防虚拟机检测行 */
    {
        app->chk_kvm = button_check();
        button_text(app->chk_kvm, "隐藏 KVM 标志");
        app->chk_hypervisor = button_check();
        button_text(app->chk_hypervisor, "隐藏 hypervisor CPU 标志");
        app->chk_vmport = button_check();
        button_text(app->chk_vmport, "禁用 VMWare 端口");
        app->chk_hv_vendor = button_check();
        button_text(app->chk_hv_vendor, "HyperV 厂商 ID");
        app->edit_hv_vendor = edit_create();
        edit_phtext(app->edit_hv_vendor, "如 Microsofit（≤12 字符）");
        layout_button(top, app->chk_kvm, 0, 4);
        layout_button(top, app->chk_hypervisor, 1, 4);
        layout_button(top, app->chk_vmport, 2, 4);
        layout_button(top, app->chk_hv_vendor, 3, 4);
        layout_edit(top, app->edit_hv_vendor, 4, 4);
    }

    /* 按钮行：应用新指纹 | 重新随机 | 备份历史 | 使用说明 | 关于 */
    app->btn_go = button_push();
    button_text(app->btn_go, "应用新指纹");
    button_OnClick(app->btn_go, listener(app, i_OnApply, App));
    layout_button(top, app->btn_go, 0, 5);
    layout_halign(top, 0, 5, ekLEFT);

    {
        Button *btn_regen = button_push();
        button_text(btn_regen, "重新随机");
        button_OnClick(btn_regen, listener(app, i_OnRegen, App));
        layout_button(top, btn_regen, 1, 5);
        layout_halign(top, 1, 5, ekLEFT);

        Button *btn_backups = button_push();
        button_text(btn_backups, "备份历史");
        button_OnClick(btn_backups, listener(app, i_OnBackups, App));
        layout_button(top, btn_backups, 2, 5);
        layout_halign(top, 2, 5, ekLEFT);

        Button *btn_view = button_push();
        button_text(btn_view, "查看文件");
        button_OnClick(btn_view, listener(app, i_OnViewXml, App));
        layout_button(top, btn_view, 3, 5);
        layout_halign(top, 3, 5, ekLEFT);

        Button *btn_help = button_push();
        button_text(btn_help, "使用说明");
        button_OnClick(btn_help, listener(app, i_OnHelp, App));
        layout_button(top, btn_help, 4, 5);
        layout_halign(top, 4, 5, ekLEFT);

        Button *btn_about = button_push();
        button_text(btn_about, "关于");
        button_OnClick(btn_about, listener(app, i_OnAbout, App));
        layout_button(top, btn_about, 5, 5);
        layout_halign(top, 5, 5, ekLEFT);
    }

    /* 指纹表格：表头 + 条目行 */
    {
        Panel *tpanel = panel_create();
        Label *h1 = label_create();
        Label *h2 = label_create();
        Label *h3 = label_create();
        app->tbl = layout_create(3, app->max_rows + 1);

        label_text(h1, "硬件指纹项");
        label_text(h2, "当前值（只读）");
        label_text(h3, "新值（自动随机生成，可手动修改）");
        layout_label(app->tbl, h1, 0, 0);
        layout_label(app->tbl, h2, 1, 0);
        layout_label(app->tbl, h3, 2, 0);
        layout_vsize(app->tbl, 0, 30);

        for (i = 0; i < app->max_rows; ++i)
        {
            app->row_name[i] = label_create();
            app->row_cur[i] = edit_create();
            edit_editable(app->row_cur[i], FALSE);
            app->row_new[i] = edit_create();
            layout_label(app->tbl, app->row_name[i], 0, i + 1);
            layout_edit(app->tbl, app->row_cur[i], 1, i + 1);
            layout_edit(app->tbl, app->row_new[i], 2, i + 1);
            layout_vsize(app->tbl, i + 1, 34);
            layout_show_row(app->tbl, i + 1, FALSE);
        }
        layout_margin(app->tbl, 2);
        panel_layout(tpanel, app->tbl);
        layout_panel(root, tpanel, 0, 1);
    }

    /* 日志区 */
    app->log = textview_create();

    layout_layout(root, top, 0, 0);
    layout_textview(root, app->log, 0, 2);
    layout_margin(root, 6);
    layout_vmargin(root, 0, 6);
    layout_vmargin(root, 1, 6);
    layout_vexpand(root, 2);
    layout_vsize(top, 0, 34);
    layout_vsize(top, 1, 34);
    layout_vsize(top, 2, 34);
    layout_vsize(top, 3, 34);
    layout_vsize(top, 4, 34);
    layout_vsize(top, 5, 38);
    layout_hmargin(top, 0, 4);
    layout_hmargin(top, 1, 4);
    layout_hsize(top, 1, 220);
    layout_hsize(top, 2, 90);

    panel_layout(panel, root);
    return panel;
}

/*---------------------------------------------------------------------------*/

static App *i_create(void)
{
    App *app = heap_new0(App);
    Panel *panel = i_panel(app);
    int i;
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, panel);
    window_title(app->window, "KVM 硬件指纹刷新工具");
    window_client_size(app->window, s2df(740, 820));
    window_origin(app->window, v2df(180, 60));
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_show(app->window);

    /* 填充品牌模板下拉（内置 + 用户自定义） */
    fpr_list_templates(&app->tpls, &app->tpl_count);
    for (i = 0; i < app->tpl_count; ++i)
        popup_add_elem(app->pop_tpl, app->tpls[i]->name, NULL);
    return app;
}

/*---------------------------------------------------------------------------*/

static void i_destroy(App **app)
{
    if ((*app)->items != NULL)
        fpr_free_items((*app)->items, (*app)->item_count);
    if ((*app)->bk_paths != NULL)
        fpr_free_backups((*app)->bk_paths, (*app)->bk_times, (*app)->bk_count);
    if ((*app)->tpls != NULL)
        fpr_free_templates((*app)->tpls, (*app)->tpl_count);
    i_free_vms(*app);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
