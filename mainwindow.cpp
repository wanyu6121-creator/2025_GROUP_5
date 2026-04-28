#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDirIterator>
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

    /* ---- 【加分功能A-2】右键菜单：将 Delete 动作挂入 treeView ----
     * treeView 的 contextMenuPolicy 已设为 ActionsContextMenu，
     * 只需 addAction 即可出现在右键菜单中。 */
    ui->treeView->addAction(ui->actionDelete_Node);
    connect(ui->actionDelete_Node, &QAction::triggered,
            this, &MainWindow::handleDeleteNode);

    /* ---- 【加分功能A-3】Open Directory 菜单项 ---- */
    connect(ui->actionOpen_Directory, &QAction::triggered,
            this, &MainWindow::on_actionOpen_Directory_triggered);

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

    /* ---- 【创意功能】光照强度滑块 ----
     * sliderLightIntensity 范围 0~100，初始值40（对应强度0.8）*/
    connect(ui->sliderLightIntensity, &QSlider::valueChanged,
            this, &MainWindow::handleLightIntensityChanged);
    ui->sliderLightIntensity->setValue(40);  /* 初始强度 0.8 = 40/100*2.0 */

    /* ---- 【加分功能】删除节点：已通过右键菜单 actionDelete_Node 连接，见上方 ---- */

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

    /* 渐变背景：底部深蓝 → 顶部亮蓝，提供明显的太空感背景 */
    renderer->SetBackground(0.10, 0.10, 0.30);   /* 底：深蓝 */
    renderer->SetBackground2(0.30, 0.50, 0.80);  /* 顶：亮蓝 */
    renderer->GradientBackgroundOn();

    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* 初始渲染，确保背景立即显示 */
    renderWindow->Render();
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

    /* 清理上一次的线程实例（支持多次重启）*/
    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    actorIndexMap.clear();
    isVRRotating = false;
    ui->pushButtonRotate->setText("Start Rotate");

    vrThread = new VRRenderThread(this);

    /* 遍历树，为每个已加载STL的零件创建VR Actor并注册 */
    populateVRActors();

    /* 将当前光照强度滑块值同步到线程初始状态 */
    double initIntensity = ui->sliderLightIntensity->value() / 100.0 * 2.0;
    vrThread->issueCommand(CMD_SET_LIGHT_INTENSITY, initIntensity);

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
 * 【创意功能】光照强度滑块
 * 滑块值 0~100 → 光照强度 0.0~2.0
 * ================================================================ */
void MainWindow::handleLightIntensityChanged(int value)
{
    /* 将0~100的滑块值线性映射到0.0~2.0的光照强度 */
    double intensity = value / 100.0 * 2.0;

    if (vrThread != nullptr && vrThread->isRunning()) {
        vrThread->issueCommand(CMD_SET_LIGHT_INTENSITY, intensity);
    }

    /* labelLightValue 实时显示百分比数字（滑块旁的数字标签）*/
    ui->labelLightValue->setText(QString("%1%").arg(value * 2));

    /* 状态栏显示当前百分比 */
    emit statusUpdateMessage(
        QString("Light intensity: %1%").arg(value * 2), 1500);
}

/* ================================================================
 * 【加分功能】删除选中节点
 * 同步从GUI渲染器和VR线程中移除对应Actor
 * ================================================================ */
void MainWindow::handleDeleteNode()
{
    QModelIndex index = ui->treeView->currentIndex();

    if (!index.isValid()) {
        emit statusUpdateMessage("Select a node to delete first!", 2000);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());

    /* 不允许删除根节点 */
    if (selectedPart->parentItem() == nullptr) {
        emit statusUpdateMessage("Cannot delete root node.", 2000);
        return;
    }

    QString partName = selectedPart->data(0).toString();

    /* 1. 通知VR线程移除Actor（在GUI操作之前，索引尚有效）*/
    if (vrThread != nullptr && vrThread->isRunning()) {
        int idx = getActorIndex(selectedPart);
        if (idx >= 0) {
            vrThread->issueCommand(CMD_REMOVE_ACTOR, 0.0, idx);
        }
    }

    /* 2. 从索引映射中移除 */
    actorIndexMap.remove(selectedPart);

    /* 3. 从GUI渲染器移除Actor */
    vtkSmartPointer<vtkActor> guiActor = selectedPart->getActor();
    if (guiActor) {
        renderer->RemoveActor(guiActor);
    }

    /* 4. 从树模型中移除节点
     * removeItem() 内部调用 beginRemoveRows/endRemoveRows（protected方法），
     * 并通过 ModelPart::removeChild() 级联释放子节点内存 */
    partList->removeItem(index);

    /* 5. 刷新GUI渲染 */
    updateRender();
    renderWindow->Render();

    emit statusUpdateMessage(QString("Deleted: ") + partName, 2000);
}

