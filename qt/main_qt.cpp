/*
 * kvm-fpr - KVM 虚拟机硬件指纹刷新工具（Qt5 版）
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
 * 构建：qmake main_qt.pro && make
 */

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QDialog>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>
#include <cstdio>
#include <cstdarg>

#include "fpr.h"
#include "icon_data.h"

/*---------------------------------------------------------------------------*/

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void appendLog(const char *fmt, ...);

private slots:
    void onList();
    void onRefresh();
    void onHelp();
    void onAbout();

private:
    QComboBox *m_combo;
    QLineEdit *m_pass;
    QCheckBox *m_chkForce;
    QCheckBox *m_chkGuest;
    QLabel *m_state;
    QTextEdit *m_log;
    char **m_names;
    int m_count;
};

/*---------------------------------------------------------------------------*/

static void logCallback(void *ctx, const char *msg)
{
    MainWindow *w = static_cast<MainWindow *>(ctx);
    w->appendLog("%s", msg);
}

/*---------------------------------------------------------------------------*/

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_names(nullptr), m_count(0)
{
    setWindowTitle(QStringLiteral("KVM 硬件指纹刷新工具"));
    resize(640, 420);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    /* 虚拟机行 */
    QGridLayout *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->addWidget(new QLabel(QStringLiteral("虚拟机：")), 0, 0);
    m_combo = new QComboBox;
    grid->addWidget(m_combo, 0, 1);
    QPushButton *btnList = new QPushButton(QStringLiteral("刷新列表"));
    connect(btnList, &QPushButton::clicked, this, &MainWindow::onList);
    grid->addWidget(btnList, 0, 2);
    root->addLayout(grid);

    /* 密码行 */
    grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->addWidget(new QLabel(QStringLiteral("sudo 密码：")), 0, 0);
    m_pass = new QLineEdit;
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setPlaceholderText(QStringLiteral("输入 sudo 密码（root 用户可留空）"));
    grid->addWidget(m_pass, 0, 1);
    m_state = new QLabel(QStringLiteral("状态：未选择"));
    grid->addWidget(m_state, 0, 2);
    root->addLayout(grid);

    /* 选项行 */
    grid = new QGridLayout;
    m_chkForce = new QCheckBox(QStringLiteral("运行中则强制关机"));
    m_chkForce->setChecked(true);
    grid->addWidget(m_chkForce, 0, 0);
    m_chkGuest = new QCheckBox(QStringLiteral("重置 guest 内部 machine-id"));
    grid->addWidget(m_chkGuest, 0, 1);
    root->addLayout(grid);

    /* 按钮行 */
    grid = new QGridLayout;
    QPushButton *btnGo = new QPushButton(QStringLiteral("刷新硬件指纹 / 机器码"));
    connect(btnGo, &QPushButton::clicked, this, &MainWindow::onRefresh);
    grid->addWidget(btnGo, 0, 0);
    QPushButton *btnHelp = new QPushButton(QStringLiteral("使用说明"));
    connect(btnHelp, &QPushButton::clicked, this, &MainWindow::onHelp);
    grid->addWidget(btnHelp, 0, 1);
    QPushButton *btnAbout = new QPushButton(QStringLiteral("关于"));
    connect(btnAbout, &QPushButton::clicked, this, &MainWindow::onAbout);
    grid->addWidget(btnAbout, 0, 2);
    root->addLayout(grid);

    /* 日志区 */
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    root->addWidget(m_log, 1);
}

/*---------------------------------------------------------------------------*/

void MainWindow::appendLog(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    m_log->append(QString::fromUtf8(buf));
}

/*---------------------------------------------------------------------------*/

