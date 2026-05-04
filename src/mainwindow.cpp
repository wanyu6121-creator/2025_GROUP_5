/**  @file mainwindow.cpp
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   主窗口实现。管理Qt UI、ModelPartList树模型和VTK渲染管线。
 *   Main window implementation. Manages the Qt UI, ModelPartList tree model
 *   and the VTK render pipeline.
 */

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
 * 滤镜递归辅助函数(前置声明,供构造函数中的lambda使用)
 * Filter recursive helper functions (forward-declared for use in constructor lambdas)
 *
 * 这些函数对一个零件节点及其所有子节点递归应用滤镜状态变更。
 * These functions recursively apply filter state changes to a part and all its children.
 * ================================================================ */

static void applyClipRecursive(ModelPart* part, bool enabled)
{
    if (!part) return;
    part->setClip(enabled);
    /* 递归处理所有子节点
     * Recursively process all children */
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
 * 程序生成星空背景贴图(GUI渲染器背景)
 * Procedurally generated starfield texture (for GUI renderer background)
 *
 * 算法:深空底色 + 星云色块 + 点星
 * Algorithm: deep-space base colour + nebula blobs + point stars
 *
 * 与VRRenderThread.cpp中的cubemap生成逻辑相同,但这里生成单张
 * 2D贴图用于GUI背景,不依赖vtkSkybox。
 * Same algorithm as the cubemap in VRRenderThread.cpp but generates a single
 * 2D texture for the GUI background, avoiding vtkSkybox dependency.
 * ================================================================ */

static vtkSmartPointer<vtkTexture> generateStarfieldTexture()
{
    const int W = 1024, H = 512, NC = 3;  /* 宽/高/通道数
                                           * Width/Height/Channels */
    std::srand(20250428);  /* 固定种子确保每次生成相同图案
                            * Fixed seed for reproducible pattern */

    vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();
    img->SetDimensions(W, H, 1);
    img->AllocateScalars(VTK_UNSIGNED_CHAR, NC);
    unsigned char* buf = static_cast<unsigned char*>(img->GetScalarPointer());

    /* clamp:将值限制在[0,255]范围内
     * clamp: keeps values within [0, 255] */
    auto clamp = [](int v) -> unsigned char {
        return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
    };

    /* addPx:在指定像素位置叠加颜色(加法混合)
     * addPx: additive colour blend at a specific pixel position */
    auto addPx = [&](int x, int y, int dr, int dg, int db) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        unsigned char* p = buf + (y * W + x) * NC;
        p[0] = clamp((int)p[0] + dr);
        p[1] = clamp((int)p[1] + dg);
        p[2] = clamp((int)p[2] + db);
    };

    /* 第1步:深空底色(暗蓝色为主,加少量随机噪点)
     * Step 1: deep-space base colour (dark blue with slight random noise) */
    for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
        int n = std::rand() % 14;
        unsigned char* p = buf + (y * W + x) * NC;
        p[0] = clamp(3  + n / 3);
        p[1] = clamp(4  + n / 4);
        p[2] = clamp(16 + n);
    }

    /* 第2步:星云色块(高斯衰减,随机颜色)
     * Step 2: nebula blobs (Gaussian falloff, random colour) */
    for (int k = 0; k < 12; ++k) {
        int cx = std::rand() % W, cy = std::rand() % H;
        int r  = 35 + std::rand() % 75;
        int nr = 8  + std::rand() % 22, ng = 8 + std::rand() % 28, nb = 30 + std::rand() % 60;
        for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            float d = std::sqrt((float)(dx*dx+dy*dy));
            if (d > r) continue;
            float a = std::exp(-2.5f*(d/r)*(d/r));  /* 高斯权重
                                                     * Gaussian weight */
            addPx((cx+dx+W)%W,(cy+dy+H)%H,(int)(nr*a),(int)(ng*a),(int)(nb*a));
        }
    }

    /* 第3步:点星(三种颜色类型:白、蓝白、黄)
     * Step 3: point stars (three colour types: white, blue-white, yellow) */
    for (int s = 0; s < 700; ++s) {
        int sx = std::rand()%W, sy = std::rand()%H, t = std::rand()%3;
        int sr, sg, sb;
        if      (t==0) { sr=sg=sb=215+std::rand()%40; }           /* 白星
                                                                   * White */
        else if (t==1) { sr=175+std::rand()%55; sg=185+std::rand()%55; sb=255; }  /* 蓝白
                                                                                   * Blue-white */
        else           { sr=255; sg=230+std::rand()%25; sb=175+std::rand()%55; }  /* 黄星
                                                                                   * Yellow */
        int hr = 1+std::rand()%3;  /* 星的半径
                                    * Star radius */
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

/* ================================================================
 * 构造函数
 * Constructor
 * ================================================================ */

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , vrThread(nullptr)
    , isVRRotating(false)
{
    ui->setupUi(this);

    /* ---- 工具栏:File下拉按钮(打开文件 + 打开目录)
     *      Toolbar: File dropdown button (Open File + Open Directory) ---- */
    {
        QMenu* fileMenu = new QMenu(this);
        fileMenu->addAction(ui->actionOpen_File);
        fileMenu->addAction(ui->actionOpen_Directory);

        QToolButton* fileBtn = new QToolButton(this);
        fileBtn->setText("File");
        fileBtn->setToolTip("File: Open file or directory");
        fileBtn->setMenu(fileMenu);
        /* MenuButtonPopup模式:主按钮触发默认动作,箭头展开菜单
         * MenuButtonPopup: main button triggers default action, arrow expands menu */
        fileBtn->setPopupMode(QToolButton::MenuButtonPopup);
        fileBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        fileBtn->setMinimumWidth(50);

        ui->toolBar->insertWidget(ui->toolBar->actions().first(), fileBtn);
    }

    /* ---- 工具栏:Item下拉按钮(添加/编辑/删除节点)
     *      Toolbar: Item dropdown button (Add/Edit/Delete nodes) ---- */
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

        /* 插入到第一个分隔符之前(File按钮之后)
         * Insert before the first separator (after the File button) */
        QAction* firstSep = nullptr;
        for (QAction* a : ui->toolBar->actions()) {
            if (a->isSeparator()) { firstSep = a; break; }
        }
        if (firstSep)
            ui->toolBar->insertWidget(firstSep, itemBtn);
        else
            ui->toolBar->addWidget(itemBtn);
    }

    /* ---- 菜单栏/工具栏动作信号连接
     *      Menu bar / toolbar action signal connections ---- */
    connect(ui->actionAdd_Item,    &QAction::triggered,
            this, &MainWindow::on_actionOpen_File_triggered);
    connect(ui->actionDelete_Node, &QAction::triggered,
            this, &MainWindow::handleDeleteNode);
    connect(ui->actionStart_VR,    &QAction::triggered,
            this, &MainWindow::handleStartVR);
    connect(ui->actionStop_VR,     &QAction::triggered,
            this, &MainWindow::handleStopVR);
    connect(ui->actionReset_View,  &QAction::triggered,
            this, &MainWindow::handleResetView);
    connect(ui->actionStart_Rotate, &QAction::triggered,
            this, &MainWindow::handleStartRotate);
    connect(ui->actionStop_Rotate,  &QAction::triggered,
            this, &MainWindow::handleStopRotate);

    /* ---- 工具栏:Set View下拉按钮(正视图/顶视图/右视图/等轴视图)
     *      Toolbar: Set View dropdown button (Front/Top/Right/Isometric) ---- */
    {
        connect(ui->actionViewFront, &QAction::triggered, this, &MainWindow::handleViewFront);
        connect(ui->actionViewTop,   &QAction::triggered, this, &MainWindow::handleViewTop);
        connect(ui->actionViewRight, &QAction::triggered, this, &MainWindow::handleViewRight);
        connect(ui->actionViewIso,   &QAction::triggered, this, &MainWindow::handleViewIso);

        QMenu* viewMenu = new QMenu(this);
        viewMenu->addAction(ui->actionViewFront);
        viewMenu->addAction(ui->actionViewTop);
        viewMenu->addAction(ui->actionViewRight);
        viewMenu->addAction(ui->actionViewIso);

        QToolButton* viewBtn = new QToolButton(this);
        viewBtn->setText("Set View");
        viewBtn->setToolTip("Set model orientation");
        viewBtn->setMenu(viewMenu);
        viewBtn->setPopupMode(QToolButton::MenuButtonPopup);
        viewBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        viewBtn->setMinimumWidth(70);
        viewBtn->setDefaultAction(ui->actionViewFront);
        viewBtn->setText("Set View");  /* setDefaultAction会覆盖文本,需重新设置
                                        * setDefaultAction overwrites text, reset it */

        /* 在Reset View动作之前插入
         * Insert before the Reset View action */
        QAction* resetAction = ui->actionReset_View;
        ui->toolBar->insertWidget(resetAction, viewBtn);
    }

    /* ---- 树视图右键菜单:添加动作
     *      Tree view right-click menu: add actions ---- */
    ui->treeView->addAction(ui->actionItem_Options);

    /* VR内选中零件时通过lambda更新状态栏
     * Update the status bar via lambda when a part is selected in VR */
    connect(vrThread, &VRRenderThread::vrActorSelected,
            this, [this](int idx, const QString& name) {
        if (idx >= 0)
            emit statusUpdateMessage(
                QString("VR Selected: %1  |  Keys: V=Visible  S=Slice  K=Shrink  C=Color")
                .arg(name), 0);
        else
            emit statusUpdateMessage("VR: No part selected", 2000);
    });

    connect(ui->treeView, &QTreeView::clicked, this, &MainWindow::handleTreeClicked);

    /* 将Delete Node动作加入右键菜单
     * Add Delete Node action to the right-click context menu */
    ui->treeView->addAction(ui->actionDelete_Node);

    /* ---- 右键菜单:对单个零件独立切换各滤镜
     *      Right-click menu: independently toggle filters on individual parts ---- */

    /* 裁剪滤镜切换(递归作用于选中节点及其子节点)
     * Clip filter toggle (recursively affects selected node and its children) */
    ui->treeView->addAction(ui->actionToggleClip);
    connect(ui->actionToggleClip, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) {
            emit statusUpdateMessage("Select a part first to toggle Clip!", 3000);
            return;
        }
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool nowClipped = !part->getClip();  /* 切换当前状态
                                              * Toggle current state */
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setClip(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, nowClipped);
        if (nowClipped)
            syncVRFilterRecursive(part, FILTER_SMOOTH, false);
        syncVRFilterRecursive(part, FILTER_CLIP, nowClipped);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Clip %2").arg(part->data(0).toString())
                                  .arg(nowClipped ? "ON" : "OFF"), 2000);
    });

    /* 收缩滤镜切换
     * Shrink filter toggle */
    ui->treeView->addAction(ui->actionToggleShrink);
    connect(ui->actionToggleShrink, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) {
            emit statusUpdateMessage("Select a part first to toggle Shrink!", 3000);
            return;
        }
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool nowShrunk = !part->getShrink();
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setShrink(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, nowShrunk);
        if (nowShrunk)
            syncVRFilterRecursive(part, FILTER_SMOOTH, false);
        syncVRFilterRecursive(part, FILTER_SHRINK, nowShrunk);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Shrink %2").arg(part->data(0).toString())
                                    .arg(nowShrunk ? "ON" : "OFF"), 2000);
    });

    /* 平滑滤镜切换
     * Smooth filter toggle */
    ui->treeView->addAction(ui->actionToggleSmooth);
    connect(ui->actionToggleSmooth, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) {
            emit statusUpdateMessage("Select a part first to toggle Smooth!", 3000);
            return;
        }
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool now = !part->getSmooth();
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setSmooth(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, now);
        if (now) {
            syncVRFilterRecursive(part, FILTER_CLIP, false);
            syncVRFilterRecursive(part, FILTER_SLICE, false);
            syncVRFilterRecursive(part, FILTER_SHRINK, false);
        }
        syncVRFilterRecursive(part, FILTER_SMOOTH, now);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Smooth %2").arg(part->data(0).toString())
                                    .arg(now ? "ON" : "OFF"), 2000);
    });

    /* 抽取滤镜切换
     * Decimate filter toggle */
    ui->treeView->addAction(ui->actionToggleDecimate);
    connect(ui->actionToggleDecimate, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) {
            emit statusUpdateMessage("Select a part first to toggle Decimate!", 3000);
            return;
        }
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool now = !part->getDecimate();
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setDecimate(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, now);
        syncVRFilterRecursive(part, FILTER_DECIMATE, now);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Decimate %2").arg(part->data(0).toString())
                                      .arg(now ? "ON" : "OFF"), 2000);
    });

    /* 高度色彩滤镜切换
     * Elevation filter toggle */
    ui->treeView->addAction(ui->actionToggleElevation);
    connect(ui->actionToggleElevation, &QAction::triggered, this, [this]() {
        QModelIndex index = ui->treeView->selectionModel()->currentIndex();
        if (!index.isValid()) {
            emit statusUpdateMessage("Select a part first to toggle Elevation!", 3000);
            return;
        }
        ModelPart* part = static_cast<ModelPart*>(index.internalPointer());
        bool now = !part->getElevation();
        std::function<void(ModelPart*, bool)> recurse = [&](ModelPart* p, bool en) {
            if (!p) return;
            p->setElevation(en);
            for (int i = 0; i < p->childCount(); ++i) recurse(p->child(i), en);
        };
        recurse(part, now);
        syncVRFilterRecursive(part, FILTER_ELEVATION, now);
        updateRender();
        renderWindow->Render();
        emit statusUpdateMessage(
            QString("%1: Elevation %2").arg(part->data(0).toString())
                                       .arg(now ? "ON" : "OFF"), 2000);
    });

    /* ---- 底部复选框滤镜连接(作用于所有零件)
     *      Bottom checkbox filter connections (apply to all parts) ---- */
    connect(ui->checkBoxClip,      &QCheckBox::toggled, this, &MainWindow::handleClipToggle);
    connect(ui->checkBoxShrink,    &QCheckBox::toggled, this, &MainWindow::handleShrinkToggle);
    connect(ui->checkBoxSmooth,    &QCheckBox::toggled, this, &MainWindow::handleSmoothToggle);
    connect(ui->checkBoxDecimate,  &QCheckBox::toggled, this, &MainWindow::handleDecimateToggle);
    connect(ui->checkBoxElevation, &QCheckBox::toggled, this, &MainWindow::handleElevationToggle);
    connect(ui->checkBoxSlice,     &QCheckBox::toggled, this, &MainWindow::handleSliceToggle);

    /* ---- 光照强度滑块连接
     *      Light intensity slider connection ---- */
    connect(ui->sliderLightIntensity, &QSlider::valueChanged,
            this, &MainWindow::handleLightIntensityChanged);
    ui->sliderLightIntensity->setValue(40);  /* 初始强度0.8(40/100*2.0)
                                              * Initial intensity 0.8 (40/100*2.0) */

    /* ---- 初始化树视图和TreeModel
     *      Initialise tree view and tree model ---- */
    this->partList = new ModelPartList("PartsList");
    ui->treeView->setModel(this->partList);

    /* 添加占位节点,使布局在加载STL前可见
     * Add placeholder nodes so the layout is visible before STL files are loaded */
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

    /* ---- 初始化VTK渲染管线
     *      Initialise VTK render pipeline ---- */
    renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    ui->widget->setRenderWindow(renderWindow);
    renderer = vtkSmartPointer<vtkRenderer>::New();
    renderWindow->AddRenderer(renderer);

    /* 星空背景贴图:程序生成,通过VTK TexturedBackground渲染。
     * 不依赖vtkSkybox/cubemap,与VR线程实现方式一致。
     * Starfield background texture: procedurally generated, rendered via VTK TexturedBackground.
     * Avoids vtkSkybox/cubemap dependency, consistent with the VR thread implementation. */
    renderer->GradientBackgroundOff();
    renderer->SetBackgroundTexture(generateStarfieldTexture());
    renderer->TexturedBackgroundOn();

    /* 初始相机角度:稍微偏转以提供3D纵深感
     * Initial camera angle: slightly tilted to give a 3D sense of depth */
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* ---- GUI侧双光源初始化
     *      GUI dual-light source initialisation ----
     *
     * 使用主光(Key Light)+补光(Fill Light)的两光源方案:
     * Uses a two-light Key+Fill rig:
     *   - 主光:正面暖白光,受滑块控制
     *     Key light: front warm white, slider-controlled
     *   - 补光:侧后方冷蓝光,强度固定为主光的40%
     *     Fill light: side-rear cool blue, fixed at 40% of key */
    renderer->AutomaticLightCreationOff();  /* 禁用VTK自动创建默认光源
                                             * Disable VTK auto light creation */

    guiKeyLight = vtkSmartPointer<vtkLight>::New();
    guiKeyLight->SetLightTypeToSceneLight();
    guiKeyLight->SetPosition(1.0, 1.0, 1.0);
    guiKeyLight->SetFocalPoint(0.0, 0.0, 0.0);
    guiKeyLight->SetDiffuseColor(1.0, 0.98, 0.95);  /* 暖白色
                                                     * Warm white */
    guiKeyLight->SetAmbientColor(0.1, 0.1, 0.1);
    guiKeyLight->SetSpecularColor(1.0, 1.0, 1.0);
    guiKeyLight->SetIntensity(0.8);
    renderer->AddLight(guiKeyLight);

    guiFillLight = vtkSmartPointer<vtkLight>::New();
    guiFillLight->SetLightTypeToSceneLight();
    guiFillLight->SetPosition(-1.0, -0.5, -0.8);
    guiFillLight->SetFocalPoint(0.0, 0.0, 0.0);
    guiFillLight->SetDiffuseColor(0.7, 0.85, 1.0);  /* 冷蓝色
                                                     * Cool blue */
    guiFillLight->SetAmbientColor(0.0, 0.0, 0.0);
    guiFillLight->SetSpecularColor(0.5, 0.5, 0.5);
    guiFillLight->SetIntensity(0.32);  /* 主光的40%
                                        * 40% of key light */
    renderer->AddLight(guiFillLight);

    /* ---- 将statusUpdateMessage信号绑定到状态栏的showMessage槽
     *      Connect statusUpdateMessage signal to the status bar's showMessage slot ---- */
    connect(this, &MainWindow::statusUpdateMessage,
            ui->statusbar, &QStatusBar::showMessage);

    renderWindow->Render();
}

