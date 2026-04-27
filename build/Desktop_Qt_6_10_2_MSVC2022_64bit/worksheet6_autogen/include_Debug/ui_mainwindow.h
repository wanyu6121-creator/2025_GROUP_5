/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.10.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "QVTKOpenGLNativeWidget.h"

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionOpen_File;
    QAction *actionItem_Options;
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QTreeView *treeView;
    QVTKOpenGLNativeWidget *widget;
    QHBoxLayout *filterLayout;
    QLabel *labelFilters;
    QCheckBox *checkBoxClip;
    QCheckBox *checkBoxShrink;
    QSpacerItem *filterSpacer;
    QHBoxLayout *buttonLayout;
    QPushButton *pushButton;
    QPushButton *pushButton_2;
    QFrame *line;
    QPushButton *pushButtonStartVR;
    QPushButton *pushButtonStopVR;
    QFrame *line2;
    QPushButton *pushButtonRotate;
    QPushButton *pushButtonResetView;
    QSpacerItem *horizontalSpacer;
    QMenuBar *menubar;
    QMenu *menuFile;
    QStatusBar *statusbar;
    QToolBar *toolBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(960, 640);
        actionOpen_File = new QAction(MainWindow);
        actionOpen_File->setObjectName("actionOpen_File");
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/icon/Icons/fileopen.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        actionOpen_File->setIcon(icon);
        actionOpen_File->setMenuRole(QAction::MenuRole::NoRole);
        actionItem_Options = new QAction(MainWindow);
        actionItem_Options->setObjectName("actionItem_Options");
        actionItem_Options->setMenuRole(QAction::MenuRole::NoRole);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName("verticalLayout");
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        treeView = new QTreeView(centralwidget);
        treeView->setObjectName("treeView");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(treeView->sizePolicy().hasHeightForWidth());
        treeView->setSizePolicy(sizePolicy);
        treeView->setMaximumSize(QSize(200, 16777215));
        treeView->setContextMenuPolicy(Qt::ContextMenuPolicy::ActionsContextMenu);

        horizontalLayout->addWidget(treeView);

        widget = new QVTKOpenGLNativeWidget(centralwidget);
        widget->setObjectName("widget");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy1);

        horizontalLayout->addWidget(widget);


        verticalLayout->addLayout(horizontalLayout);

        filterLayout = new QHBoxLayout();
        filterLayout->setObjectName("filterLayout");
        labelFilters = new QLabel(centralwidget);
        labelFilters->setObjectName("labelFilters");

        filterLayout->addWidget(labelFilters);

        checkBoxClip = new QCheckBox(centralwidget);
        checkBoxClip->setObjectName("checkBoxClip");

        filterLayout->addWidget(checkBoxClip);

        checkBoxShrink = new QCheckBox(centralwidget);
        checkBoxShrink->setObjectName("checkBoxShrink");

        filterLayout->addWidget(checkBoxShrink);

        filterSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        filterLayout->addItem(filterSpacer);


        verticalLayout->addLayout(filterLayout);

        buttonLayout = new QHBoxLayout();
        buttonLayout->setObjectName("buttonLayout");
        pushButton = new QPushButton(centralwidget);
        pushButton->setObjectName("pushButton");

        buttonLayout->addWidget(pushButton);

        pushButton_2 = new QPushButton(centralwidget);
        pushButton_2->setObjectName("pushButton_2");

        buttonLayout->addWidget(pushButton_2);

        line = new QFrame(centralwidget);
        line->setObjectName("line");
        line->setFrameShape(QFrame::Shape::VLine);
        line->setFrameShadow(QFrame::Shadow::Sunken);

        buttonLayout->addWidget(line);

        pushButtonStartVR = new QPushButton(centralwidget);
        pushButtonStartVR->setObjectName("pushButtonStartVR");

        buttonLayout->addWidget(pushButtonStartVR);

        pushButtonStopVR = new QPushButton(centralwidget);
        pushButtonStopVR->setObjectName("pushButtonStopVR");

        buttonLayout->addWidget(pushButtonStopVR);

        line2 = new QFrame(centralwidget);
        line2->setObjectName("line2");
        line2->setFrameShape(QFrame::Shape::VLine);
        line2->setFrameShadow(QFrame::Shadow::Sunken);

        buttonLayout->addWidget(line2);

        pushButtonRotate = new QPushButton(centralwidget);
        pushButtonRotate->setObjectName("pushButtonRotate");

        buttonLayout->addWidget(pushButtonRotate);

        pushButtonResetView = new QPushButton(centralwidget);
        pushButtonResetView->setObjectName("pushButtonResetView");

        buttonLayout->addWidget(pushButtonResetView);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        buttonLayout->addItem(horizontalSpacer);


        verticalLayout->addLayout(buttonLayout);

        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 960, 22));
        menuFile = new QMenu(menubar);
        menuFile->setObjectName("menuFile");
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);
        toolBar = new QToolBar(MainWindow);
        toolBar->setObjectName("toolBar");
        MainWindow->addToolBar(Qt::ToolBarArea::TopToolBarArea, toolBar);

        menubar->addAction(menuFile->menuAction());
        menuFile->addAction(actionOpen_File);
        toolBar->addAction(actionOpen_File);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "EEEE2076 VR Viewer", nullptr));
        actionOpen_File->setText(QCoreApplication::translate("MainWindow", "Open File", nullptr));
        actionItem_Options->setText(QCoreApplication::translate("MainWindow", "Item Options", nullptr));
#if QT_CONFIG(tooltip)
        actionItem_Options->setToolTip(QCoreApplication::translate("MainWindow", "Item Options", nullptr));
#endif // QT_CONFIG(tooltip)
        labelFilters->setText(QCoreApplication::translate("MainWindow", "Filters:", nullptr));
        checkBoxClip->setText(QCoreApplication::translate("MainWindow", "Clip", nullptr));
        checkBoxShrink->setText(QCoreApplication::translate("MainWindow", "Shrink", nullptr));
        pushButton->setText(QCoreApplication::translate("MainWindow", "Add Item", nullptr));
        pushButton_2->setText(QCoreApplication::translate("MainWindow", "Item Options", nullptr));
        pushButtonStartVR->setText(QCoreApplication::translate("MainWindow", "Start VR", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonStartVR->setToolTip(QCoreApplication::translate("MainWindow", "Start VR rendering on HTC Vive", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonStopVR->setText(QCoreApplication::translate("MainWindow", "Stop VR", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonStopVR->setToolTip(QCoreApplication::translate("MainWindow", "Stop VR rendering", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonRotate->setText(QCoreApplication::translate("MainWindow", "Start Rotate", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonRotate->setToolTip(QCoreApplication::translate("MainWindow", "Toggle model rotation in VR", nullptr));
#endif // QT_CONFIG(tooltip)
        pushButtonResetView->setText(QCoreApplication::translate("MainWindow", "Reset View", nullptr));
#if QT_CONFIG(tooltip)
        pushButtonResetView->setToolTip(QCoreApplication::translate("MainWindow", "Reset VR camera to initial position", nullptr));
#endif // QT_CONFIG(tooltip)
        menuFile->setTitle(QCoreApplication::translate("MainWindow", "File", nullptr));
        toolBar->setWindowTitle(QCoreApplication::translate("MainWindow", "toolBar", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
