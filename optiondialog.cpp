/**  @file optiondialog.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Implements the item-options dialog.
 *
 *   Colour editing works in two complementary ways:
 *     1. Manual entry  – type values directly into the R/G/B spin boxes.
 *     2. Colour picker – click "Pick Colour..." to open the system
 *                        QColorDialog; the chosen colour is written back
 *                        into all three spin boxes automatically.
 *   A small colour-swatch label always shows the current RGB value so
 *   the user gets instant visual feedback without having to close the dialog.
 */

#include "optiondialog.h"
#include "ui_optiondialog.h"

#include <QColorDialog>
#include <QColor>

/* ---- Constructor -------------------------------------------------------- */

OptionDialog::OptionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OptionDialog)
{
    ui->setupUi(this);

    /* Deep-dark theme consistent with MainWindow */
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
        /* Pick Colour button */
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
        /* OK / Cancel */
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

    /* Per-channel tint so the user can visually identify which is R/G/B */
    ui->rspinBox->setStyleSheet("color:#f87171;");
    ui->gspinBox->setStyleSheet("color:#34d399;");
    ui->bspinBox->setStyleSheet("color:#60a5fa;");

    /* Connect pick-colour button */
    connect(ui->pickColourButton, &QPushButton::clicked,
            this, &OptionDialog::onPickColour);

    /* Connect all three spin boxes so the swatch stays in sync */
    connect(ui->rspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
    connect(ui->gspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
    connect(ui->bspinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OptionDialog::onSpinBoxChanged);
}

/* ---- Destructor --------------------------------------------------------- */

OptionDialog::~OptionDialog()
{
    delete ui;
}

/* ---- Public interface --------------------------------------------------- */

void OptionDialog::setInitialData(const QString &name,
                                  int r, int g, int b, bool visible)
{
    ui->nameEdit->setText(name);
    ui->rspinBox->setValue(r);
    ui->gspinBox->setValue(g);
    ui->bspinBox->setValue(b);
    ui->visibleCheckBox->setChecked(visible);
    updateSwatch(r, g, b);   /* paint the swatch with the initial colour */
}

QString OptionDialog::getName()       const { return ui->nameEdit->text();           }
int     OptionDialog::getR()          const { return ui->rspinBox->value();           }
int     OptionDialog::getG()          const { return ui->gspinBox->value();           }
int     OptionDialog::getB()          const { return ui->bspinBox->value();           }
bool    OptionDialog::getIsVisible()  const { return ui->visibleCheckBox->isChecked();}

/* ---- Private slots ------------------------------------------------------ */

void OptionDialog::onPickColour()
{
    /* Initialise the colour dialog with whatever is currently in the spin boxes */
    QColor initial(ui->rspinBox->value(),
                   ui->gspinBox->value(),
                   ui->bspinBox->value());

    /* QColorDialog::getColor blocks until the user closes the dialog.
     * Passing 'this' keeps it centred over the OptionDialog.
     * ShowAlphaChannel is intentionally omitted — we use RGB only. */
    QColor chosen = QColorDialog::getColor(initial, this, tr("Pick Part Colour"));

    if (!chosen.isValid())
        return;   /* user pressed Cancel — leave spin boxes unchanged */

    /* Write the chosen values back; the valueChanged signals will fire
     * and call onSpinBoxChanged(), which refreshes the swatch automatically. */
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

/* ---- Private helpers ---------------------------------------------------- */

void OptionDialog::updateSwatch(int r, int g, int b)
{
    /* Set the label background to the current colour.
     * A thin border is always shown so the swatch is visible even for very
     * dark colours (e.g. pure black on a dark background). */
    ui->colourSwatch->setStyleSheet(
        QString("background-color: rgb(%1,%2,%3);"
                "border: 1px solid rgba(255,255,255,0.25);"
                "border-radius: 3px;")
        .arg(r).arg(g).arg(b)
    );
}