/* ================================================================
 * 析构函数
 * Destructor
 * ================================================================ */

MainWindow::~MainWindow()
{
    /* 优雅地停止VR线程,等待最多5秒
     * Gracefully stop the VR thread, waiting up to 5 seconds */
    if (vrThread != nullptr && vrThread->isRunning()) {
        vrThread->requestInterruption();
        vrThread->wait(5000);
    }
    delete vrThread;
    delete ui;
}

/* ================================================================
 * VR启动
 * VR startup
 * ================================================================ */

void MainWindow::handleStartVR()
{
    /* 防止重复启动
     * Prevent duplicate launch */
    if (vrThread != nullptr && vrThread->isRunning()) {
        emit statusUpdateMessage("VR is already running!", 3000);
        return;
    }

    /* 清理上一次的线程实例(支持多次重启)
     * Clean up the previous thread instance (supports multiple restarts) */
    if (vrThread != nullptr) {
        delete vrThread;
        vrThread = nullptr;
    }

    actorIndexMap.clear();
    isVRRotating = false;

    vrThread = new VRRenderThread(this);

    /* 遍历树,为每个已加载STL的零件创建VR Actor并注册
     * Traverse the tree and register a VR actor for each loaded STL part */
    populateVRActors();

    /* 将当前光照强度滑块值同步到VR线程的初始状态
     * Sync the current light intensity slider value to the VR thread's initial state */
    double initIntensity = ui->sliderLightIntensity->value() / 100.0 * 2.0;
    vrThread->issueCommand(CMD_SET_LIGHT_INTENSITY, initIntensity);

    vrThread->start();
    emit statusUpdateMessage("VR started! Put on your headset.", 0);
}

