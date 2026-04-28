#include "optiondialog.h"
#include "ui_optiondialog.h"

OptionDialog::OptionDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::OptionDialog)
{
    ui->setupUi(this);
}

OptionDialog::~OptionDialog()
{
    delete ui;
}

// optiondialog.cpp
void OptionDialog::setInitialData(const QString &name, int r, int g, int b, bool visible) {
    ui->nameEdit->setText(name);
    ui->rspinBox->setValue(r);
    ui->gspinBox->setValue(g);
    ui->bspinBox->setValue(b);
    ui->visibleCheckBox->setChecked(visible);
}

QString OptionDialog::getName() const { return ui->nameEdit->text(); }
int OptionDialog::getR() const { return ui->rspinBox->value(); }
int OptionDialog::getG() const { return ui->gspinBox->value(); }
int OptionDialog::getB() const { return ui->bspinBox->value(); }
bool OptionDialog::getIsVisible() const { return ui->visibleCheckBox->isChecked(); }
