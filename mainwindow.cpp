#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QDirIterator>
#include <QToolButton>
#include <QMenu>
#include <functional>
#include "optiondialog.h"

#include <vtkSmartPointer.h>
#include <vtkNew.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkLight.h>
#include <vtkTexture.h>
#include <vtkImageData.h>
#include <cmath>
#include <cstdlib>

/* ================================================================
 * 过滤器递归辅助函数（前置声明，供构造函数 lambda 使用）
 * ================================================================ */
static void applyClipRecursive(ModelPart* part, bool enabled)
{
    if (!part) return;
    part->setClip(enabled);
    for (int i = 0; i < part->childCount(); ++i)
        applyClipRecursive(part->child(i), enabled);
}

static void applyShrinkRecursive(ModelPart* part, bool enabled)
{
    if (!part) return;
    part->setShrink(enabled);
    for (int i = 0; i < part->childCount(); ++i)
        applyShrinkRecursive(part->child(i), enabled);
}

/* ================================================================
 * 程序生成星空贴图（GUI renderer 背景）
 * 与 VRRenderThread.cpp 中的同名函数逻辑相同，独立定义以免跨文件依赖。
 * ================================================================ */
static vtkSmartPointer<vtkTexture> generateStarfieldTexture()
{
    const int W = 1024, H = 512, NC = 3;
    std::srand(20250428);

    vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();
    img->SetDimensions(W, H, 1);
    img->AllocateScalars(VTK_UNSIGNED_CHAR, NC);
    unsigned char* buf = static_cast<unsigned char*>(img->GetScalarPointer());

    auto clamp = [](int v) -> unsigned char {
        return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
    };
    auto addPx = [&](int x, int y, int dr, int dg, int db) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        unsigned char* p = buf + (y * W + x) * NC;
        p[0] = clamp((int)p[0] + dr);
        p[1] = clamp((int)p[1] + dg);
        p[2] = clamp((int)p[2] + db);
    };

    /* 深空底色 */
    for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
        int n = std::rand() % 14;
        unsigned char* p = buf + (y * W + x) * NC;
        p[0] = clamp(3  + n / 3);
        p[1] = clamp(4  + n / 4);
        p[2] = clamp(16 + n);
    }
    /* 星云色块 */
    for (int k = 0; k < 12; ++k) {
        int cx = std::rand() % W, cy = std::rand() % H;
        int r  = 35 + std::rand() % 75;
        int nr = 8  + std::rand() % 22, ng = 8 + std::rand() % 28, nb = 30 + std::rand() % 60;
        for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            float d = std::sqrt((float)(dx*dx+dy*dy));
            if (d > r) continue;
            float a = std::exp(-2.5f*(d/r)*(d/r));
            addPx((cx+dx+W)%W,(cy+dy+H)%H,(int)(nr*a),(int)(ng*a),(int)(nb*a));
        }
    }
    /* 点星 */
    for (int s = 0; s < 700; ++s) {
        int sx = std::rand()%W, sy = std::rand()%H, t = std::rand()%3;
        int sr, sg, sb;
        if      (t==0) { sr=sg=sb=215+std::rand()%40; }
        else if (t==1) { sr=175+std::rand()%55; sg=185+std::rand()%55; sb=255; }
        else           { sr=255; sg=230+std::rand()%25; sb=175+std::rand()%55; }
        int hr = 1+std::rand()%3;
        for (int dy=-hr; dy<=hr; ++dy)
        for (int dx=-hr; dx<=hr; ++dx) {
            float d=std::sqrt((float)(dx*dx+dy*dy));
            if (d>hr) continue;
            float a=(d<=0.5f)?1.0f:std::exp(-3.0f*(d/hr)*(d/hr));
            addPx((sx+dx+W)%W,(sy+dy+H)%H,(int)(sr*a),(int)(sg*a),(int)(sb*a));
        }
    }

    vtkSmartPointer<vtkTexture> tex = vtkSmartPointer<vtkTexture>::New();
    tex->SetInputData(img);
    tex->InterpolateOn();
    tex->RepeatOff();
    return tex;
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vrThread(nullptr)
    , isVRRotating(false)
{
    ui->setupUi(this);

    /* ---- 工具栏：File 下拉按钮（Open File + Open Directory）---- */
    {
        QMenu* fileMenu = new QMenu(this);
        fileMenu->addAction(ui->actionOpen_File);
        fileMenu->addAction(ui->actionOpen_Directory);

        QToolButton* fileBtn = new QToolButton(this);
        fileBtn->setText("File");
        fileBtn->setToolTip("File: Open file or directory");
        fileBtn->setMenu(fileMenu);
        fileBtn->setPopupMode(QToolButton::MenuButtonPopup);
        fileBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        fileBtn->setMinimumWidth(50);

        ui->toolBar->insertWidget(ui->toolBar->actions().first(), fileBtn);
    }

    /* ---- 工具栏：Item 下拉按钮（Add Item + Item Options + Delete Node）---- */
    {
        QMenu* itemMenu = new QMenu(this);
        itemMenu->addAction(ui->actionAdd_Item);
        itemMenu->addAction(ui->actionItem_Options);
        itemMenu->addSeparator();
        itemMenu->addAction(ui->actionDelete_Node);

        QToolButton* itemBtn = new QToolButton(this);
        itemBtn->setText("Item");
        itemBtn->setToolTip("Item: Add, edit or delete parts");
        itemBtn->setMenu(itemMenu);
        itemBtn->setPopupMode(QToolButton::MenuButtonPopup);
        itemBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        itemBtn->setMinimumWidth(50);

        /* 插入到第一个 separator 之前（即 File 按钮之后）*/
        QAction* firstSep = nullptr;
        for (QAction* a : ui->toolBar->actions()) {
            if (a->isSeparator()) { firstSep = a; break; }
        }
        if (firstSep)
            ui->toolBar->insertWidget(firstSep, itemBtn);
        else
            ui->toolBar->addWidget(itemBtn);
    }

    /* ---- 菜单栏 / 工具栏 action 连接 ---- */
    connect(ui->actionAdd_Item,    &QAction::triggered,
            this, &MainWindow::on_actionOpen_File_triggered);
    /* actionItem_Options 由 on_actionItem_Options_triggered 自动连接，无需手动 connect */
    connect(ui->actionDelete_Node, &QAction::triggered,
            this, &MainWindow::handleDeleteNode);
    connect(ui->actionStart_VR,    &QAction::triggered,
            this, &MainWindow::handleStartVR);
    connect(ui->actionStop_VR,     &QAction::triggered,
            this, &MainWindow::handleStopVR);
    connect(ui->actionReset_View,  &QAction::triggered,
            this, &MainWindow::handleResetView);
    connect(ui->actionToggle_Rotate, &QAction::triggered,
            this, &MainWindow::handleToggleRotate);

    /* ---- 原有按钮行已删除，连接迁移到菜单/工具栏 action ---- */
    ui->treeView->addAction(ui->actionItem_Options);
    /* ---- 【B方案】VR内选中零件通知 ---- */
    connect(vrThread, &VRRenderThread::vrActorSelected,
            this, [this](int idx, const QString& name) {
        if (idx >= 0)
            emit statusUpdateMessage(
                QString("VR Selected: %1  |  Keys: V=Visible  S=Slice  K=Shrink  C=Color")
                .arg(name), 0);
        else
            emit statusUpdateMessage("VR: No part selected", 2000);
    });
    connect(ui->treeView, &QTreeView::clicked,
            this, &MainWindow::handleTreeClicked);

    /* ---- 【加分功能A-2】右键菜单：将 Delete 动作挂入 treeView ----
     * treeView 的 contextMenuPolicy 已设为 ActionsContextMenu，
     * 只需 addAction 即可出现在右键菜单中。 */
    ui->treeView->addAction(ui->actionDelete_Node);
    /* actionDelete_Node 已在第171行连接，此处只需 addAction 加入右键菜单 */

    /* 右键菜单：对单个零件独立切换 Clip / Shrink 滤镜（评分要求）*/
    ui->treeView->addAction(ui->actionToggleClip);
    connect(ui->actionToggleClip, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) return;
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool nowClipped = !part->getClip();
        /* 递归设置选中节点及子节点的 clip 状态 */
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setClip(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, nowClipped);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Clip %2").arg(part->data(0).toString())
                                  .arg(nowClipped ? "ON" : "OFF"), 2000);
    });

    ui->treeView->addAction(ui->actionToggleShrink);
    connect(ui->actionToggleShrink, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) return;
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool nowShrunk = !part->getShrink();
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setShrink(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, nowShrunk);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Shrink %2").arg(part->data(0).toString())
                                    .arg(nowShrunk ? "ON" : "OFF"), 2000);
    });

    /* ---- 【加分功能A-3】Open Directory：由 Qt 命名约定自动连接，无需手动 connect ---- */

    /* ---- 滤镜复选框 ---- */
    connect(ui->checkBoxClip,   &QCheckBox::toggled,
            this, &MainWindow::handleClipToggle);
    connect(ui->checkBoxShrink, &QCheckBox::toggled,
            this, &MainWindow::handleShrinkToggle);
    connect(ui->checkBoxSlice,  &QCheckBox::toggled,
            this, &MainWindow::handleSliceToggle);

    /* ---- VR控制：已迁移到工具栏 action，此处保留空注释 ---- */

    /* ---- 【创意功能】光照强度滑块 ---- */
    connect(ui->sliderLightIntensity, &QSlider::valueChanged,
            this, &MainWindow::handleLightIntensityChanged);
    ui->sliderLightIntensity->setValue(40);  /* 初始强度 0.8 = 40/100*2.0 */

    /* ---- 删除节点：通过工具栏 Item 菜单 + 右键菜单触发 ---- */

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

    /* 星空背景贴图：程序生成，通过 VTK TexturedBackground 机制渲染，
     * 不依赖 vtkSkybox / cubemap，与 VR 线程保持一致的实现方式 */
    renderer->GradientBackgroundOff();
    renderer->SetBackgroundTexture(generateStarfieldTexture());
    renderer->TexturedBackgroundOn();

    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* ---- GUI侧双光源初始化 ---- */
    renderer->AutomaticLightCreationOff();

    guiKeyLight = vtkSmartPointer<vtkLight>::New();
    guiKeyLight->SetLightTypeToSceneLight();
    guiKeyLight->SetPosition(1.0, 1.0, 1.0);
    guiKeyLight->SetFocalPoint(0.0, 0.0, 0.0);
    guiKeyLight->SetDiffuseColor(1.0, 0.98, 0.95);
    guiKeyLight->SetAmbientColor(0.1, 0.1, 0.1);
    guiKeyLight->SetSpecularColor(1.0, 1.0, 1.0);
    guiKeyLight->SetIntensity(0.8);
    renderer->AddLight(guiKeyLight);

    guiFillLight = vtkSmartPointer<vtkLight>::New();
    guiFillLight->SetLightTypeToSceneLight();
    guiFillLight->SetPosition(-1.0, -0.5, -0.8);
    guiFillLight->SetFocalPoint(0.0, 0.0, 0.0);
    guiFillLight->SetDiffuseColor(0.7, 0.85, 1.0);
    guiFillLight->SetAmbientColor(0.0, 0.0, 0.0);
    guiFillLight->SetSpecularColor(0.5, 0.5, 0.5);
    guiFillLight->SetIntensity(0.32);
    renderer->AddLight(guiFillLight);

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
        
        emit statusUpdateMessage("VR rotation stopped.", 2000);
    } else {
        vrThread->issueCommand(CMD_START_ROTATE, 0.0);
        isVRRotating = true;
        
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
    
    emit statusUpdateMessage("VR view reset.", 2000);
}

