/********************************************************************************
** Form generated from reading UI file 'optiondialog.ui'
**
** Created by: Qt User Interface Compiler version 6.10.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_OPTIONDIALOG_H
#define UI_OPTIONDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_OptionDialog
{
public:
    QWidget *widget;
    QFormLayout *formLayout;
    QLabel *label;
    QLabel *label_2;
    QSpinBox *rspinBox;
    QLabel *label_3;
    QSpinBox *gspinBox;
    QLabel *label_4;
    QSpinBox *bspinBox;
    QLabel *label_5;
    QCheckBox *visibleCheckBox;
    QDialogButtonBox *buttonBox;
    QLineEdit *nameEdit;

    void setupUi(QDialog *OptionDialog)
    {
        if (OptionDialog->objectName().isEmpty())
            OptionDialog->setObjectName("OptionDialog");
        OptionDialog->resize(400, 300);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(OptionDialog->sizePolicy().hasHeightForWidth());
        OptionDialog->setSizePolicy(sizePolicy);
        widget = new QWidget(OptionDialog);
        widget->setObjectName("widget");
        widget->setGeometry(QRect(30, 50, 310, 265));
        formLayout = new QFormLayout(widget);
        formLayout->setObjectName("formLayout");
        formLayout->setHorizontalSpacing(28);
        formLayout->setVerticalSpacing(17);
        formLayout->setContentsMargins(0, 0, 50, 14);
        label = new QLabel(widget);
        label->setObjectName("label");

        formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label);

        label_2 = new QLabel(widget);
        label_2->setObjectName("label_2");

        formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_2);

        rspinBox = new QSpinBox(widget);
        rspinBox->setObjectName("rspinBox");
        rspinBox->setMaximum(255);

        formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, rspinBox);

        label_3 = new QLabel(widget);
        label_3->setObjectName("label_3");

        formLayout->setWidget(2, QFormLayout::ItemRole::LabelRole, label_3);

        gspinBox = new QSpinBox(widget);
        gspinBox->setObjectName("gspinBox");
        gspinBox->setMaximum(255);

        formLayout->setWidget(2, QFormLayout::ItemRole::FieldRole, gspinBox);

        label_4 = new QLabel(widget);
        label_4->setObjectName("label_4");

        formLayout->setWidget(3, QFormLayout::ItemRole::LabelRole, label_4);

        bspinBox = new QSpinBox(widget);
        bspinBox->setObjectName("bspinBox");
        bspinBox->setMaximum(255);

        formLayout->setWidget(3, QFormLayout::ItemRole::FieldRole, bspinBox);

        label_5 = new QLabel(widget);
        label_5->setObjectName("label_5");

        formLayout->setWidget(4, QFormLayout::ItemRole::LabelRole, label_5);

        visibleCheckBox = new QCheckBox(widget);
        visibleCheckBox->setObjectName("visibleCheckBox");

        formLayout->setWidget(4, QFormLayout::ItemRole::FieldRole, visibleCheckBox);

        buttonBox = new QDialogButtonBox(widget);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Orientation::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::StandardButton::Cancel|QDialogButtonBox::StandardButton::Ok);

        formLayout->setWidget(5, QFormLayout::ItemRole::SpanningRole, buttonBox);

        nameEdit = new QLineEdit(widget);
        nameEdit->setObjectName("nameEdit");

        formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, nameEdit);


        retranslateUi(OptionDialog);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, OptionDialog, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, OptionDialog, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(OptionDialog);
    } // setupUi

    void retranslateUi(QDialog *OptionDialog)
    {
        OptionDialog->setWindowTitle(QCoreApplication::translate("OptionDialog", "Dialog", nullptr));
        label->setText(QCoreApplication::translate("OptionDialog", "Name", nullptr));
        label_2->setText(QCoreApplication::translate("OptionDialog", "Red", nullptr));
        label_3->setText(QCoreApplication::translate("OptionDialog", "Green", nullptr));
        label_4->setText(QCoreApplication::translate("OptionDialog", "Blue", nullptr));
        label_5->setText(QCoreApplication::translate("OptionDialog", "Viaible", nullptr));
        visibleCheckBox->setText(QCoreApplication::translate("OptionDialog", "isVisible", nullptr));
    } // retranslateUi

};

namespace Ui {
    class OptionDialog: public Ui_OptionDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_OPTIONDIALOG_H