/* ================================================================
 * VR停止
 * VR stop
 * ================================================================ */

void MainWindow::handleStopVR()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("VR is not running.", 3000);
        return;
    }

    /* 请求中断并等待线程退出
     * Request interruption and wait for the thread to exit */
    vrThread->requestInterruption();
    if (!vrThread->wait(5000)) {
        /* 超时则强制终止(最后手段)
         * Timeout: force terminate (last resort) */
        vrThread->terminate();
        vrThread->wait();
    }

    isVRRotating = false;
    emit statusUpdateMessage("VR stopped.", 3000);
}

/* ================================================================
 * 旋转动画开关
 * Rotation animation toggle
 * ================================================================ */

void MainWindow::handleStartRotate()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000);
        return;
    }
    if (isVRRotating) {
        emit statusUpdateMessage("Rotation already running.", 2000);
        return;
    }
    vrThread->issueCommand(CMD_START_ROTATE, 0.0);
    isVRRotating = true;
    emit statusUpdateMessage("VR rotation started.", 2000);
}

void MainWindow::handleStopRotate()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000);
        return;
    }
    if (!isVRRotating) {
        emit statusUpdateMessage("Rotation is not running.", 2000);
        return;
    }
    vrThread->issueCommand(CMD_STOP_ROTATE, 0.0);
    isVRRotating = false;
    emit statusUpdateMessage("VR rotation stopped.", 2000);
}

