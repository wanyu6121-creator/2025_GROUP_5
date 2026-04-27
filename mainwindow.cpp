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
{
    ui->setupUi(this);

    /* ---- 原有信号槽 ---- */
    connect(ui->pushButton,   &QPushButton::released, this, &MainWindow::handleButton);
    connect(ui->pushButton_2, &QPushButton::released, this, &MainWindow::handleOptionsButton);
    ui->treeView->addAction(ui->actionItem_Options);
    connect(this, &MainWindow::statusUpdateMessage,
            ui->statusbar, &QStatusBar::showMessage);
    connect(ui->treeView, &QTreeView::clicked, this, &MainWindow::handleTreeClicked);

    /* ---- 【阶段二】VR按钮信号槽（UI文件已添加按钮，此处恢复连接）---- */
    connect(ui->pushButtonStartVR, &QPushButton::released, this, &MainWindow::handleStartVR);
    connect(ui->pushButtonStopVR,  &QPushButton::released, this, &MainWindow::handleStopVR);

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
    /* 确保VR线程在窗口销毁前安全退出 */
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
    /* 防止重复启动 */
    if (vrThread != nullptr && vrThread->isRunning()) {
        emit statusUpdateMessage("VR is already running!", 3000);
        return;
    }

    /* 清理上一次的线程对象 */
    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    /* 创建新线程并填充Actor */
    vrThread = new VRRenderThread(this);
    populateVRActors();

    /* 启动线程 */
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

    emit statusUpdateMessage("VR stopped.", 3000);
}

/* ================================================================
 * 遍历整棵树，为每个已加载STL的零件创建独立VR Actor
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
    emit statusUpdateMessage("Add button was clicked", 0);
}

void MainWindow::handleTreeClicked()
{
    QModelIndex index = ui->treeView->currentIndex();
    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    QString text = selectedPart->data(0).toString();
    emit statusUpdateMessage(QString("The selected item is: ") + text, 0);
}

void MainWindow::on_actionOpen_File_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open File"),
        "C:\\",
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
            emit statusUpdateMessage("Please select a parent item in the tree first!", 0);
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
        selectedPart->visible(),
        selectedPart->getColourR(),
        selectedPart->getColourG(),
        selectedPart->getColourB()
        );

    if (dialog.exec() == QDialog::Accepted) {
        QString newName    = dialog.getName();
        bool    newVisible = dialog.getIsVisible();
        int r = dialog.getR();
        int g = dialog.getG();
        int b = dialog.getB();

        selectedPart->set(0, newName);
        selectedPart->setVisible(newVisible);
        selectedPart->setColour(r, g, b);

        /* ----------------------------------------------------------------
         * 【阶段二核心】可见性同步到VR
         *
         * 颜色已通过 SetProperty 共享自动同步，无需额外操作。
         * 但 SetVisibility 是 Actor 级别的属性，不在 Property 中，
         * 因此需要通过命令队列显式通知VR线程。
         *
         * 注意：由于每个ModelPart对应一个VR Actor，而VRRenderThread
         * 目前只持有一个统一的actorList，暂时对所有Actor广播可见性命令。
         * 阶段三（滤镜联调）时可以优化为按索引精准控制。
         * ---------------------------------------------------------------- */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vrThread->issueCommand(CMD_SET_VISIBLE, newVisible ? 1.0 : 0.0);
        }

        renderWindow->Render();
        emit statusUpdateMessage("Item updated. Changes reflected in VR.", 0);
    } else {
        emit statusUpdateMessage("Dialog cancelled.", 0);
    }
}

void MainWindow::on_actionItem_Options_triggered()
{
    handleOptionsButton();
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
