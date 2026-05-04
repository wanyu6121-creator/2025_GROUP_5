/**  @file optiondialog.cpp
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   实现条目选项对话框。
 *   Implements the item-options dialog.
 *
 *   颜色编辑支持两种互补方式:
 *   Colour editing works in two complementary ways:
 *     1. 手动输入 - 直接在 R/G/B 微调框中输入数值。
 *        Manual entry - type values directly into the R/G/B spin boxes.
 *     2. 颜色选择器 - 点击 "Pick Colour..." 打开系统 QColorDialog。
 *        Colour picker - click "Pick Colour..." to open the system QColorDialog.
 *        选中的颜色会自动写回三个微调框。
 *        The chosen colour is written back into all three spin boxes automatically.
 *   小型颜色预览标签始终显示当前 RGB 值,方便用户即时确认。
 *   A small colour-swatch label always shows the current RGB value for instant feedback.
 */

#include "optiondialog.h"
#include "ui_optiondialog.h"

#include <QColorDialog>
#include <QColor>

/* ---- 构造函数 ------------------------------------------------------------
 *      Constructor -------------------------------------------------------- */

OptionDialog::OptionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OptionDialog)
{
    ui->setupUi(this);

    /* 与 MainWindow 保持一致的深色主题。
     * Deep-dark theme consistent with MainWindow. */
    this->setStyleSheet(
        "QDialog {"
        "  background-color: #111827;"
        "  color: #e2e8f0;"
        "  font-family: 'Segoe UI', sans-serif;"
        "  font-size: 13px;"
        "}"
        "QLabel {"
        "  color: #64748b; background: transparent;"
        "  font-size: 11px; font-weight: 600;"
        "}"
        "QLineEdit {"
        "  background: #0a0d1a; color: #e2e8f0;"
        "  border: 1px solid rgba(99,179,237,0.2);"
        "  border-radius: 5px; padding: 5px 9px; font-size: 13px;"
        "  selection-background-color: rgba(59,130,246,0.35);"
        "}"
        "QLineEdit:focus { border-color: rgba(59,130,246,0.55); }"
        "QLineEdit:hover { border-color: rgba(99,179,237,0.35); }"
        "QSpinBox {"
        "  background: #0a0d1a; color: #e2e8f0;"
        "  border: 1px solid rgba(99,179,237,0.2);"
        "  border-radius: 5px; padding: 4px 8px;"
        "  font-size: 13px; font-family: 'Consolas',monospace; min-width: 70px;"
        "}"
        "QSpinBox:focus { border-color: rgba(59,130,246,0.55); }"
        "QSpinBox:hover { border-color: rgba(99,179,237,0.35); }"
        "QSpinBox::up-button, QSpinBox::down-button {"
        "  background: rgba(99,179,237,0.08); border: none; width: 18px;"
        "}"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover {"
        "  background: rgba(59,130,246,0.2);"
        "}"
        "QCheckBox {"
        "  color: #e2e8f0; background: transparent;"
        "  font-size: 13px; spacing: 7px; border: none;"
        "}"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px;"
        "  border: 1px solid rgba(99,179,237,0.3);"
        "  border-radius: 4px; background: #0a0d1a;"
        "}"
        "QCheckBox::indicator:hover { border-color: rgba(59,130,246,0.55); }"
        "QCheckBox::indicator:checked {"
        "  background: rgba(59,130,246,0.35); border-color: #3b82f6;"
        "}"
        /* Pick Colour 按钮。
         * Pick Colour button. */
        "QPushButton#pickColourButton {"
        "  background: transparent; color: #60a5fa;"
        "  border: 1px solid rgba(59,130,246,0.4);"
        "  border-radius: 5px; padding: 4px 12px;"
        "  font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#pickColourButton:hover {"
        "  background: rgba(59,130,246,0.18); border-color: rgba(59,130,246,0.65);"
        "}"
        "QPushButton#pickColourButton:pressed { background: rgba(59,130,246,0.30); }"
        /* OK 和 Cancel 按钮。
         * OK and Cancel buttons. */
        "QDialogButtonBox QPushButton {"
        "  background: transparent; color: #64748b;"
        "  border: 1px solid rgba(99,179,237,0.2);"
        "  border-radius: 5px; padding: 5px 18px;"
        "  font-size: 12px; font-weight: 600; min-width: 64px;"
        "}"
        "QDialogButtonBox QPushButton:hover {"
        "  background: #1a2436; color: #e2e8f0;"
        "  border-color: rgba(99,179,237,0.35);"
        "}"
        "QDialogButtonBox QPushButton[text='OK'] {"
        "  background: rgba(59,130,246,0.18); color: #60a5fa;"
        "  border-color: rgba(59,130,246,0.4);"
        "}"
        "QDialogButtonBox QPushButton[text='OK']:hover {"
        "  background: rgba(59,130,246,0.30);"
        "}"
    );

    /* 为每个颜色通道设置不同色调,方便用户识别 R/G/B。
     * Per-channel tint so the user can visually identify which is R/G/B. */
    ui->rspinBox->setStyleSheet("color:#f87171;");
    ui->gspinBox->setStyleSheet("color:#34d399;");
    ui->bspinBox->setStyleSheet("color:#60a5fa;");

    /* 连接颜色选择按钮。
     * Connect the pick-colour button. */
    connect(ui->pickColourButton, &QPushButton::clicked,
            this, &OptionDialog::onPickColour);

    /* 连接三个微调框,保持颜色预览同步。
     * Connect all three spin boxes so the swatch stays in sync. */
    connect(ui->rspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
    connect(ui->gspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
    connect(ui->bspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
}

/* ---- 析构函数 ------------------------------------------------------------
 *      Destructor --------------------------------------------------------- */

OptionDialog::~OptionDialog()
{
    delete ui;
}

/* ---- 公共接口 ------------------------------------------------------------
 *      Public interface --------------------------------------------------- */

void OptionDialog::setInitialData(const QString &name,
                                  int r, int g, int b, bool visible)
{
    ui->nameEdit->setText(name);
    ui->rspinBox->setValue(r);
    ui->gspinBox->setValue(g);
    ui->bspinBox->setValue(b);
    ui->visibleCheckBox->setChecked(visible);
    updateSwatch(r, g, b);   /* 使用初始颜色绘制预览色块。
                              * Paint the swatch with the initial colour. */
}

QString OptionDialog::getName()       const { return ui->nameEdit->text();           }
int     OptionDialog::getR()          const { return ui->rspinBox->value();           }
int     OptionDialog::getG()          const { return ui->gspinBox->value();           }
int     OptionDialog::getB()          const { return ui->bspinBox->value();           }
bool    OptionDialog::getIsVisible()  const { return ui->visibleCheckBox->isChecked();}

/* ---- 私有槽函数 ----------------------------------------------------------
 *      Private slots ------------------------------------------------------ */

void OptionDialog::onPickColour()
{
    /* 使用微调框中的当前值初始化颜色对话框。
     * Initialise the colour dialog with whatever is currently in the spin boxes. */
    QColor initial(ui->rspinBox->value(),
                   ui->gspinBox->value(),
                   ui->bspinBox->value());

    /* QColorDialog::getColor 会阻塞,直到用户关闭对话框。
     * QColorDialog::getColor blocks until the user closes the dialog.
     * 传入 'this' 可使其居中显示在 OptionDialog 上方。
     * Passing 'this' keeps it centred over the OptionDialog.
     * 这里故意省略 ShowAlphaChannel,因为只使用 RGB。
     * ShowAlphaChannel is intentionally omitted because we use RGB only.
     */
    QColor chosen = QColorDialog::getColor(initial, this, tr("Pick Part Colour"));

    if (!chosen.isValid())
        return;   /* 用户按下 Cancel,保持微调框不变。
                   * User pressed Cancel, so leave spin boxes unchanged. */

    /* 将选中的值写回; valueChanged 信号会触发。
     * Write the chosen values back; the valueChanged signals will fire.
     * 信号会调用 onSpinBoxChanged(),从而自动刷新色块。
     * The signals call onSpinBoxChanged(), which refreshes the swatch automatically.
     */
    ui->rspinBox->setValue(chosen.red());
    ui->gspinBox->setValue(chosen.green());
    ui->bspinBox->setValue(chosen.blue());
}

void OptionDialog::onSpinBoxChanged()
{
    updateSwatch(ui->rspinBox->value(),
                 ui->gspinBox->value(),
                 ui->bspinBox->value());
}

/* ---- 私有辅助函数 --------------------------------------------------------
 *      Private helpers ---------------------------------------------------- */

void OptionDialog::updateSwatch(int r, int g, int b)
{
    /* 将标签背景设置为当前颜色。
     * Set the label background to the current colour.
     * 始终显示细边框,确保非常暗的颜色也能看见。
     * A thin border is always shown so the swatch is visible even for very dark colours.
     * 例如深色背景上的纯黑色。
     * For example, pure black on a dark background.
     */
    ui->colourSwatch->setStyleSheet(
        QString("background-color: rgb(%1,%2,%3);"
                "border: 1px solid rgba(255,255,255,0.25);"
                "border-radius: 3px;")
        .arg(r).arg(g).arg(b)
    );
}
