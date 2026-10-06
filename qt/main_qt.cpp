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
 * 构建：QT_SELECT=qt5 qmake main_qt.pro && QT_SELECT=qt5 make
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
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QDialog>
#include <QPixmap>
#include <QFont>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

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
    void onRegen();
    void onApply();
    void onComboChanged(int index);
    void onTplChanged(int index);
    void onBackups();
    void onViewXml();
    void onHelp();
    void onAbout();

private:
    void loadItems();

    QComboBox *m_combo;
    QLineEdit *m_pass;
    QCheckBox *m_chkForce;
    QCheckBox *m_chkGuest;
    QLabel *m_state;
    QTextEdit *m_log;
    QTableWidget *m_table;
    char **m_names;
    int m_count;
    FprItem **m_items;
    int m_itemCount;
    QComboBox *m_tpl;
    FprTemplate **m_tpls;
    int m_tplCount;
    QCheckBox *m_chkKvm;
    QCheckBox *m_chkHypervisor;
    QCheckBox *m_chkVmport;
    QCheckBox *m_chkHvVendor;
    QLineEdit *m_hvVendor;
    char **m_bkPaths;
    char **m_bkTimes;
    int m_bkCount;
    int m_bkVmIndex;
};

/*---------------------------------------------------------------------------*/

static void logCallback(void *ctx, const char *msg)
{
    MainWindow *w = static_cast<MainWindow *>(ctx);
    w->appendLog("%s", msg);
}