/* ================================================================
 * 视图重置与视图预设
 * View reset and view presets
 * ================================================================ */

void MainWindow::handleResetView()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000);
        return;
    }
    /* CMD_RESET_VIEW将所有Actor恢复到场景初始化时保存的位置
     * CMD_RESET_VIEW restores all actors to positions saved at scene initialisation */
    vrThread->issueCommand(CMD_RESET_VIEW, 0.0);
    isVRRotating = false;
    emit statusUpdateMessage("Model reset -- all parts returned to original positions.", 3000);
}

/* 四个视图预设:通过CMD_SET_VIEW传递预设索引(0=正视图,1=顶视图,2=右视图,3=等轴视图)
 * Four view presets: CMD_SET_VIEW carries preset index (0=Front, 1=Top, 2=Right, 3=Iso) */

void MainWindow::handleViewFront()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000); return;
    }
    vrThread->issueCommand(CMD_SET_VIEW, 0.0);
    emit statusUpdateMessage("Model view: Front", 2000);
}

void MainWindow::handleViewTop()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000); return;
    }
    vrThread->issueCommand(CMD_SET_VIEW, 1.0);
    emit statusUpdateMessage("Model view: Top", 2000);
}

void MainWindow::handleViewRight()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000); return;
    }
    vrThread->issueCommand(CMD_SET_VIEW, 2.0);
    emit statusUpdateMessage("Model view: Right Side", 2000);
}

void MainWindow::handleViewIso()
{
    if (vrThread == nullptr || !vrThread->isRunning()) {
        emit statusUpdateMessage("Start VR first!", 3000); return;
    }
    vrThread->issueCommand(CMD_SET_VIEW, 3.0);
    emit statusUpdateMessage("Model view: Isometric", 2000);
}

/* ================================================================
 * 光照强度滑块处理
 * Light intensity slider handler
 *
 * 滑块值0~100映射到光照强度0.0~2.0。
 * Slider value 0~100 maps to light intensity 0.0~2.0.
 * 同时更新GUI渲染器的主光源和VR线程。
 * Updates both the GUI renderer's key light and the VR thread.
 * ================================================================ */

void MainWindow::handleLightIntensityChanged(int value)
{
    /* 线性映射:滑块0~100 -> 强度0.0~2.0
     * Linear mapping: slider 0~100 -> intensity 0.0~2.0 */
    double intensity = value / 100.0 * 2.0;

    /* GUI侧:更新桌面窗口光照
     * GUI side: update desktop window lighting */
    if (guiKeyLight) {
        guiKeyLight->SetIntensity(intensity);
    }
    if (guiFillLight) {
        guiFillLight->SetIntensity(intensity * 0.4);  /* 补光始终保持主光的40%
                                                       * Fill always at 40% of key */
    }
    renderWindow->Render();  /* 立即刷新,桌面窗口可见光照变化
                              * Refresh immediately so GUI shows the change */

    /* VR侧:通过命令队列同步到VR线程
     * VR side: sync to VR thread via command queue */
    if (vrThread != nullptr && vrThread->isRunning()) {
        vrThread->issueCommand(CMD_SET_LIGHT_INTENSITY, intensity);
    }

    /* 更新滑块旁边的百分比标签
     * Update the percentage label next to the slider */
    ui->labelLightValue->setText(QString("%1%").arg(value * 2));

    emit statusUpdateMessage(QString("Light intensity: %1%").arg(value * 2), 1500);
}