/* ================================================================
 * 遍历树，注册VR Actor
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

    /* 更新复选框以反映当前选中零件的滤镜状态 */
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

            /* 【加分功能】VR运行时动态推送新Actor */
            if (vrThread != nullptr && vrThread->isRunning()) {
                vtkActor* vrActor = newItem->getNewActor();
                if (vrActor) {
                    ActorPackage pkg;
                    pkg.actor    = vrActor;
                    pkg.reader   = vtkSmartPointer<vtkSTLReader>(newItem->getReader());
                    pkg.clipOn   = newItem->getClip();
                    pkg.shrinkOn = newItem->getShrink();

                    /* 记录新Actor的索引（在push前预算，基于当前actorList大小）*/
                    /* 注意：实际索引由VR线程在processPendingActors中分配，
                     * 这里暂不更新actorIndexMap（动态添加的Actor索引无法提前预知）
                     * 如需后续控制该Actor，需要通过其他机制获取真实索引 */
                    vrThread->queueAddActor(pkg);
                }
            }

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

        bool newVisible = dialog.getIsVisible();
        selectedPart->setVisible(newVisible);

        /* 颜色写入共享Property，VR Actor自动同步 */
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

    /* 同步到VR：value = filterType*10 + (enabled?1:0) */
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

/* ================================================================
 * 【加分功能A-1】批量目录加载
 *
 * 流程：
 *   1. QFileDialog::getExistingDirectory 选择根目录
 *   2. QDirIterator 递归遍历，找出所有 *.stl
 *   3. 目录结构映射为树状父子节点
 *      - 每级子目录对应一个非叶节点（若已存在则复用）
 *      - 文件名作为叶节点名，加载 STL
 *   4. VR 线程运行时，同步推送新 Actor
 * ================================================================ */
void MainWindow::on_actionOpen_Directory_triggered()
{
    QString rootDir = QFileDialog::getExistingDirectory(
        this,
        tr("Select Directory to Load"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (rootDir.isEmpty())
        return;

    /* 在树中为此根目录创建一个顶级父节点，以目录名命名 */
    QDir dir(rootDir);
    ModelPart* rootItem  = partList->getRootItem();
    QString    dirName   = dir.dirName().isEmpty() ? rootDir : dir.dirName();
    ModelPart* dirRoot   = new ModelPart({ dirName, "true", 200, 200, 200 });
    rootItem->appendChild(dirRoot);

    int loadedCount = 0;

    /* -------------------------------------------------------
     * 递归辅助 lambda：根据相对路径段构建/复用中间目录节点
     * ------------------------------------------------------- */
    std::function<ModelPart*(ModelPart*, const QStringList&, int)> ensurePath;
    ensurePath = [&](ModelPart* parent, const QStringList& parts, int depth) -> ModelPart*
    {
        if (depth >= parts.size() - 1)   /* 最后一段是文件名，由调用者处理 */
            return parent;

        const QString& seg = parts[depth];

        /* 查找是否已有同名子节点（避免重复）*/
        for (int i = 0; i < parent->childCount(); ++i) {
            ModelPart* child = parent->child(i);
            if (child->data(0).toString() == seg)
                return ensurePath(child, parts, depth + 1);
        }

        /* 不存在则新建目录节点 */
        ModelPart* node = new ModelPart({ seg, "true", 180, 180, 180 });
        parent->appendChild(node);
        return ensurePath(node, parts, depth + 1);
    };

    /* -------------------------------------------------------
     * QDirIterator 递归遍历所有 .stl 文件
     * ------------------------------------------------------- */
    QDirIterator it(rootDir,
                    QStringList() << "*.stl" << "*.STL",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);

        /* 计算相对路径，拆分为路径段列表（含文件名作最后一段）*/
        QString    relPath = dir.relativeFilePath(filePath);
        QStringList parts  = relPath.split('/', Qt::SkipEmptyParts);

        /* 找到或创建中间目录节点 */
        ModelPart* parentNode = ensurePath(dirRoot, parts, 0);

        /* 创建叶节点（以文件名命名）并加载 STL */
        QString    fileName = fi.fileName();
        ModelPart* fileNode = new ModelPart({ fileName, "true", 255, 255, 255 });
        parentNode->appendChild(fileNode);
        fileNode->loadSTL(filePath);

        /* VR 线程运行时动态推送 Actor */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vtkActor* vrActor = fileNode->getNewActor();
            if (vrActor) {
                ActorPackage pkg;
                pkg.actor    = vrActor;
                pkg.reader   = vtkSmartPointer<vtkSTLReader>(fileNode->getReader());
                pkg.clipOn   = fileNode->getClip();
                pkg.shrinkOn = fileNode->getShrink();
                vrThread->queueAddActor(pkg);
            }
        }

        ++loadedCount;
    }

    /* 刷新树视图与渲染 */
    ui->treeView->model()->layoutChanged();
    ui->treeView->expandAll();

    updateRender();
    renderer->ResetCamera();
    renderWindow->Render();

    if (loadedCount > 0) {
        emit statusUpdateMessage(
            QString("Loaded %1 STL file(s) from: %2").arg(loadedCount).arg(dirName), 0);
    } else {
        /* 如果目录里没有任何 STL，移除刚才创建的空父节点 */
        QModelIndex rootNodeIndex = partList->index(
            rootItem->childCount() - 1, 0, QModelIndex());
        partList->removeItem(rootNodeIndex);
        ui->treeView->model()->layoutChanged();
        emit statusUpdateMessage("No STL files found in the selected directory.", 3000);
    }
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