/* ================================================================
 * 【创意功能】光照强度滑块
 * 滑块值 0~100 → 光照强度 0.0~2.0
 * 同时更新 GUI 渲染器的主光源（补光保持主光40%比例）
 * ================================================================ */
void MainWindow::handleLightIntensityChanged(int value)
{
    /* 将0~100的滑块值线性映射到0.0~2.0的光照强度 */
    double intensity = value / 100.0 * 2.0;

    /* ---- GUI侧：同步更新桌面窗口的光照 ---- */
    if (guiKeyLight) {
        guiKeyLight->SetIntensity(intensity);
    }
    if (guiFillLight) {
        guiFillLight->SetIntensity(intensity * 0.4);  /* 补光始终保持主光的40% */
    }
    renderWindow->Render();   /* 立即刷新，桌面窗口可见光照变化 */

    /* ---- VR侧：同步命令到VR线程 ---- */
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
 * 递归移除子节点的所有 Actor，兼容右键菜单触发（无需先点击选中）
 * ================================================================ */

/* 递归从 GUI renderer 移除节点及其所有子节点的 Actor */
static void removeActorsRecursive(ModelPart* part, vtkRenderer* renderer)
{
    if (!part) return;
    vtkSmartPointer<vtkActor> actor = part->getActor();
    if (actor) renderer->RemoveActor(actor);
    for (int i = 0; i < part->childCount(); ++i)
        removeActorsRecursive(part->child(i), renderer);
}

void MainWindow::handleDeleteNode()
{
    /* 右键菜单触发时 currentIndex() 可能无效，
     * 改用 selectionModel 的当前索引（hover 选中即可）*/
    QModelIndex index = ui->treeView->selectionModel()->currentIndex();

    if (!index.isValid()) {
        emit statusUpdateMessage("Select a node to delete first!", 2000);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());

    /* 不允许删除根节点（parentItem 为 nullptr 表示是内部 rootItem）*/
    if (selectedPart->parentItem() == nullptr) {
        emit statusUpdateMessage("Cannot delete root node.", 2000);
        return;
    }

    QString partName = selectedPart->data(0).toString();

    /* 1. 通知VR线程移除Actor */
    if (vrThread != nullptr && vrThread->isRunning()) {
        int idx = getActorIndex(selectedPart);
        if (idx >= 0) vrThread->issueCommand(CMD_REMOVE_ACTOR, 0.0, idx);
    }

    /* 2. 从索引映射中移除 */
    actorIndexMap.remove(selectedPart);

    /* 3. 递归从GUI渲染器移除该节点及所有子节点的Actor */
    removeActorsRecursive(selectedPart, renderer);

    /* 4. 从树模型中移除节点（内部级联释放内存）*/
    partList->removeItem(index);

    /* 5. 刷新 */
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
        /* 【B方案】注册零件名称，VR内选中时显示 */
        vrThread->setActorName(idx, part->data(0).toString());
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
    /* getOpenFileNames 允许按住 Ctrl/Shift 多选 */
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this, tr("Open STL File(s)"), "C:\\",
        tr("STL Files (*.stl);;All Files (*)")
    );

    if (fileNames.isEmpty())
        return;

    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        emit statusUpdateMessage(
            "Please select a parent item in the tree first!", 0);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    int loadedCount = 0;

    for (const QString& fileName : qAsConst(fileNames)) {
        QFileInfo fileInfo(fileName);
        QString   onlyFileName = fileInfo.fileName();

        ModelPart* newItem = new ModelPart({ onlyFileName, "true" });
        selectedPart->appendChild(newItem);
        newItem->loadSTL(fileName);

        /* VR 运行时动态推送新 Actor */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vtkActor* vrActor = newItem->getNewActor();
            if (vrActor) {
                ActorPackage pkg;
                pkg.actor    = vrActor;
                pkg.reader   = vtkSmartPointer<vtkSTLReader>(newItem->getReader());
                pkg.clipOn   = newItem->getClip();
                pkg.shrinkOn = newItem->getShrink();
                vrThread->queueAddActor(pkg);
            }
        }
        ++loadedCount;
    }

    ui->treeView->model()->layoutChanged();
    updateRender();
    renderer->ResetCamera();
    renderWindow->Render();
    emit statusUpdateMessage(
        QString("Loaded %1 STL file(s)").arg(loadedCount), 0);
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
 * 递归作用于选中节点及其所有子节点
 * ================================================================ */

