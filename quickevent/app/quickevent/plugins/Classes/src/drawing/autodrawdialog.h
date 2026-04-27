#ifndef DRAWING_AUTODRAWDIALOG_H
#define DRAWING_AUTODRAWDIALOG_H

#include "autodrawengine.h"

#include <qf/gui/framework/dialogwidget.h>

namespace Ui { class AutoDrawDialog; }

namespace drawing {

class AutoDrawDialog : public qf::gui::framework::DialogWidget
{
    Q_OBJECT
private:
    typedef qf::gui::framework::DialogWidget Super;
public:
    explicit AutoDrawDialog(QWidget *parent = nullptr);
    ~AutoDrawDialog() override;

    AutoDrawConfig config() const;

    void settleDownInDialog(qf::gui::dialogs::Dialog *dlg) Q_DECL_OVERRIDE;

private slots:
    void onAddRule();
    void onRemoveRule();
    void onMoveUp();
    void onMoveDown();
    void onResetDefaults();

private:
    void loadConfig(const AutoDrawConfig &cfg);
    void appendRuleRow(const IntervalRule &rule);
    void swapRows(int row1, int row2);

    ::Ui::AutoDrawDialog *ui;
};

}  // namespace drawing

#endif // DRAWING_AUTODRAWDIALOG_H
