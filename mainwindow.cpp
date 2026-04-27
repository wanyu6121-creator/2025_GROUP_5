#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include "optiondialog.h"

#include <vtkSmartPointer.h>
#include <vtkNew.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkCamera.h>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vrThread(nullptr)
    , isVRRotating(false)
{
    ui->setupUi(this);

    /* ---- 原有信号槽 ---- */
    /* 【修复问题1】Add Item按钮改为触发文件加载，与菜单Open File相同 */
    connect(ui->pushButton,   &QPushButton::released,
            this, &MainWindow::on_actionOpen_File_triggered);
    connect(ui->pushButton_2, &QPushButton::released,
            this, &MainWindow::handleOptionsButton);
    ui->treeView->addAction(ui->actionItem_Options);
    connect(this, &MainWindow::statusUpdateMessage,
            ui->statusbar, &QStatusBar::showMessage);
    connect(ui->treeView, &QTreeView::clicked,
            this, &MainWindow::handleTreeClicked);

    /* ---- 滤镜复选框 ---- */
    connect(ui->checkBoxClip,   &QCheckBox::toggled,
            this, &MainWindow::handleClipToggle);
    connect(ui->checkBoxShrink, &QCheckBox::toggled,
            this, &MainWindow::handleShrinkToggle);

    /* ---- VR控制按钮 ---- */
    connect(ui->pushButtonStartVR,   &QPushButton::released,
            this, &MainWindow::handleStartVR);
    connect(ui->pushButtonStopVR,    &QPushButton::released,
            this, &MainWindow::handleStopVR);
    connect(ui->pushButtonRotate,    &QPushButton::released,
            this, &MainWindow::handleToggleRotate);
    connect(ui->pushButtonResetView, &QPushButton::released,
            this, &MainWindow::handleResetView);

    /* ---- 初始化TreeView ---- */
    this->partList = new ModelPartList("PartsList");
    ui->treeView->setModel(this->partList);

    ModelPart* rootItem = this->partList->getRootItem();
    for (int i = 0; i < 3; i++) {
        QString name    = QString("TopLevel %1").arg(i);
        QString visible = "true";
        ModelPart* childItem = new ModelPart({ name, visible, 255, 255, 255 });
        rootItem->appendChild(childItem);
        for (int j = 0; j < 5; j++) {
            QString subName = QString("Item %1,%2").arg(i).arg(j);
            ModelPart* childChildItem = new ModelPart({ subName, visible, 255, 255, 255 });
            childItem->appendChild(childChildItem);
        }
    }

    /* ---- 初始化VTK渲染器 ---- */
    renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    ui->widget->setRenderWindow(renderWindow);
    renderer = vtkSmartPointer<vtkRenderer>::New();
    renderWindow->AddRenderer(renderer);
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();
}

MainWindow::~MainWindow()
{
    if (vrThread != nullptr && vrThread->isRunning()) {
        vrThread->requestInterruption();
        vrThread->wait(3000);
    }
    delete vrThread;
    delete ui;
}

/* ================================================================
 * VR 启动
 * ================================================================ */
void MainWindow::handleStartVR()
{
    if (vrThread != nullptr && vrThread->isRunning()) {
        emit statusUpdateMessage("VR is already running!", 3000);
        return;
    }

    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    isVRRotating = false;
    ui->pushButtonRotate->setText("Start Rotate");

    vrThread = new VRRenderThread(this);
    populateVRActors();
    vrThread->start();

    emit statusUpdateMessage("VR started! Put on your headset.", 0);
}

/* ================================================================
 * VR 停止
 * ================================================================ */
void MainWindow::handleStopVR()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("VR is not running.", 3000);
        return;
    }

    vrThread->requestInterruption();
    if (!vrThread->wait(5000)) {
        vrThread->terminate();
        vrThread->wait();
    }

    isVRRotating = false;
    ui->pushButtonRotate->setText("Start Rotate");
    emit statusUpdateMessage("VR stopped.", 3000);
}