/* ================================================================
 * 删除选中节点
 * Delete selected node
 *
 * 递归移除该节点及所有子节点的Actor,兼容右键菜单触发。
 * Recursively removes actors for the node and all children,
 * compatible with right-click menu triggering (no prior click needed).
 * ================================================================ */

/* 递归从GUI渲染器移除节点及所有子节点的Actor
 * Recursively remove actors for a node and all its children from the GUI renderer */
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
    /* 右键菜单触发时currentIndex()可能无效,
     * 改用selectionModel的当前索引(鼠标悬停即选中)
     * When triggered from right-click menu, currentIndex() may be invalid;
     * use selectionModel's current index instead (hover selects) */
    QModelIndex index = ui->treeView->selectionModel()->currentIndex();

    if (!index.isValid()) {
        emit statusUpdateMessage("Select a node to delete first!", 2000);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());

    /* 不允许删除内部根节点(parentItem为nullptr表示是内部根)
     * Prevent deletion of the internal root node (parentItem nullptr = internal root) */
    if (selectedPart->parentItem() == nullptr) {
        emit statusUpdateMessage("Cannot delete root node.", 2000);
        return;
    }

    QString partName = selectedPart->data(0).toString();

    /* 1. 通知VR线程移除对应Actor
     *    Notify VR thread to remove the corresponding actor */
    if (vrThread != nullptr && vrThread->isRunning()) {
        int idx = getActorIndex(selectedPart);
        if (idx >= 0) vrThread->issueCommand(CMD_REMOVE_ACTOR, 0.0, idx);
    }

    /* 2. 从索引映射表中移除该零件
     *    Remove this part from the index map */
    actorIndexMap.remove(selectedPart);

    /* 3. 递归从GUI渲染器移除该节点及所有子节点的Actor
     *    Recursively remove actors from the GUI renderer for this node and all children */
    removeActorsRecursive(selectedPart, renderer);

    /* 4. 从树模型中移除节点(内部级联释放内存)
     *    Remove node from tree model (internally cascades memory release) */
    partList->removeItem(index);

    /* 5. 刷新渲染
     *    Refresh the render */
    updateRender();
    renderWindow->Render();

    emit statusUpdateMessage(QString("Deleted: ") + partName, 2000);
}

/* ================================================================
 * 遍历树注册VR Actor
 * Tree traversal to register VR actors
 * ================================================================ */

void MainWindow::populateVRActors()
{
    if (!vrThread) return;

    /* 遍历所有顶级节点并递归注册VR Actor
     * Traverse all top-level nodes and recursively register VR actors */
    int topLevelRows = partList->rowCount(QModelIndex());
    for (int i = 0; i < topLevelRows; i++) {
        populateVRActorsFromTree(partList->index(i, 0, QModelIndex()));
    }
}

void MainWindow::populateVRActorsFromTree(const QModelIndex& index)
{
    if (!index.isValid()) return;

    ModelPart* part = static_cast<ModelPart*>(index.internalPointer());

    /* getNewActor()只为已加载STL的节点返回非nullptr
     * getNewActor() returns non-nullptr only for nodes that have loaded an STL */
    vtkActor* vrActor = part->getNewActor();
    if (vrActor != nullptr) {
        int idx = vrThread->addActorOffline(
            vrActor,
            part->getReader(),
            part->getClip(),
            part->getShrink(),
            part->getSmooth(),
            part->getDecimate(),
            part->getElevation(),
            part->getSlice()
        );
        actorIndexMap.insert(part, idx);
        /* 注册零件名称,VR内选中时显示在状态栏
         * Register part name for display in status bar when selected in VR */
        vrThread->setActorName(idx, part->data(0).toString());
    }

    /* 递归处理子节点
     * Recursively process child nodes */
    if (partList->hasChildren(index)) {
        int rows = partList->rowCount(index);
        for (int i = 0; i < rows; i++) {
            populateVRActorsFromTree(partList->index(i, 0, index));
        }
    }
}

int MainWindow::getActorIndex(ModelPart* part) const
{
    /* 从映射表中查找Actor索引,未注册则返回-1
     * Look up actor index from the map; returns -1 if not registered */
    return actorIndexMap.value(part, -1);
}

void MainWindow::syncVRFilterRecursive(ModelPart* part, int filterType, bool enabled)
{
    if (!part || !vrThread || !vrThread->isRunning()) return;

    int idx = getActorIndex(part);
    if (idx >= 0) {
        double value = filterType * 10.0 + (enabled ? 1.0 : 0.0);
        vrThread->issueCommand(CMD_APPLY_FILTER, value, idx);
    }

    for (int i = 0; i < part->childCount(); ++i)
        syncVRFilterRecursive(part->child(i), filterType, enabled);
}

/* ================================================================
 * 其他原有函数
 * Other original functions
 * ================================================================ */

void MainWindow::handleButton()
{
    emit statusUpdateMessage("Add button was clicked", 0);
}

