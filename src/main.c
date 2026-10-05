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
};

static void i_OnHelp(App *app, Event *e); /* 前向声明 */

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
}

/*---------------------------------------------------------------------------*/

static void i_OnRefresh(App *app, Event *e)
{
    const char *pass;
    const char *vm;
    int sel;
    int rc;

    unref(e);
    sel = (int)popup_get_selected(app->pop_vm);
    if (sel < 0 || sel >= app->vm_count)
    {
        i_log(app, "[错误] 请先选择一台虚拟机");
        return;
    }
    vm = app->vm_names[sel];
    pass = edit_get_text(app->edit_pass);

    i_log(app, "======================================================");
    i_log(app, ">> 开始刷新虚拟机：%s", vm);
    rc = fpr_refresh(pass, vm,
                     button_get_state(app->chk_force) == ekGUI_ON,
                     button_get_state(app->chk_guest) == ekGUI_ON,
                     i_log_ctx, app);
    if (rc == 0)
        i_log(app, ">> 刷新流程结束（成功）");
    else
        i_log(app, ">> 刷新流程结束（失败，请查看上方错误信息）");
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
    Label *title, *copyright, *mail1, *mail2, *web, *lic;
    Button *ok;

    unref(e);
    about = window_create(ekWINDOW_TITLE | ekWINDOW_CLOSE);
    panel = panel_create();
    layout = layout_create(2, 7);

    icon = imageview_create();
    imageview_size(icon, s2df(72, 72));
    img = image_from_data(kIconData, kIconDataSize);
    if (img != NULL)
        imageview_image(icon, img);

    title = label_create();
    label_text(title, "kvm-fpr v1.0.0\nKVM 硬件指纹刷新工具");
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
    layout_label(layout, lic, 1, 5);
    layout_button(layout, ok, 0, 6);
    layout_halign(layout, 0, 6, ekCENTER);
    layout_valign(layout, 1, 0, ekTOP);
    layout_margin(layout, 14);
    layout_hmargin(layout, 0, 12);
    layout_vmargin(layout, 0, 8);
    layout_vmargin(layout, 1, 4);
    layout_vmargin(layout, 2, 4);
    layout_vmargin(layout, 3, 4);
    layout_vmargin(layout, 4, 4);
    layout_vmargin(layout, 5, 12);

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
    Layout *root = layout_create(1, 2);
    Layout *top = layout_create(3, 4);
    Label *l1 = label_create();
    Label *l2 = label_create();

    /* 虚拟机选择行 */
    label_text(l1, "虚拟机：");
    app->pop_vm = popup_create();
    popup_list_height(app->pop_vm, 12);
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
    layout_label(top, app->lbl_state, 2, 1);

    /* 选项行 */
    app->chk_force = button_check();
    button_text(app->chk_force, "运行中则强制关机");
    button_state(app->chk_force, ekGUI_ON);
    app->chk_guest = button_check();
    button_text(app->chk_guest, "重置 guest 内部 machine-id");

    layout_button(top, app->chk_force, 0, 2);
    layout_button(top, app->chk_guest, 1, 2);

    /* 主操作按钮 + 使用说明 + 关于（并排） */
    app->btn_go = button_push();
    button_text(app->btn_go, "刷新硬件指纹 / 机器码");
    button_OnClick(app->btn_go, listener(app, i_OnRefresh, App));

    layout_button(top, app->btn_go, 0, 3);
    layout_halign(top, 0, 3, ekLEFT);

    {
        Button *btn_help = button_push();
        button_text(btn_help, "使用说明");
        button_OnClick(btn_help, listener(app, i_OnHelp, App));
        layout_button(top, btn_help, 1, 3);
        layout_halign(top, 1, 3, ekLEFT);

        Button *btn_about = button_push();
        button_text(btn_about, "关于");
        button_OnClick(btn_about, listener(app, i_OnAbout, App));
        layout_button(top, btn_about, 2, 3);
        layout_halign(top, 2, 3, ekLEFT);
    }

    /* 日志区 */
    app->log = textview_create();

    layout_layout(root, top, 0, 0);
    layout_textview(root, app->log, 0, 1);
    layout_vexpand(root, 1);
    layout_margin(root, 6);
    layout_vmargin(root, 0, 6);
    layout_vsize(top, 0, 26);
    layout_vsize(top, 1, 26);
    layout_vsize(top, 2, 26);
    layout_vsize(top, 3, 34);
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
    app->window = window_create(ekWINDOW_STDRES);
    window_panel(app->window, panel);
    window_title(app->window, "KVM 硬件指纹刷新工具");
    window_client_size(app->window, s2df(640, 420));
    window_origin(app->window, v2df(200, 120));
    window_OnClose(app->window, listener(app, i_OnClose, App));
    window_show(app->window);
    return app;
}

/*---------------------------------------------------------------------------*/

static void i_destroy(App **app)
{
    i_free_vms(*app);
    window_destroy(&(*app)->window);
    heap_delete(app, App);
}

/*---------------------------------------------------------------------------*/

#include <osapp/osmain.h>
osmain(i_create, i_destroy, "", App)