void MainWindow::handleClipToggle(bool checked)
{
    /* Clip 对选中节点及其子节点独立应用（评分要求：每个零件独立）*/
    QModelIndex index = ui->treeView->currentIndex();
    if (index.isValid()) {
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        applyClipRecursive(part, checked);
    } else {
        /* 无选中时全局应用 */
        applyClipRecursive(partList->getRootItem(), checked);
    }
    updateRender();
    renderWindow->Render();

    if (vrThread != nullptr && vrThread->isRunning()) {
        double value = FILTER_CLIP * 10.0 + (checked ? 1.0 : 0.0);
        for (int idx : actorIndexMap.values())
            vrThread->issueCommand(CMD_APPLY_FILTER, value, idx);
    }

    emit statusUpdateMessage(
        checked ? "Clip filter applied" : "Clip filter removed", 2000);
}

void MainWindow::handleShrinkToggle(bool checked)
{
    /* Shrink 对选中节点及其子节点独立应用 */
    QModelIndex index = ui->treeView->currentIndex();
    if (index.isValid()) {
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        applyShrinkRecursive(part, checked);
    } else {
        applyShrinkRecursive(partList->getRootItem(), checked);
    }
    updateRender();
    renderWindow->Render();

    if (vrThread != nullptr && vrThread->isRunning()) {
        double value = FILTER_SHRINK * 10.0 + (checked ? 1.0 : 0.0);
        for (int idx : actorIndexMap.values())
            vrThread->issueCommand(CMD_APPLY_FILTER, value, idx);
    }

    emit statusUpdateMessage(
        checked ? "Shrink filter applied" : "Shrink filter removed", 2000);
}