void MainWindow::handleTreeClicked()
{
    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) return;

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    QString name = selectedPart->data(0).toString();

    /* 同步所有五个滤镜复选框以反映选中零件的当前状态。
     * blockSignals(true)防止程序性setChecked()触发handleXxxToggle()导致不必要的渲染。
     * Sync all five filter checkboxes to reflect the selected part's current state.
     * blockSignals(true) prevents programmatic setChecked() from triggering
     * handleXxxToggle() and causing unnecessary renders. */
    ui->checkBoxClip->blockSignals(true);
    ui->checkBoxShrink->blockSignals(true);
    ui->checkBoxSmooth->blockSignals(true);
    ui->checkBoxDecimate->blockSignals(true);
    ui->checkBoxElevation->blockSignals(true);
    ui->checkBoxSlice->blockSignals(true);

    ui->checkBoxClip->setChecked(selectedPart->getClip());
    ui->checkBoxShrink->setChecked(selectedPart->getShrink());
    ui->checkBoxSmooth->setChecked(selectedPart->getSmooth());
    ui->checkBoxDecimate->setChecked(selectedPart->getDecimate());
    ui->checkBoxElevation->setChecked(selectedPart->getElevation());
    ui->checkBoxSlice->setChecked(selectedPart->getSlice());

    ui->checkBoxClip->blockSignals(false);
    ui->checkBoxShrink->blockSignals(false);
    ui->checkBoxSmooth->blockSignals(false);
    ui->checkBoxDecimate->blockSignals(false);
    ui->checkBoxElevation->blockSignals(false);
    ui->checkBoxSlice->blockSignals(false);

    /* 状态栏显示:零件名+可见性+当前激活的滤镜列表
     * Status bar: part name + visibility + list of currently active filters */
    QStringList activeFilters;
    if (selectedPart->getClip())      activeFilters << "Clip";
    if (selectedPart->getShrink())    activeFilters << "Shrink";
    if (selectedPart->getSmooth())    activeFilters << "Smooth";
    if (selectedPart->getDecimate())  activeFilters << "Decimate";
    if (selectedPart->getElevation()) activeFilters << "Elevation";
    if (selectedPart->getSlice())     activeFilters << "Slice";

    QString filterStr = activeFilters.isEmpty() ? "none" : activeFilters.join(", ");
    QString visStr    = selectedPart->visible() ? "Visible" : "Hidden";

    emit statusUpdateMessage(
        QString("Selected: %1  |  %2  |  Filters: %3")
            .arg(name).arg(visStr).arg(filterStr), 0);
}

void MainWindow::on_actionOpen_File_triggered()
{
    /* 支持按住Ctrl/Shift多选
     * Supports Ctrl/Shift multi-select */
    QStringList fileNames = QFileDialog::getOpenFileNames(
        this, tr("Open STL File(s)"), "C:\\",
        tr("STL Files (*.stl);;All Files (*)")
    );

    if (fileNames.isEmpty()) return;

    QModelIndex index = ui->treeView->currentIndex();
    if (!index.isValid()) {
        emit statusUpdateMessage("Please select a parent item in the tree first!", 0);
        return;
    }

    ModelPart* selectedPart = static_cast<ModelPart*>(index.internalPointer());
    int loadedCount = 0;

    for (const QString& fileName : qAsConst(fileNames)) {
        QFileInfo fileInfo(fileName);
        QString   onlyFileName = fileInfo.fileName();

        /* 创建树节点并加载STL
         * Create tree node and load the STL */
        ModelPart* newItem = new ModelPart({ onlyFileName, "true" });
        selectedPart->appendChild(newItem);
        newItem->loadSTL(fileName);

        /* VR运行时动态推送新Actor
         * Dynamically push new actor to VR thread if it is running */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vtkActor* vrActor = newItem->getNewActor();
            if (vrActor) {
                ActorPackage pkg;
                pkg.actor    = vrActor;
                pkg.reader   = vtkSmartPointer<vtkSTLReader>(newItem->getReader());
                pkg.clipOn   = newItem->getClip();
                pkg.shrinkOn = newItem->getShrink();
                pkg.smoothOn = newItem->getSmooth();
                pkg.decimateOn = newItem->getDecimate();
                pkg.elevationOn = newItem->getElevation();
                pkg.sliceOn = newItem->getSlice();
                vrThread->queueAddActor(pkg);
            }
        }
        ++loadedCount;
    }

    ui->treeView->model()->layoutChanged();
    updateRender();
    renderer->ResetCamera();
    renderWindow->Render();
    emit statusUpdateMessage(QString("Loaded %1 STL file(s)").arg(loadedCount), 0);
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

    /* 预填当前属性,使用户可以看到现有值并只修改需要的部分
     * Pre-fill with current properties so user sees existing values */
    dialog.setInitialData(
        selectedPart->data(0).toString(),
        selectedPart->getColourR(),
        selectedPart->getColourG(),
        selectedPart->getColourB(),
        selectedPart->visible()
    );

    if (dialog.exec() == QDialog::Accepted) {
        /* 保存旧值用于状态栏差异对比
         * Save old values for status bar diff comparison */
        QString oldName    = selectedPart->data(0).toString();
        bool    oldVisible = selectedPart->visible();
        int     oldR       = selectedPart->getColourR();
        int     oldG       = selectedPart->getColourG();
        int     oldB       = selectedPart->getColourB();

        /* 应用新值
         * Apply new values */
        selectedPart->set(0, dialog.getName());

        bool newVisible = dialog.getIsVisible();
        selectedPart->setVisible(newVisible);

        unsigned char newR = static_cast<unsigned char>(dialog.getR());
        unsigned char newG = static_cast<unsigned char>(dialog.getG());
        unsigned char newB = static_cast<unsigned char>(dialog.getB());
        selectedPart->setColour(newR, newG, newB);

        /* 向VR线程发送可见性命令(精确定位到对应Actor)
         * Send visibility command to VR thread targeting the specific actor */
        if (vrThread != nullptr && vrThread->isRunning()) {
            int idx = getActorIndex(selectedPart);
            vrThread->issueCommand(CMD_SET_VISIBLE, newVisible ? 1.0 : 0.0, idx);
        }

        renderWindow->Render();

        /* 生成详细的变更描述用于状态栏
         * Generate detailed change description for the status bar */
        QStringList changes;
        if (dialog.getName() != oldName)
            changes << QString("Name: %1->%2").arg(oldName).arg(dialog.getName());
        if (newVisible != oldVisible)
            changes << QString("Visible: %1->%2")
                       .arg(oldVisible ? "Yes":"No")
                       .arg(newVisible ? "Yes":"No");
        if (newR != oldR || newG != oldG || newB != oldB)
            changes << QString("Colour: (%1,%2,%3)->(%4,%5,%6)")
                       .arg(oldR).arg(oldG).arg(oldB)
                       .arg(newR).arg(newG).arg(newB);

        if (changes.isEmpty())
            emit statusUpdateMessage(QString("%1: no changes made.").arg(dialog.getName()), 2000);
        else
            emit statusUpdateMessage(
                QString("%1 updated -- ").arg(dialog.getName()) + changes.join("  "), 0);

    } else {
        emit statusUpdateMessage(
            QString("Edit cancelled: %1").arg(selectedPart->data(0).toString()), 2000);
    }
}