/*---------------------------------------------------------------------------*/

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), m_names(nullptr), m_count(0),
      m_items(nullptr), m_itemCount(0), m_tpls(nullptr), m_tplCount(0),
      m_bkPaths(nullptr), m_bkTimes(nullptr), m_bkCount(0), m_bkVmIndex(0)
{
    setWindowTitle(QStringLiteral("KVM 硬件指纹刷新工具"));
    resize(740, 640);

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
    grid->addWidget(m_state, 0, 3);
    root->addLayout(grid);

    /* 选项行 */
    grid = new QGridLayout;
    m_chkForce = new QCheckBox(QStringLiteral("运行中则强制关机"));
    m_chkForce->setChecked(true);
    grid->addWidget(m_chkForce, 0, 0);
    m_chkGuest = new QCheckBox(QStringLiteral("重置 guest 内部 machine-id"));
    grid->addWidget(m_chkGuest, 0, 1);
    root->addLayout(grid);

    /* 品牌模板行 */
    grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    grid->addWidget(new QLabel(QStringLiteral("品牌模板：")), 0, 0);
    m_tpl = new QComboBox;
    grid->addWidget(m_tpl, 0, 1);
    root->addLayout(grid);

    /* 防虚拟机检测行 */
    grid = new QGridLayout;
    grid->setColumnStretch(4, 1);
    m_chkKvm = new QCheckBox(QStringLiteral("隐藏 KVM 标志"));
    grid->addWidget(m_chkKvm, 0, 0);
    m_chkHypervisor = new QCheckBox(QStringLiteral("隐藏 hypervisor CPU 标志"));
    grid->addWidget(m_chkHypervisor, 0, 1);
    m_chkVmport = new QCheckBox(QStringLiteral("禁用 VMWare 端口"));
    grid->addWidget(m_chkVmport, 0, 2);
    m_chkHvVendor = new QCheckBox(QStringLiteral("HyperV 厂商 ID"));
    grid->addWidget(m_chkHvVendor, 0, 3);
    m_hvVendor = new QLineEdit;
    m_hvVendor->setPlaceholderText(QStringLiteral("如 Microsofit（≤12 字符）"));
    grid->addWidget(m_hvVendor, 0, 4);
    root->addLayout(grid);

    /* 按钮行：应用新指纹 | 重新随机 | 备份历史 | 使用说明 | 关于 */
    grid = new QGridLayout;
    QPushButton *btnApply = new QPushButton(QStringLiteral("应用新指纹"));
    connect(btnApply, &QPushButton::clicked, this, &MainWindow::onApply);
    grid->addWidget(btnApply, 0, 0);
    QPushButton *btnRegen = new QPushButton(QStringLiteral("重新随机"));
    connect(btnRegen, &QPushButton::clicked, this, &MainWindow::onRegen);
    grid->addWidget(btnRegen, 0, 1);
    QPushButton *btnBackups = new QPushButton(QStringLiteral("备份历史"));
    connect(btnBackups, &QPushButton::clicked, this, &MainWindow::onBackups);
    grid->addWidget(btnBackups, 0, 2);
    QPushButton *btnView = new QPushButton(QStringLiteral("查看文件"));
    connect(btnView, &QPushButton::clicked, this, &MainWindow::onViewXml);
    grid->addWidget(btnView, 0, 3);
    QPushButton *btnHelp = new QPushButton(QStringLiteral("使用说明"));
    connect(btnHelp, &QPushButton::clicked, this, &MainWindow::onHelp);
    grid->addWidget(btnHelp, 0, 4);
    QPushButton *btnAbout = new QPushButton(QStringLiteral("关于"));
    connect(btnAbout, &QPushButton::clicked, this, &MainWindow::onAbout);
    grid->addWidget(btnAbout, 0, 5);
    root->addLayout(grid);

    /* 指纹表格 */
    m_table = new QTableWidget(0, 3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("硬件指纹项"),
         QStringLiteral("当前值（只读）"),
         QStringLiteral("新值（自动随机生成，可手动修改）")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setMaximumHeight(360);
    root->addWidget(m_table);

    /* 日志区 */
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    root->addWidget(m_log, 1);

    connect(m_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onComboChanged);
    connect(m_tpl, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &MainWindow::onTplChanged);

    /* 填充品牌模板 */
    fpr_list_templates(&m_tpls, &m_tplCount);
    for (int i = 0; i < m_tplCount; ++i)
        m_tpl->addItem(QString::fromUtf8(m_tpls[i]->name));
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
    loadItems();
}

/*---------------------------------------------------------------------------*/

void MainWindow::onComboChanged(int index)
{
    Q_UNUSED(index);
    loadItems();
}

/*---------------------------------------------------------------------------*/

void MainWindow::onTplChanged(int index)
{
    if (m_items == nullptr || m_itemCount <= 0)
        return;
    if (index < 0 || index >= m_tplCount)
        return;
    fpr_apply_template(m_items, m_itemCount, m_tpls[index]);
    for (int i = 0; i < m_itemCount && m_items[i] != nullptr; ++i)
    {
        QTableWidgetItem *item = m_table->item(i, 2);
        if (item != nullptr)
            item->setText(QString::fromUtf8(m_items[i]->newval));
    }
    appendLog(">> 已应用品牌模板：%s", m_tpls[index]->name);
}

/*---------------------------------------------------------------------------*/

void MainWindow::onBackups()
{
    int sel = m_combo->currentIndex();
    if (sel < 0 || sel >= m_count)
    {
        appendLog("[错误] 请先选择虚拟机");
        return;
    }
    if (m_bkPaths != nullptr)
    {
        fpr_free_backups(m_bkPaths, m_bkTimes, m_bkCount);
        m_bkPaths = nullptr;
        m_bkTimes = nullptr;
        m_bkCount = 0;
    }
    m_bkVmIndex = sel;
    fpr_list_backups(m_names[sel], &m_bkPaths, &m_bkTimes, &m_bkCount);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("备份历史"));
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QComboBox *combo = new QComboBox;
    for (int i = 0; i < m_bkCount; ++i)
        combo->addItem(QString::fromUtf8(m_bkTimes[i]));
    v->addWidget(combo);

    QHBoxLayout *hb = new QHBoxLayout;
    QPushButton *btnRestore = new QPushButton(QStringLiteral("恢复所选备份"));
    hb->addWidget(btnRestore);
    QPushButton *btnDelete = new QPushButton(QStringLiteral("删除所选备份"));
    hb->addWidget(btnDelete);
    v->addLayout(hb);

    QPushButton *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(btnClose, 0, Qt::AlignHCenter);

    connect(btnRestore, &QPushButton::clicked, [this, combo, &dlg]() {
        int idx = combo->currentIndex();
        if (idx < 0 || idx >= m_bkCount)
        {
            appendLog("[错误] 请先选择一条备份");
            return;
        }
        appendLog("======================================================");
        if (fpr_restore_backup(m_pass->text().toUtf8().constData(),
                               m_names[m_bkVmIndex], m_bkPaths[idx],
                               logCallback, this) == 0)
            appendLog(">> 恢复成功，可关闭对话框后重新加载指纹");
    });
    connect(btnDelete, &QPushButton::clicked, [this, combo]() {
        int idx = combo->currentIndex();
        if (idx < 0 || idx >= m_bkCount)
        {
            appendLog("[错误] 请先选择一条备份");
            return;
        }
        if (fpr_delete_backup(m_bkPaths[idx]) == 0)
            appendLog(">> 已删除备份：%s", m_bkTimes[idx]);
        else
            appendLog("[错误] 删除备份失败");
    });
    dlg.exec();
}

/*---------------------------------------------------------------------------*/

void MainWindow::loadItems()
{
    int sel = m_combo->currentIndex();
    if (sel < 0 || sel >= m_count)
        return;

    if (m_items != nullptr)
    {
        fpr_free_items(m_items, m_itemCount);
        m_items = nullptr;
        m_itemCount = 0;
    }

    appendLog(">> 正在读取虚拟机 %s 的硬件指纹 ...", m_names[sel]);
    if (fpr_get_items(m_pass->text().toUtf8().constData(), m_names[sel],
                      &m_items, &m_itemCount, logCallback, this) != 0)
        return;

    m_table->setRowCount(m_itemCount);
    for (int i = 0; i < m_itemCount && m_items[i] != nullptr; ++i)
    {
        QTableWidgetItem *nameItem = new QTableWidgetItem(QString::fromUtf8(m_items[i]->name));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, nameItem);

        QTableWidgetItem *curItem = new QTableWidgetItem(QString::fromUtf8(m_items[i]->current));
        curItem->setFlags(curItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, curItem);

        QTableWidgetItem *newItem = new QTableWidgetItem(QString::fromUtf8(m_items[i]->newval));
        newItem->setFlags(newItem->flags() | Qt::ItemIsEditable);
        m_table->setItem(i, 2, newItem);
    }
    appendLog(">> 已生成 %d 项硬件指纹（右侧可手动修改）", m_itemCount);
}