/* ================================================================
 * 旋转动画开关
 * ================================================================ */
void MainWindow::handleToggleRotate()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000);
        return;
    }

    if (isVRRotating) {
        vrThread->issueCommand(CMD_STOP_ROTATE, 0.0);
        isVRRotating = false;
        ui->pushButtonRotate->setText("Start Rotate");
        emit statusUpdateMessage("VR rotation stopped.", 2000);
    } else {
        vrThread->issueCommand(CMD_START_ROTATE, 0.0);
        isVRRotating = true;
        ui->pushButtonRotate->setText("Stop Rotate");
        emit statusUpdateMessage("VR rotation started.", 2000);
    }
}

/* ================================================================
 * 重置视角
 * ================================================================ */
void MainWindow::handleResetView()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000);
        return;
    }

    vrThread->issueCommand(CMD_RESET_VIEW, 0.0);
    isVRRotating = false;
    ui->pushButtonRotate->setText("Start Rotate");
    emit statusUpdateMessage("VR view reset.", 2000);
}

/* ================================================================
 * 遍历树，为每个已加载STL的零件创建VR Actor
 * ================================================================ */
void MainWindow::populateVRActors()
{
    if (vrThread == nullptr) return;

    int topLevelRows = partList->rowCount(QModelIndex());
    for (int i = 0; i < topLevelRows; i++) {
        populateVRActorsFromTree(partList->index(i, 0, QModelIndex()));
    }
}

void MainWindow::populateVRActorsFromTree(const QModelIndex& index)
{
    if (!index.isValid()) return;

    ModelPart* part = static_cast<ModelPart*>(index.internalPointer());

    vtkActor* vrActor = part->getNewActor();
    if (vrActor != nullptr) {
        vrThread->addActorOffline(vrActor);
    }

    if (partList->hasChildren(index)) {
        int rows = partList->rowCount(index);
        for (int i = 0; i < rows; i++) {
            populateVRActorsFromTree(partList->index(i, 0, index));
        }
    }
}

/* ================================================================
 * 原有函数
 * ================================================================ */

void MainWindow::handleButton()
{
    /* 此函数保留但不再使用，Add Item按钮已直接连接到
     * on_actionOpen_File_triggered */
    emit statusUpdateMessage("Add button was clicked", 0);
}

void MainWindow::handleTreeClicked()
{
    QModelIndex index = ui->treeView->currentIndex();
    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    QString text = selectedPart->data(0).toString();
    emit statusUpdateMessage(QString("The selected item is: ") + text, 0);

    /* 更新复选框状态以反映当前选中零件的滤镜状态 */
    ui->checkBoxClip->blockSignals(true);
    ui->checkBoxShrink->blockSignals(true);
    ui->checkBoxClip->setChecked(selectedPart->getClip());
    ui->checkBoxShrink->setChecked(selectedPart->getShrink());
    ui->checkBoxClip->blockSignals(false);
    ui->checkBoxShrink->blockSignals(false);
}

void MainWindow::on_actionOpen_File_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("Open File"), "C:\\",
        tr("STL Files (*.stl);;Text Files (*.txt)")
        );

    if (!fileName.isEmpty()) {
        QFileInfo fileInfo(fileName);
        QString onlyFileName = fileInfo.fileName();
        QModelIndex index = ui->treeView->currentIndex();

        if (index.isValid()) {
            ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
            ModelPart* newItem = new ModelPart({ onlyFileName, "true" });
            selectedPart->appendChild(newItem);
            ui->treeView->model()->layoutChanged();
            newItem->loadSTL(fileName);
            updateRender();
            renderer->ResetCamera();
            renderWindow->Render();
            emit statusUpdateMessage(QString("Loaded: ") + onlyFileName, 0);
        } else {
            emit statusUpdateMessage(
                "Please select a parent item in the tree first!", 0);
        }
    }
}