/* 创意加分：Slice 截面视图（对选中节点，全局 fallback）*/
void MainWindow::handleSliceToggle(bool checked)
{
    auto applySlice = [](ModelPart* part, bool en, auto& self) -> void {
        if (!part) return;
        part->setSlice(en);
        for (int i = 0; i < part->childCount(); ++i)
            self(part->child(i), en, self);
    };

    QModelIndex index = ui->treeView->currentIndex();
    if (index.isValid()) {
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        applySlice(part, checked, applySlice);
    } else {
        applySlice(partList->getRootItem(), checked, applySlice);
    }
    updateRender();
    renderWindow->Render();

    emit statusUpdateMessage(
        checked ? "Slice view applied" : "Slice view removed", 2000);
}

/* ================================================================
 * 渲染树遍历
 * ================================================================ */

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

        /* 计算相对路径，用 QDir::toNativeSeparators 无关写法：
         * 先把路径统一转为正斜杠再 split，兼容 Windows（\）和 Linux（/）*/
        QString relPath = dir.relativeFilePath(filePath);
        relPath = QDir::fromNativeSeparators(relPath);   /* 统一为 '/' */
        QStringList parts = relPath.split('/', Qt::SkipEmptyParts);

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

/* ================================================================
 * 渲染树遍历
 * 直接通过 ModelPart 指针递归，不依赖 QModelIndex，
 * 避免 layoutChanged 后索引失效导致部分 Actor 漏加。
 * ================================================================ */

static void addActorsFromPart(ModelPart* part, vtkRenderer* renderer)
{
    if (!part) return;
    vtkSmartPointer<vtkActor> actor = part->getActor();
    if (actor) renderer->AddActor(actor);
    for (int i = 0; i < part->childCount(); ++i)
        addActorsFromPart(part->child(i), renderer);
}

void MainWindow::updateRender()
{
    renderer->RemoveAllViewProps();
    addActorsFromPart(partList->getRootItem(), renderer);
    renderer->Render();
}

void MainWindow::updateRenderFromTree(const QModelIndex& index)
{
    if (!index.isValid()) return;
    ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
    addActorsFromPart(part, renderer);
}

