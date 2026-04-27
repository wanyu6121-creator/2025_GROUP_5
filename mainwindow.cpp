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
    , vrThread(nullptr)           /* 【新增】初始化VR线程指针为nullptr */
{
    ui->setupUi(this);

    /* ---- 原有信号槽连接，保持不变 ---- */
    connect(ui->pushButton,  &QPushButton::released, this, &MainWindow::handleButton);
    connect(ui->pushButton_2, &QPushButton::released, this, &MainWindow::handleOptionsButton);
    ui->treeView->addAction(ui->actionItem_Options);
    connect(this, &MainWindow::statusUpdateMessage,
            ui->statusbar, &QStatusBar::showMessage);
    connect(ui->treeView, &QTreeView::clicked, this, &MainWindow::handleTreeClicked);

    /* ---- 【新增】连接VR按钮信号槽 ---- */
    /* 注意：需要在 mainwindow.ui 中添加两个按钮，objectName分别为
     *       pushButtonStartVR 和 pushButtonStopVR
     * 如果还没改UI，可以先用下面注释的方式通过菜单Action触发 */
    //connect(ui->pushButtonStartVR, &QPushButton::released, this, &MainWindow::handleStartVR);
    //connect(ui->pushButtonStopVR,  &QPushButton::released, this, &MainWindow::handleStopVR);

    /* ---- 初始化TreeView和ModelPartList（保持不变）---- */
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

    /* ---- 初始化VTK渲染器（保持不变）---- */
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
    /* 【新增】确保VR线程在窗口销毁前安全退出 */
    if (vrThread != nullptr && vrThread->isRunning()) {
        vrThread->requestInterruption();
        vrThread->wait(3000); /* 最多等待3秒 */
    }
    delete vrThread;
    delete ui;
}

/* ================================================================
 * 【新增】VR启动函数
 * ================================================================ */
void MainWindow::handleStartVR()
{
    /* 防止重复启动 */
    if (vrThread != nullptr && vrThread->isRunning()) {
        emit statusUpdateMessage("VR is already running!", 3000);
        return;
    }

    /* 清理上一次的线程对象（如果有）*/
    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    /* 创建新的VR线程实例 */
    vrThread = new VRRenderThread(this);

    /* 遍历树，收集所有已加载STL的零件，为每个创建独立VR Actor */
    populateVRActors();

    /* 启动VR线程 */
    vrThread->start();

    emit statusUpdateMessage("VR started!", 3000);
}

/* ================================================================
 * 【新增】VR停止函数
 * ================================================================ */
void MainWindow::handleStopVR()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("VR is not running.", 3000);
        return;
    }

    /* 请求线程中断，run()中的 isInterruptionRequested() 会变为true */
    vrThread->requestInterruption();

    /* 等待线程真正退出（最多5秒）*/
    if (!vrThread->wait(5000)) {
        /* 超时强制终止（不推荐，但作为保险措施）*/
        vrThread->terminate();
        vrThread->wait();
    }

    emit statusUpdateMessage("VR stopped.", 3000);
}

/* ================================================================
 * 【新增】遍历整棵树，为每个有Actor的零件创建VR Actor
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

    /* 尝试为此零件创建VR Actor（如果未加载STL则返回nullptr）*/
    vtkActor* vrActor = part->getNewActor();
    if (vrActor != nullptr) {
        vrThread->addActorOffline(vrActor);
        /* addActorOffline 内部存储了裸指针，VRRenderThread负责其生命周期 */
    }

    /* 递归处理子节点 */
    if (partList->hasChildren(index)) {
        int rows = partList->rowCount(index);
        for (int i = 0; i < rows; i++) {
            populateVRActorsFromTree(partList->index(i, 0, index));
        }
    }
}

/* ================================================================
 * 以下为原有函数，保持不变
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
            emit statusUpdateMessage(QString("Loaded STL file: ") + onlyFileName, 0);
        } else {
            emit statusUpdateMessage("File chosen, but no parent item selected in the tree!", 0);
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

        /* 颜色通过共享Property自动同步到VR，无需额外操作 */
        /* 可见性需要通知VR线程（阶段二实现，此处预留）*/
        // if (vrThread && vrThread->isRunning()) {
        //     vrThread->issueCommand(CMD_SET_VISIBLE, newVisible ? 1.0 : 0.0);
        // }

        renderWindow->Render();
        emit statusUpdateMessage("Dialog accepted: Item updated", 0);
    } else {
        emit statusUpdateMessage("Dialog rejected", 0);
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