void MainWindow::handleOptionsButton()
{
    QModelIndex index = ui->treeView->currentIndex();

    if (!index.isValid()) {
        emit statusUpdateMessage("Please select an item first!", 0);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    OptionDialog dialog(this);

    /* ----------------------------------------------------------------
     * 【修复问题2】setInitialData参数顺序修正
     *
     * optiondialog.h 声明为：
     *   setInitialData(name, r, g, b, visible)
     *
     * 之前错误地传入：
     *   setInitialData(name, visible, r, g, b)  ← 错误
     *
     * 修正为正确顺序：
     *   setInitialData(name, r, g, b, visible)  ← 正确
     * ---------------------------------------------------------------- */
    dialog.setInitialData(
        selectedPart->data(0).toString(),   /* name    */
        selectedPart->getColourR(),          /* r       */
        selectedPart->getColourG(),          /* g       */
        selectedPart->getColourB(),          /* b       */
        selectedPart->visible()              /* visible */
        );

    if (dialog.exec() == QDialog::Accepted) {
        selectedPart->set(0, dialog.getName());
        selectedPart->setVisible(dialog.getIsVisible());
        selectedPart->setColour(
            static_cast<unsigned char>(dialog.getR()),
            static_cast<unsigned char>(dialog.getG()),
            static_cast<unsigned char>(dialog.getB())
            );

        /* 颜色通过共享Property自动同步到VR */
        /* 可见性需要通过命令队列通知VR线程 */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vrThread->issueCommand(CMD_SET_VISIBLE,
                                   dialog.getIsVisible() ? 1.0 : 0.0);
        }

        renderWindow->Render();
        emit statusUpdateMessage("Item updated.", 0);
    } else {
        emit statusUpdateMessage("Dialog cancelled.", 0);
    }
}

void MainWindow::on_actionItem_Options_triggered()
{
    handleOptionsButton();
}

void MainWindow::handleClipToggle(bool checked)
{
    QModelIndex index = ui->treeView->currentIndex();

    if (!index.isValid()) {
        ui->checkBoxClip->blockSignals(true);
        ui->checkBoxClip->setChecked(false);
        ui->checkBoxClip->blockSignals(false);
        emit statusUpdateMessage("No item selected — select a part first", 2000);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    selectedPart->setClip(checked);
    updateRender();

    emit statusUpdateMessage(
        checked ? QString("Clip filter applied to: ") + selectedPart->data(0).toString()
                : QString("Clip filter removed from: ") + selectedPart->data(0).toString(),
        2000);
}

void MainWindow::handleShrinkToggle(bool checked)
{
    QModelIndex index = ui->treeView->currentIndex();

    if (!index.isValid()) {
        ui->checkBoxShrink->blockSignals(true);
        ui->checkBoxShrink->setChecked(false);
        ui->checkBoxShrink->blockSignals(false);
        emit statusUpdateMessage("No item selected — select a part first", 2000);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    selectedPart->setShrink(checked);
    updateRender();

    emit statusUpdateMessage(
        checked ? QString("Shrink filter applied to: ") + selectedPart->data(0).toString()
                : QString("Shrink filter removed from: ") + selectedPart->data(0).toString(),
        2000);
}

void MainWindow::updateRender()
{
    renderer->RemoveAllViewProps();

    int topLevelRows = partList->rowCount(QModelIndex());
    for (int i = 0; i < topLevelRows; i++) {
        updateRenderFromTree(partList->index(i, 0, QModelIndex()));
    }

    renderer->Render();
}

void MainWindow::updateRenderFromTree(const QModelIndex& index)
{
    if (index.isValid()) {
        ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
        vtkSmartPointer<vtkActor> partActor = selectedPart->getActor();
        if (partActor != nullptr) {
            renderer->AddActor(partActor);
        }
    }

    if (!partList->hasChildren(index) || (index.flags() & Qt::ItemNeverHasChildren)) {
        return;
    }

    int rows = partList->rowCount(index);
    for (int i = 0; i < rows; i++) {
        updateRenderFromTree(partList->index(i, 0, index));
    }
}