void MainWindow::on_actionItem_Options_triggered()
{
    handleOptionsButton();
}

/* ================================================================
 * 全局滤镜切换(底部复选框,作用于所有零件)
 * Global filter toggles (bottom checkboxes, apply to all parts)
 *
 * 通用递归辅助:对树中所有零件应用同一滤镜状态。
 * Generic recursive helper: applies the same filter state to all parts in the tree.
 * ================================================================ */

static void applyFilterAll(ModelPart* part, bool en,
                           void (ModelPart::*setter)(bool))
{
    if (!part) return;
    (part->*setter)(en);  /* 调用成员函数指针
                           * Call member function pointer */
    for (int i = 0; i < part->childCount(); ++i)
        applyFilterAll(part->child(i), en, setter);
}

void MainWindow::handleClipToggle(bool checked)
{
    /* 对所有零件递归应用裁剪滤镜状态
     * Recursively apply clip filter state to all parts */
    applyFilterAll(partList->getRootItem(), checked, &ModelPart::setClip);
    if (checked) {
        ui->checkBoxSmooth->blockSignals(true);
        ui->checkBoxSmooth->setChecked(false);
        ui->checkBoxSmooth->blockSignals(false);
    }
    updateRender();
    renderWindow->Render();

    /* 同步到VR线程中所有已注册的Actor
     * Sync to all registered actors in the VR thread */
    if (vrThread && vrThread->isRunning()) {
        if (checked)
            syncVRFilterRecursive(partList->getRootItem(), FILTER_SMOOTH, false);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_CLIP, checked);
    }
    emit statusUpdateMessage(
        checked ? "Clip applied to all parts" : "Clip removed from all parts", 2000);
}

void MainWindow::handleShrinkToggle(bool checked)
{
    applyFilterAll(partList->getRootItem(), checked, &ModelPart::setShrink);
    if (checked) {
        ui->checkBoxSmooth->blockSignals(true);
        ui->checkBoxSmooth->setChecked(false);
        ui->checkBoxSmooth->blockSignals(false);
    }
    updateRender();
    renderWindow->Render();

    if (vrThread && vrThread->isRunning()) {
        if (checked)
            syncVRFilterRecursive(partList->getRootItem(), FILTER_SMOOTH, false);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_SHRINK, checked);
    }
    emit statusUpdateMessage(
        checked ? "Shrink applied to all parts" : "Shrink removed from all parts", 2000);
}

void MainWindow::handleSmoothToggle(bool checked)
{
    applyFilterAll(partList->getRootItem(), checked, &ModelPart::setSmooth);

    /* Smooth与Clip不兼容:启用Smooth时全局关闭Clip复选框
     * Smooth and Clip are incompatible: disable Clip checkbox globally when Smooth is enabled */
    if (checked) {
        ui->checkBoxClip->blockSignals(true);
        ui->checkBoxClip->setChecked(false);
        ui->checkBoxClip->blockSignals(false);
        ui->checkBoxSlice->blockSignals(true);
        ui->checkBoxSlice->setChecked(false);
        ui->checkBoxSlice->blockSignals(false);
        ui->checkBoxShrink->blockSignals(true);
        ui->checkBoxShrink->setChecked(false);
        ui->checkBoxShrink->blockSignals(false);
        applyFilterAll(partList->getRootItem(), false, &ModelPart::setClip);
        applyFilterAll(partList->getRootItem(), false, &ModelPart::setSlice);
        applyFilterAll(partList->getRootItem(), false, &ModelPart::setShrink);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_CLIP, false);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_SLICE, false);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_SHRINK, false);
    }
    updateRender();
    renderWindow->Render();

    if (vrThread && vrThread->isRunning()) {
        syncVRFilterRecursive(partList->getRootItem(), FILTER_SMOOTH, checked);
    }
    emit statusUpdateMessage(
        checked ? "Smooth applied to all parts" : "Smooth removed from all parts", 2000);
}

void MainWindow::handleDecimateToggle(bool checked)
{
    applyFilterAll(partList->getRootItem(), checked, &ModelPart::setDecimate);
    updateRender();
    renderWindow->Render();

    if (vrThread && vrThread->isRunning()) {
        syncVRFilterRecursive(partList->getRootItem(), FILTER_DECIMATE, checked);
    }
    emit statusUpdateMessage(
        checked ? "Decimate applied to all parts" : "Decimate removed from all parts", 2000);
}

void MainWindow::handleElevationToggle(bool checked)
{
    applyFilterAll(partList->getRootItem(), checked, &ModelPart::setElevation);
    updateRender();
    renderWindow->Render();

    if (vrThread && vrThread->isRunning()) {
        syncVRFilterRecursive(partList->getRootItem(), FILTER_ELEVATION, checked);
    }
    emit statusUpdateMessage(
        checked ? "Elevation applied to all parts" : "Elevation removed from all parts", 2000);
}

/* 截面视图(创意功能):对选中节点独立应用,未选中则全局应用
 * Slice view (creative feature): applies to selected node independently,
 * falls back to global application if nothing is selected */
void MainWindow::handleSliceToggle(bool checked)
{
    if (checked) {
        ui->checkBoxSmooth->blockSignals(true);
        ui->checkBoxSmooth->setChecked(false);
        ui->checkBoxSmooth->blockSignals(false);
    }

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
        if (checked)
            syncVRFilterRecursive(part, FILTER_SMOOTH, false);
        syncVRFilterRecursive(part, FILTER_SLICE, checked);
    } else {
        applySlice(partList->getRootItem(), checked, applySlice);
        if (checked)
            syncVRFilterRecursive(partList->getRootItem(), FILTER_SMOOTH, false);
        syncVRFilterRecursive(partList->getRootItem(), FILTER_SLICE, checked);
    }
    updateRender();
    renderWindow->Render();

    emit statusUpdateMessage(
        checked ? "Slice view applied" : "Slice view removed", 2000);
}

