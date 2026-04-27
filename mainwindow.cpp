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
        vrThread->wait(5000);
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

    /* 停止并清理上一次的线程实例（支持多次重启）*/
    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    /* 清空索引映射（每次启动重新建立）*/
    actorIndexMap.clear();

    isVRRotating = false;
    ui->pushButtonRotate->setText("Start Rotate");

    vrThread = new VRRenderThread(this);

    /* 遍历树，为每个已加载STL的零件创建VR Actor并注册 */
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
 * 遍历树，为每个已加载STL的零件创建VR Actor并注册到vrThread
 * ================================================================ */
void MainWindow::populateVRActors()
{
    if (!vrThread) return;

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
        /* 注册Actor时同时传入reader和初始滤镜状态，
         * 这样VR线程可以在收到CMD_APPLY_FILTER时正确重建pipeline */
        int idx = vrThread->addActorOffline(
            vrActor,
            part->getReader(),
            part->getClip(),
            part->getShrink()
        );
        actorIndexMap.insert(part, idx);
    }

    if (partList->hasChildren(index)) {
        int rows = partList->rowCount(index);
        for (int i = 0; i < rows; i++) {
            populateVRActorsFromTree(partList->index(i, 0, index));
        }
    }
}

int MainWindow::getActorIndex(ModelPart* part) const
{
    return actorIndexMap.value(part, -1);
}

/* ================================================================
 * 原有函数
 * ================================================================ */

void MainWindow::handleButton()
{
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

    dialog.setInitialData(
        selectedPart->data(0).toString(),
        selectedPart->getColourR(),
        selectedPart->getColourG(),
        selectedPart->getColourB(),
        selectedPart->visible()
    );

    if (dialog.exec() == QDialog::Accepted) {
        selectedPart->set(0, dialog.getName());

        /* 更新可见性 */
        bool newVisible = dialog.getIsVisible();
        selectedPart->setVisible(newVisible);

        /* 更新颜色（写入共享Property，VR Actor自动同步）*/
        selectedPart->setColour(
            static_cast<unsigned char>(dialog.getR()),
            static_cast<unsigned char>(dialog.getG()),
            static_cast<unsigned char>(dialog.getB())
        );

        /* 向VR线程发送可见性命令（精确定位到对应Actor）*/
        if (vrThread != nullptr && vrThread->isRunning()) {
            int idx = getActorIndex(selectedPart);
            vrThread->issueCommand(CMD_SET_VISIBLE,
                                   newVisible ? 1.0 : 0.0,
                                   idx);
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

/* ================================================================
 * 过滤器切换（GUI侧 + 同步到VR）
 * ================================================================ */

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

    /* 同步到VR线程：value = filterType*10 + enabled */
    if (vrThread != nullptr && vrThread->isRunning()) {
        int idx = getActorIndex(selectedPart);
        double value = FILTER_CLIP * 10.0 + (checked ? 1.0 : 0.0);
        vrThread->issueCommand(CMD_APPLY_FILTER, value, idx);
    }

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

    /* 同步到VR线程：value = filterType*10 + enabled */
    if (vrThread != nullptr && vrThread->isRunning()) {
        int idx = getActorIndex(selectedPart);
        double value = FILTER_SHRINK * 10.0 + (checked ? 1.0 : 0.0);
        vrThread->issueCommand(CMD_APPLY_FILTER, value, idx);
    }

    emit statusUpdateMessage(
        checked ? QString("Shrink filter applied to: ") + selectedPart->data(0).toString()
                : QString("Shrink filter removed from: ") + selectedPart->data(0).toString(),
        2000);
}

/* ================================================================
 * 渲染树遍历
 * ================================================================ */

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