/*---------------------------------------------------------------------------*/

void MainWindow::onRegen()
{
    if (m_items == nullptr || m_itemCount <= 0)
    {
        appendLog("[提示] 请先选择虚拟机并加载指纹");
        return;
    }
    fpr_regen_values(m_items, m_itemCount);
    for (int i = 0; i < m_itemCount && m_items[i] != nullptr; ++i)
    {
        QTableWidgetItem *item = m_table->item(i, 2);
        if (item != nullptr)
            item->setText(QString::fromUtf8(m_items[i]->newval));
    }
    appendLog(">> 已重新随机生成 %d 项新指纹", m_itemCount);
}

/*---------------------------------------------------------------------------*/

void MainWindow::onApply()
{
    int sel = m_combo->currentIndex();
    if (sel < 0 || sel >= m_count)
    {
        appendLog("[错误] 请先选择一台虚拟机");
        return;
    }
    if (m_items == nullptr || m_itemCount <= 0)
    {
        appendLog("[错误] 请先点击「刷新列表」加载指纹");
        return;
    }

    for (int i = 0; i < m_itemCount && m_items[i] != nullptr; ++i)
    {
        QTableWidgetItem *item = m_table->item(i, 2);
        const char *txt = (item != nullptr)
            ? item->text().toUtf8().constData() : "";
        if (txt == nullptr || txt[0] == '\0')
        {
            appendLog("[错误] 第 %d 项新值为空，请填写或点击「重新随机」", i + 1);
            return;
        }
        free(m_items[i]->newval);
        m_items[i]->newval = strdup(txt);
    }

    unsigned int avoid = 0;
    const char *hv = nullptr;
    if (m_chkKvm->isChecked()) avoid |= FPR_AVOID_KVM_HIDDEN;
    if (m_chkHypervisor->isChecked()) avoid |= FPR_AVOID_HYPERVISOR;
    if (m_chkVmport->isChecked()) avoid |= FPR_AVOID_VMPORT;
    if (m_chkHvVendor->isChecked())
    {
        QByteArray ba = m_hvVendor->text().toUtf8();
        hv = ba.constData();
        if (hv == nullptr || hv[0] == '\0')
        {
            appendLog("[错误] 已勾选 HyperV 厂商 ID，请填写厂商 ID（≤12 字符）");
            return;
        }
        avoid |= FPR_AVOID_HYPERV_VENDOR;
    }
    appendLog("======================================================");
    appendLog(">> 开始应用新指纹：%s", m_names[sel]);
    int rc = fpr_apply_items_ext(m_pass->text().toUtf8().constData(), m_names[sel],
                                 m_items, m_itemCount, avoid, hv,
                                 m_chkForce->isChecked() ? 1 : 0,
                                 m_chkGuest->isChecked() ? 1 : 0,
                                 logCallback, this);
    if (rc == 0)
    {
        appendLog(">> 应用成功！");
        loadItems();
    }
    else
    {
        appendLog(">> 应用失败，请查看上方错误信息");
    }
}

/*---------------------------------------------------------------------------*/

void MainWindow::onViewXml()
{
    int sel = m_combo->currentIndex();
    if (sel < 0 || sel >= m_count)
    {
        appendLog("[错误] 请先选择虚拟机");
        return;
    }
    char *xml = nullptr;
    if (fpr_dumpxml(m_pass->text().toUtf8().constData(), m_names[sel],
                    &xml, logCallback, this) != 0)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("查看 / 编辑 XML 配置"));
    dlg.resize(700, 560);
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QPlainTextEdit *editor = new QPlainTextEdit;
    editor->setPlainText(QString::fromUtf8(xml != nullptr ? xml : ""));
    free(xml);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    v->addWidget(editor);

    QHBoxLayout *hb = new QHBoxLayout;
    QPushButton *btnSave = new QPushButton(QStringLiteral("保存并应用"));
    hb->addWidget(btnSave);
    QPushButton *btnClose = new QPushButton(QStringLiteral("关闭"));
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    hb->addWidget(btnClose, 0, Qt::AlignRight);
    v->addLayout(hb);

    connect(btnSave, &QPushButton::clicked, [this, editor, sel]() {
        QByteArray ba = editor->toPlainText().toUtf8();
        appendLog("======================================================");
        if (fpr_apply_xml(m_pass->text().toUtf8().constData(), m_names[sel],
                          ba.constData(), logCallback, this) == 0)
            appendLog(">> 修改已应用，请重新加载指纹");
        else
            appendLog(">> 应用失败，请查看上方错误信息");
    });
    dlg.exec();
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
    QLabel *l = new QLabel(QStringLiteral("kvm-fpr v1.1.0\nKVM 硬件指纹刷新工具"));
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