/* ================================================================
 * 批量目录加载
 * Batch directory loading
 *
 * 流程:
 * Flow:
 *   1. QFileDialog::getExistingDirectory选择根目录
 *      Select root directory via QFileDialog::getExistingDirectory
 *   2. QDirIterator递归遍历所有*.stl文件
 *      QDirIterator recursively traverses all *.stl files
 *   3. 目录结构映射为树状父子节点(目录=父节点,文件=叶节点)
 *      Directory structure maps to tree parent/child nodes (dir=parent, file=leaf)
 *   4. VR线程运行时同步推送新Actor
 *      Dynamically push new actors to VR thread if running
 * ================================================================ */

void MainWindow::on_actionOpen_Directory_triggered()
{
    QString rootDir = QFileDialog::getExistingDirectory(
        this, tr("Select Directory to Load"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (rootDir.isEmpty()) return;

    /* 为此根目录创建顶级父节点
     * Create a top-level parent node for this root directory */
    QDir dir(rootDir);
    ModelPart* rootItem  = partList->getRootItem();
    QString    dirName   = dir.dirName().isEmpty() ? rootDir : dir.dirName();
    ModelPart* dirRoot   = new ModelPart({ dirName, "true", 200, 200, 200 });
    rootItem->appendChild(dirRoot);

    int loadedCount = 0;

    /* 递归辅助lambda:根据相对路径构建或复用中间目录节点。
     * 避免为同一子目录创建重复节点。
     * Recursive helper lambda: build or reuse intermediate directory nodes.
     * Avoids creating duplicate nodes for the same subdirectory. */
    std::function<ModelPart*(ModelPart*, const QStringList&, int)> ensurePath;
    ensurePath = [&](ModelPart* parent, const QStringList& parts, int depth) -> ModelPart*
    {
        if (depth >= parts.size() - 1)  /* 最后一段是文件名,由调用者处理
                                         * Last segment is filename, handled by caller */
            return parent;

        const QString& seg = parts[depth];

        /* 查找是否已有同名子节点(避免重复)
         * Check if a child with this name already exists (avoid duplicates) */
        for (int i = 0; i < parent->childCount(); ++i) {
            ModelPart* child = parent->child(i);
            if (child->data(0).toString() == seg)
                return ensurePath(child, parts, depth + 1);
        }

        /* 不存在则新建目录节点
         * Create a new directory node if it doesn't exist */
        ModelPart* node = new ModelPart({ seg, "true", 180, 180, 180 });
        parent->appendChild(node);
        return ensurePath(node, parts, depth + 1);
    };

    /* 递归遍历所有.stl文件
     * Recursively traverse all .stl files */
    QDirIterator it(rootDir,
                    QStringList() << "*.stl" << "*.STL",
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);

        /* 统一使用正斜杠(兼容Windows的反斜杠和Linux的正斜杠)
         * Normalise to forward slashes (compatible with Windows backslash and Linux forward slash) */
        QString relPath = dir.relativeFilePath(filePath);
        relPath = QDir::fromNativeSeparators(relPath);
        QStringList parts = relPath.split('/', Qt::SkipEmptyParts);

        /* 找到或创建中间目录节点
         * Find or create intermediate directory nodes */
        ModelPart* parentNode = ensurePath(dirRoot, parts, 0);

        /* 创建叶节点(文件)并加载STL
         * Create leaf node (file) and load the STL */
        QString    fileName = fi.fileName();
        ModelPart* fileNode = new ModelPart({ fileName, "true", 255, 255, 255 });
        parentNode->appendChild(fileNode);
        fileNode->loadSTL(filePath);

        /* VR线程运行时动态推送Actor
         * Dynamically push actor to VR thread if running */
        if (vrThread != nullptr && vrThread->isRunning()) {
            vtkActor* vrActor = fileNode->getNewActor();
            if (vrActor) {
                ActorPackage pkg;
                pkg.actor    = vrActor;
                pkg.reader   = vtkSmartPointer<vtkSTLReader>(fileNode->getReader());
                pkg.clipOn   = fileNode->getClip();
                pkg.shrinkOn = fileNode->getShrink();
                pkg.smoothOn = fileNode->getSmooth();
                pkg.decimateOn = fileNode->getDecimate();
                pkg.elevationOn = fileNode->getElevation();
                pkg.sliceOn = fileNode->getSlice();
                vrThread->queueAddActor(pkg);
            }
        }
        ++loadedCount;
    }

    /* 刷新树视图并展开新节点
     * Refresh tree view and expand the new nodes */
    ui->treeView->model()->layoutChanged();
    ui->treeView->expandAll();

    updateRender();
    renderer->ResetCamera();
    renderWindow->Render();

    if (loadedCount > 0) {
        emit statusUpdateMessage(
            QString("Loaded %1 STL file(s) from: %2").arg(loadedCount).arg(dirName), 0);
    } else {
        /* 目录内无STL文件:移除刚才创建的空父节点并提示用户
         * No STL files found: remove the empty parent node just created and inform the user */
        QModelIndex rootNodeIndex = partList->index(
            rootItem->childCount() - 1, 0, QModelIndex());
        partList->removeItem(rootNodeIndex);
        ui->treeView->model()->layoutChanged();
        emit statusUpdateMessage("No STL files found in the selected directory.", 3000);
    }
}

/* ================================================================
 * 渲染树遍历
 * Render tree traversal
 *
 * 直接通过ModelPart指针递归,不依赖QModelIndex,
 * 避免layoutChanged()后索引失效导致部分Actor漏加。
 * Recursively via ModelPart pointer rather than QModelIndex,
 * avoiding the risk of stale indices after layoutChanged() causing missed actors.
 * ================================================================ */

static void addActorsFromPart(ModelPart* part, vtkRenderer* renderer)
{
    if (!part) return;
    vtkSmartPointer<vtkActor> actor = part->getActor();
    /* getActor()对未加载STL的节点返回nullptr,需要检查
     * getActor() returns nullptr for nodes that haven't loaded an STL; must check */
    if (actor) renderer->AddActor(actor);
    for (int i = 0; i < part->childCount(); ++i)
        addActorsFromPart(part->child(i), renderer);
}

void MainWindow::updateRender()
{
    /* 移除所有现有Actor,重新从树中添加
     * Remove all existing actors, then re-add from the tree */
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