void MainWindow::onList()
{
    char **names = nullptr;
    int count = 0;

    for (int i = 0; i < m_count; ++i)
        free(m_names[i]);
    free(m_names);
    m_names = nullptr;
    m_count = 0;
    m_combo->clear();
    appendLog(">> 正在刷新虚拟机列表 ...");

    if (fpr_list_vms(m_pass->text().toUtf8().constData(), &names, &count, logCallback, this) != 0)
    {
        appendLog("[提示] 请检查 sudo 密码是否正确，或当前用户是否在 libvirt 用户组");
        return;
    }

    m_names = names;
    m_count = count;
    if (count == 0)
    {
        appendLog("[提示] 没有找到任何虚拟机");
        m_state->setText(QStringLiteral("无虚拟机"));
        return;
    }
    for (int i = 0; i < count; ++i)
        m_combo->addItem(QString::fromUtf8(m_names[i]));
    m_combo->setCurrentIndex(0);
    m_state->setText(QStringLiteral("已选择"));
    appendLog(">> 共找到 %d 台虚拟机", count);
}

/*---------------------------------------------------------------------------*/

void MainWindow::onRefresh()
{
    int sel = m_combo->currentIndex();
    if (sel < 0 || sel >= m_count)
    {
        appendLog("[错误] 请先选择一台虚拟机");
        return;
    }
    appendLog("======================================================");
    appendLog(">> 开始刷新虚拟机：%s", m_names[sel]);
    int rc = fpr_refresh(m_pass->text().toUtf8().constData(), m_names[sel],
                         m_chkForce->isChecked() ? 1 : 0,
                         m_chkGuest->isChecked() ? 1 : 0,
                         logCallback, this);
    if (rc == 0)
        appendLog(">> 刷新流程结束（成功）");
    else
        appendLog(">> 刷新流程结束（失败，请查看上方错误信息）");
}

/*---------------------------------------------------------------------------*/

void MainWindow::onHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("使用说明"));
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QLabel *text = new QLabel(
        QStringLiteral("kvm-fpr 使用说明\n"
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
        "· 版权：彭刚要，协议：GPLv3。"));
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(text);
    QPushButton *ok = new QPushButton(QStringLiteral("确定"));
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok, 0, Qt::AlignHCenter);
    dlg.exec();
}

/*---------------------------------------------------------------------------*/

void MainWindow::onAbout()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("关于 kvm-fpr"));
    QVBoxLayout *v = new QVBoxLayout(&dlg);

    QHBoxLayout *top = new QHBoxLayout;
    QPixmap pix;
    pix.loadFromData(kIconData, kIconDataSize, "PNG");
    QLabel *icon = new QLabel;
    if (!pix.isNull())
        icon->setPixmap(pix.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    top->addWidget(icon);

    QVBoxLayout *info = new QVBoxLayout;
    QLabel *l = new QLabel(QStringLiteral("kvm-fpr v1.0.0\nKVM 硬件指纹刷新工具"));
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    info->addWidget(l);
    info->addWidget(new QLabel(QStringLiteral("版权：© 2026 彭刚要")));

    QLabel *link = new QLabel;
    link->setOpenExternalLinks(true);
    link->setText(QStringLiteral("<a href=\"mailto:pgy866@163.com\">pgy866@163.com</a>"));
    info->addWidget(link);
    link = new QLabel;
    link->setOpenExternalLinks(true);
    link->setText(QStringLiteral("<a href=\"mailto:yaoying@yaoying.vip\">yaoying@yaoying.vip</a>"));
    info->addWidget(link);
    link = new QLabel;
    link->setOpenExternalLinks(true);
    link->setText(QStringLiteral("<a href=\"http://www.yaoying.vip\">www.yaoying.vip</a>"));
    info->addWidget(link);
    link = new QLabel;
    link->setOpenExternalLinks(true);
    link->setText(QStringLiteral("项目主页：<a href=\"https://github.com/yaoyingkeji/kvm-fpr\">https://github.com/yaoyingkeji/kvm-fpr</a>"));
    info->addWidget(link);
    info->addWidget(new QLabel(QStringLiteral("协议：GPLv3 (GNU General Public License v3)")));
    top->addLayout(info);
    v->addLayout(top);

    QPushButton *ok = new QPushButton(QStringLiteral("确定"));
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok, 0, Qt::AlignHCenter);
    dlg.exec();
}

/*---------------------------------------------------------------------------*/

#include "main_qt.moc"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
