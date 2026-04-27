#include "autodrawdialog.h"
#include "ui_autodrawdialog.h"

#include <qf/gui/dialogs/dialog.h>
#include <qf/gui/dialogbuttonbox.h>

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableWidgetItem>

using namespace drawing;

AutoDrawDialog::AutoDrawDialog(QWidget *parent)
    : Super(parent), ui(new Ui::AutoDrawDialog)
{
    setTitle(tr("Auto Draw Configuration"));
    setPersistentSettingsId("AutoDrawDialog");
    ui->setupUi(this);

    ui->tblRules->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->tblRules->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->tblRules->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(ui->btnAddRule,    &QPushButton::clicked, this, &AutoDrawDialog::onAddRule);
    connect(ui->btnRemoveRule, &QPushButton::clicked, this, &AutoDrawDialog::onRemoveRule);
    connect(ui->btnMoveUp,     &QPushButton::clicked, this, &AutoDrawDialog::onMoveUp);
    connect(ui->btnMoveDown,   &QPushButton::clicked, this, &AutoDrawDialog::onMoveDown);
    connect(ui->btnDefaults,   &QPushButton::clicked, this, &AutoDrawDialog::onResetDefaults);

    loadConfig(AutoDrawConfig::fromSettings());
}

AutoDrawDialog::~AutoDrawDialog()
{
    delete ui;
}

void AutoDrawDialog::settleDownInDialog(qf::gui::dialogs::Dialog *dlg)
{
    dlg->setButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *okBtn = dlg->buttonBox()->button(QDialogButtonBox::Ok);
    if (okBtn)
        okBtn->setText(tr("Run"));
}

AutoDrawConfig AutoDrawDialog::config() const
{
    AutoDrawConfig cfg;
    cfg.gapFactor         = ui->spnGapFactor->value();
    cfg.maxExpandInterval = ui->spnMaxExpand->value();

    for (int row = 0; row < ui->tblRules->rowCount(); row++) {
        IntervalRule rule;
        QString names = ui->tblRules->item(row, 0)->text().trimmed();
        if (!names.isEmpty())
            rule.classNames = names.split(",", Qt::SkipEmptyParts);
        for (QString &n : rule.classNames)
            n = n.trimmed();
        rule.maxRunners  = ui->tblRules->item(row, 1)->text().toInt();
        rule.minInterval = ui->tblRules->item(row, 2)->text().toInt();
        cfg.intervalRules << rule;
    }
    return cfg;
}

void AutoDrawDialog::loadConfig(const AutoDrawConfig &cfg)
{
    ui->spnGapFactor->setValue(cfg.gapFactor);
    ui->spnMaxExpand->setValue(cfg.maxExpandInterval);

    ui->tblRules->setRowCount(0);
    for (const IntervalRule &rule : cfg.intervalRules)
        appendRuleRow(rule);
}

void AutoDrawDialog::appendRuleRow(const IntervalRule &rule)
{
    int row = ui->tblRules->rowCount();
    ui->tblRules->insertRow(row);
    ui->tblRules->setItem(row, 0, new QTableWidgetItem(rule.classNames.join(", ")));
    ui->tblRules->setItem(row, 1, new QTableWidgetItem(QString::number(rule.maxRunners)));
    ui->tblRules->setItem(row, 2, new QTableWidgetItem(QString::number(rule.minInterval)));
}

void AutoDrawDialog::swapRows(int row1, int row2)
{
    if (row1 < 0 || row2 < 0 || row1 >= ui->tblRules->rowCount()
            || row2 >= ui->tblRules->rowCount())
        return;
    for (int col = 0; col < ui->tblRules->columnCount(); col++) {
        auto *item1 = ui->tblRules->takeItem(row1, col);
        auto *item2 = ui->tblRules->takeItem(row2, col);
        ui->tblRules->setItem(row1, col, item2);
        ui->tblRules->setItem(row2, col, item1);
    }
}

void AutoDrawDialog::onAddRule()
{
    IntervalRule rule;
    rule.minInterval = 2;
    appendRuleRow(rule);
    ui->tblRules->selectRow(ui->tblRules->rowCount() - 1);
}

void AutoDrawDialog::onRemoveRule()
{
    int row = ui->tblRules->currentRow();
    if (row >= 0)
        ui->tblRules->removeRow(row);
}

void AutoDrawDialog::onMoveUp()
{
    int row = ui->tblRules->currentRow();
    if (row > 0) {
        swapRows(row, row - 1);
        ui->tblRules->selectRow(row - 1);
    }
}

void AutoDrawDialog::onMoveDown()
{
    int row = ui->tblRules->currentRow();
    if (row >= 0 && row < ui->tblRules->rowCount() - 1) {
        swapRows(row, row + 1);
        ui->tblRules->selectRow(row + 1);
    }
}

void AutoDrawDialog::onResetDefaults()
{
    loadConfig(AutoDrawConfig::defaults());
}
