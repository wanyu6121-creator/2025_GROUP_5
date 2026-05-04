/**  @file VRRenderThread.cpp
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程实现。支持两种渲染模式(自动检测头显):
 *   VR render thread implementation. Supports two render modes (auto-detect headset):
 *     - VR模式:连接HTC Vive头显时使用vtkOpenVRRenderWindow
 *       VR mode: uses vtkOpenVRRenderWindow when HTC Vive headset is connected
 *     - 桌面模式:无头显时自动fallback,使用普通vtkRenderWindow预览
 *       Desktop mode: falls back to plain vtkRenderWindow when no headset is found
 *
 *   特色功能:
 *   Feature highlights:
 *     - Skybox天空盒背景(程序生成星空cubemap)
 *       Skybox background (procedurally generated starfield cubemap)
 *     - 光照强度实时控制(CMD_SET_LIGHT_INTENSITY,value=0.0~2.0)
 *       Real-time light intensity control (CMD_SET_LIGHT_INTENSITY, value 0.0~2.0)
 *     - 动态增删Actor(queueAddActor + processPendingActors
 *     CMD_REMOVE_ACTOR)
 *       Dynamic add/remove actors (queueAddActor + processPendingActors / CMD_REMOVE_ACTOR)
 *     - 手柄射线拾取+拖动零件(VR模式)
 *       Controller ray picking + drag parts (VR mode)
 *     - 鼠标点击拾取+键盘快捷键(桌面模式)
 *       Mouse click picking + keyboard shortcuts (desktop mode)
 */

#include "VRRenderThread.h"

#include <QCoreApplication>
#include <QDir>
#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkPlaneSource.h>
#include <algorithm>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkCallbackCommand.h>
#include <vtkImageData.h>
#include <vtkTexture.h>
#include <vtkSkybox.h>
#include <cmath>
#include <cstdlib>
#include <memory>

/* ================================================================
 * 颜色循环表(CMD_VR_SET_COLOUR命令依次切换颜色)
 * Colour cycle table (CMD_VR_SET_COLOUR command cycles through these)
 * ================================================================ */
const int VRRenderThread::colorTable[VRRenderThread::COLOR_COUNT][3] = {
    {255, 255, 255},   /* 白
                        * White */
    {220,  50,  50},   /* 红
                        * Red */
    { 50, 180,  50},   /* 绿
                        * Green */
    { 50, 120, 220},   /* 蓝
                        * Blue */
    {220, 180,  50},   /* 黄
                        * Yellow */
    {180,  50, 220},   /* 紫
                        * Purple */
};

/* ================================================================
 * 构造 / 析构
 * Constructor / Destructor
 * ================================================================ */

VRRenderThread::VRRenderThread(QObject* parent)
    : QThread(parent)
    , isRotating(false)
    , mainLightIntensity(0.8)
    , selectedActorIndex(-1)
    , rotationAngle(0.0)
    , initCamSaved(false)
    , isDragging(false)
    , dragActorIndex(-1)
    , vrPickRenderer(nullptr)
{
    /* 初始化保存颜色(用于取消高亮时恢复)
     * Initialise saved colour (used to restore when un-highlighting) */
    savedColor[0] = savedColor[1] = savedColor[2] = 1.0;
    vrDragLastPos[0] = vrDragLastPos[1] = vrDragLastPos[2] = 0.0;
}

VRRenderThread::~VRRenderThread()
{
    /* 请求中断并等待线程结束
     * Request interruption and wait for the thread to finish */
    if (isRunning()) {
        requestInterruption();
        wait(5000);
    }
    /* 释放所有Actor(由getNewActor()分配)
     * Free all actors (allocated by getNewActor()) */
    for (vtkActor* a : actorList) {
        if (a) a->Delete();
    }
}

/* ================================================================
 * 模型视图辅助函数
 * Model view helper functions
 *
 * saveFactoryState()     -- 场景初始化后调用一次;记录相机和每个Actor的
 *                           世界坐标,供CMD_RESET_VIEW精确恢复。
 *                           Called once after scene setup; records the camera
 *                           and every actor's world position for CMD_RESET_VIEW.
 *
 * resetModelView()       -- 将Actor恢复到初始位置,清除旋转。
 *                           Restores actors to factory positions, clears rotation.
 *                           真实VR中不移动相机(用户物理站立);桌面模式中也重置相机。
 *                           In real VR the camera is not moved (user stands physically);
 *                           in desktop mode the camera is also reset.
 *
 * applyViewPreset(index) -- 将所有Actor旋转到指定预设方向:
 *                           Rotates all actors to the chosen preset orientation:
 *                           0=正视图, 1=顶视图, 2=右视图, 3=等轴视图
 *                           0=Front, 1=Top, 2=Right Side, 3=Isometric
 * ================================================================ */

void VRRenderThread::saveFactoryState(vtkCamera* camera)
{
    if (!camera) return;

    /* 保存相机的位置、焦点和朝上方向
     * Save the camera's position, focal point and view-up direction */
    camera->GetPosition(initCamPos);
    camera->GetFocalPoint(initCamFocal);
    camera->GetViewUp(initCamUp);
    initCamSaved = true;

    /* 保存每个Actor的世界坐标
     * Save the world position of each actor */
    initActorPositions.resize(actorList.size());
    for (int i = 0; i < actorList.size(); ++i) {
        if (actorList[i]) {
            double p[3];
            actorList[i]->GetPosition(p);
            initActorPositions[i] = {p[0], p[1], p[2]};
        } else {
            initActorPositions[i] = {0.0, 0.0, 0.0};
        }
    }
}

void VRRenderThread::resetModelView(vtkCamera* camera,
                                     vtkRenderer* renderer,
                                     bool restoreCamera)
{
    /* 停止旋转动画
     * Stop rotation animation */
    isRotating    = false;
    rotationAngle = 0.0;

    /* 将每个Actor的位置和方向重置为场景初始化时保存的值
     * Reset each actor's position and orientation to the saved factory state */
    for (int i = 0; i < actorList.size(); ++i) {
        if (!actorList[i]) continue;
        actorList[i]->SetOrientation(0.0, 0.0, 0.0);  /* 清除所有旋转
                                                       * Clear all rotation */
        if (i < initActorPositions.size()) {
            actorList[i]->SetPosition(
                initActorPositions[i][0],
                initActorPositions[i][1],
                initActorPositions[i][2]);
        }
    }

    /* 仅在桌面模式下恢复相机(VR模式中用户物理站立,移动相机会产生不适感)
     * Only restore camera in desktop mode (in real VR moving the camera feels wrong) */
    if (restoreCamera && camera && renderer && initCamSaved) {
        camera->SetPosition(initCamPos);
        camera->SetFocalPoint(initCamFocal);
        camera->SetViewUp(initCamUp);
        renderer->ResetCameraClippingRange();
    }
}

void VRRenderThread::applyViewPreset(int index)
{
    /* 四个命名方向:通过SetOrientation(俯仰,偏航,滚转)应用
     * Four named orientations applied as SetOrientation(pitch, yaw, roll)
     *   0 - 正视图:模型正面朝向观察者
     *       Front: model faces viewer straight on
     *   1 - 顶视图:从上方俯视顶面
     *       Top: looking down at the top surface
     *   2 - 右视图:模型旋转90度展示右侧面
     *       Right Side: model turned 90 degrees to show right face
     *   3 - 等轴视图:经典3/4鸟瞰角度
     *       Isometric: classic 3/4 overview angle */
    struct Preset { double pitch, yaw, roll; };
    static const Preset presets[] = {
        {  0.0,   0.0, 0.0 },   /* 正视图
                                 * Front */
        { 90.0,   0.0, 0.0 },   /* 顶视图
                                 * Top */
        {  0.0, -90.0, 0.0 },   /* 右视图
                                 * Right Side */
        { 30.0,  45.0, 0.0 },   /* 等轴视图
                                 * Isometric */
    };

    if (index < 0 || index > 3) index = 0;  /* 越界则使用正视图
                                             * Clamp to Front */
    const Preset& p = presets[index];

    /* 对所有Actor应用相同的方向
     * Apply the same orientation to all actors */
    for (int i = 0; i < actorList.size(); ++i)
        if (actorList[i])
            actorList[i]->SetOrientation(p.pitch, p.yaw, p.roll);
}

/* ================================================================
 * 高亮辅助:选中/取消选中Actor
 * Highlight helper: select/deselect an actor
 *
 * 选中时:保存原始颜色,替换为亮黄色并显示边缘线。
 * When selecting: saves original colour, replaces with bright yellow and shows edge lines.
 * 取消时:恢复原始颜色,隐藏边缘线。
 * When deselecting: restores original colour, hides edge lines.
 * ================================================================ */

void VRRenderThread::highlightActor(int idx, bool on)
{
    if (idx < 0 || idx >= actorList.size() || !actorList[idx]) return;
    vtkProperty* prop = actorList[idx]->GetProperty();
    if (on) {
        /* 保存原始颜色(取消高亮时恢复)
         * Save the original colour (restored when unhighlighted) */
        prop->GetDiffuseColor(savedColor);
        /* 应用亮黄色高亮+加粗边缘线
         * Apply bright yellow highlight + thick edge lines */
        prop->SetDiffuseColor(1.0, 0.95, 0.0);
        prop->SetEdgeVisibility(1);
        prop->SetEdgeColor(1.0, 1.0, 0.0);
        prop->SetLineWidth(3.0);
    } else {
        /* 恢复原始颜色并隐藏边缘线
         * Restore original colour and hide edge lines */
        prop->SetDiffuseColor(savedColor);
        prop->SetEdgeVisibility(0);
        prop->SetLineWidth(1.0);
    }
}

/* ================================================================
 * 射线拾取辅助:屏幕坐标->Actor索引
 * Ray-picking helper: screen coordinates -> actor index
 * ================================================================ */

int VRRenderThread::pickActorAt(int x, int y, vtkRenderer* renderer)
{
    vtkNew<vtkPropPicker> picker;
    picker->Pick(x, y, 0, renderer);  /* z=0表示从屏幕平面投射射线
                                       * z=0 casts ray from screen plane */
    vtkActor* hit = picker->GetActor();
    if (!hit) return -1;
    /* 在actorList中查找命中的Actor
     * Search actorList for the hit actor */
    for (int i = 0; i < actorList.size(); ++i) {
        if (actorList[i] == hit) return i;
    }
    return -1;
}

/* ================================================================
 * 注册零件名称(用于VR内选中时在状态栏显示)
 * Register part name (for display in status bar when selected in VR)
 * ================================================================ */

void VRRenderThread::setActorName(int index, const QString& name)
{
    /* 扩展列表以容纳指定索引
     * Expand the list to accommodate the given index */
    while (actorNames.size() <= index) actorNames.append("");
    actorNames[index] = name;
}

/* ================================================================
 * 公共接口:添加Actor(线程启动前批量添加)
 * Public interface: add actor (batch add before thread starts)
 *
 * 为每个Actor预创建所有五种滤镜对象。
 * Pre-creates all five filter objects for each actor.
 * 滤镜对象始终存在,通过标志和rebuildPipeline()控制是否接入管线。
 * Filter objects always exist; flags + rebuildPipeline() control whether they are wired in.
 * ================================================================ */

int VRRenderThread::addActorOffline(vtkActor* actor,
                                    vtkSTLReader* reader,
                                    bool clipOn,
                                    bool shrinkOn,
                                    bool smoothOn,
                                    bool decimateOn,
                                    bool elevationOn,
                                    bool sliceOn)
{
    if (!actor) return -1;

    int idx = actorList.size();
    actorList.append(actor);
    readerList.append(vtkSmartPointer<vtkSTLReader>(reader));

    /* 裁剪滤镜:平面原点设为模型X中心,确保裁剪总是从中间切开
     * Clip filter: plane origin at model X-centre so cut always bisects the model */
    double xCentre = 0.0;
    if (reader) {
        reader->Update();
        double bounds[6];
        reader->GetOutput()->GetBounds(bounds);
        xCentre = (bounds[0] + bounds[1]) * 0.5;
    }

    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(xCentre, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);

    vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
    cf->SetClipFunction(clipPlane.Get());

    /* 收缩滤镜:系数0.6产生明显可见的间隙
     * Shrink filter: factor 0.6 creates clearly visible gaps */
    vtkSmartPointer<vtkShrinkPolyData> sf = vtkSmartPointer<vtkShrinkPolyData>::New();
    sf->SetShrinkFactor(0.6);

    clipFilters.append(cf);
    shrinkFilters.append(sf);

    /* 平滑滤镜
     * Smooth filter */
    vtkSmartPointer<vtkSmoothPolyDataFilter> smf = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    smf->SetNumberOfIterations(20);
    smf->SetRelaxationFactor(0.1);
    smf->FeatureEdgeSmoothingOff();
    smf->BoundarySmoothingOn();
    smoothFilters.append(smf);

    /* 抽取滤镜(需要GeometryFilter和CleanPolyData预处理)
     * Decimate filter (requires GeometryFilter and CleanPolyData preprocessing) */
    vtkSmartPointer<vtkGeometryFilter>  gf  = vtkSmartPointer<vtkGeometryFilter>::New();
    vtkSmartPointer<vtkCleanPolyData>   clf = vtkSmartPointer<vtkCleanPolyData>::New();
    vtkSmartPointer<vtkDecimatePro>     df  = vtkSmartPointer<vtkDecimatePro>::New();
    df->SetTargetReduction(0.9);
    df->PreserveTopologyOn();
    geometryFilters.append(gf);
    cleanFilters.append(clf);
    decimateFilters.append(df);

    /* 高度色彩滤镜+彩虹色表
     * Elevation filter + rainbow lookup table */
    vtkSmartPointer<vtkElevationFilter> ef = vtkSmartPointer<vtkElevationFilter>::New();
    if (reader) {
        double bounds[6];
        reader->GetOutput()->GetBounds(bounds);
        double zMin = bounds[4], zMax = bounds[5];
        if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }
        ef->SetLowPoint(0.0, 0.0, zMin);
        ef->SetHighPoint(0.0, 0.0, zMax);
    }
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetHueRange(0.667, 0.0);   /* 蓝色->红色
                                     * Blue -> red */
    lut->Build();
    elevationFilters.append(ef);
    elevationLUTs.append(lut);

    mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
    clipState.append(clipOn);
    shrinkState.append(shrinkOn);
    smoothState.append(smoothOn);
    decimateState.append(decimateOn);
    elevationState.append(elevationOn);
    sliceState.append(sliceOn);
    actorNames.append("");
    actorColorIdx.append(0);
    rebuildPipeline(idx);

    return idx;
}

void VRRenderThread::clearActors()
{
    /* 清空所有列表(不Delete Actor,VR重启时由外部管理)
     * Clear all lists (don't Delete actors; managed externally on VR restart) */
    actorList.clear();
    readerList.clear();
    mapperList.clear();
    clipFilters.clear();
    shrinkFilters.clear();
    smoothFilters.clear();
    decimateFilters.clear();
    elevationFilters.clear();
    elevationLUTs.clear();
    cleanFilters.clear();
    geometryFilters.clear();
    clipState.clear();
    shrinkState.clear();
    smoothState.clear();
    decimateState.clear();
    elevationState.clear();
    sliceState.clear();
    actorNames.clear();
    actorColorIdx.clear();
    selectedActorIndex = -1;
}

void VRRenderThread::issueCommand(int cmd, double value, int actorIndex)
{
    /* 线程安全地将命令推入队列(可从任意线程调用)
     * Thread-safe enqueue of a command (can be called from any thread) */
    QMutexLocker locker(&mutex);
    commandQueue.enqueue(VRCmd(cmd, value, actorIndex));
}

void VRRenderThread::queueAddActor(const ActorPackage& pkg)
{
    /* 线程安全地将新Actor推入待添加队列。
     * 渲染循环在下一帧调用processPendingActors时消费此队列。
     * Thread-safe push of a new actor to the pending queue.
     * The render loop consumes this queue on the next frame via processPendingActors. */
    QMutexLocker locker(&pendingMutex);
    pendingActors.enqueue(pkg);
}

/* ================================================================
 * run()入口:自动检测头显,选择VR或桌面模式
 * run() entry: auto-detect headset, choose VR or desktop mode
 * ================================================================ */

void VRRenderThread::run()
{
    /* 尝试初始化OpenVR运行时以检测头显是否连接
     * Attempt to initialise the OpenVR runtime to detect a connected headset */
    bool vrAvailable = false;
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);

    if (initError == vr::VRInitError_None && vrSystem != nullptr) {
        vrAvailable = true;
        /* 仅用于检测;关闭后由vtkOpenVRRenderWindow内部重新初始化
         * Only used for detection; closed so vtkOpenVRRenderWindow can reinitialise internally */
        vr::VR_Shutdown();
    }

    if (vrAvailable) {
        runVRMode();
    } else {
        runDesktopMode();
    }
}

/* ================================================================
 * VR模式渲染循环
 * VR mode render loop
 * ================================================================ */

void VRRenderThread::runVRMode()
{
    vtkNew<vtkOpenVRRenderer>               renderer;
    vtkNew<vtkOpenVRRenderWindow>           renderWindow;
    vtkNew<vtkOpenVRRenderWindowInteractor> interactor;
    vtkNew<vtkOpenVRCamera>                 camera;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(2160, 1200);  /* HTC Vive分辨率
                                         * HTC Vive resolution */
    interactor->SetRenderWindow(renderWindow);
    renderer->SetActiveCamera(camera);
    renderer->SetBackground(0.05, 0.05, 0.15);  /* 深蓝色背景(Skybox会覆盖此颜色)
                                                 * Deep blue (Skybox will override) */

    /* 将启动时已注册的所有模型Actor加入渲染器
     * Add all model actors registered before startup to the renderer */
    for (vtkActor* actor : actorList) {
        if (actor) renderer->AddActor(actor);
    }

    /* 场景初始化顺序:地板->光照->Skybox
     * Scene init order: floor -> lighting -> skybox */
    setupFloor(renderer.Get());
    setupLighting(renderer.Get());
    setupSkybox(renderer.Get(), renderWindow.Get());

    /* ---- 动作清单必须在Initialize()之前设置
     *      Action manifest must be set before Initialize()
     * SetActionManifestDirectory指定vrbindings目录,SteamVR从中找到动作绑定文件。
     * SetActionManifestDirectory specifies the vrbindings directory where SteamVR finds action binding files.
     * 两者都要设置,否则手柄无射线输入。
     * Both must be set, otherwise controllers have no ray input. */
    QString bindingsDir  = QCoreApplication::applicationDirPath() + "/vrbindings";
    if (!QDir(bindingsDir).exists()) {
        bindingsDir = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../vrbindings");
    }
    QString manifestPath = bindingsDir + "/vtk_openvr_actions.json";
    interactor->SetActionManifestDirectory(bindingsDir.toStdString());
    interactor->SetActionManifestFileName(manifestPath.toStdString());

    /* 初始化顺序:先renderWindow建立OpenVR会话,再interactor注册动作集
     * Init order: renderWindow first (establishes OpenVR session), interactor second (registers action set) */
    renderWindow->Initialize();
    interactor->Initialize();

    /* ---- 相机初始化:让模型出现在用户正前方
     *      Camera init: position model in front of the user ---- */
    {
        double bounds[6] = {0,0,0,0,0,0};
        bool hasBounds = false;
        vtkActorCollection* acs = renderer->GetActors();
        acs->InitTraversal();
        while (vtkActor* a = acs->GetNextActor()) {
            double b[6]; a->GetBounds(b);
            if (b[0] > b[1]) continue;  /* 跳过无效包围盒
                                         * Skip invalid bounds */
            if (!hasBounds) {
                for (int i = 0; i < 6; ++i) bounds[i] = b[i];
                hasBounds = true;
            } else {
                if (b[0]<bounds[0]) bounds[0]=b[0];
                if (b[1]>bounds[1]) bounds[1]=b[1];
                if (b[2]<bounds[2]) bounds[2]=b[2];
                if (b[3]>bounds[3]) bounds[3]=b[3];
                if (b[4]<bounds[4]) bounds[4]=b[4];
                if (b[5]>bounds[5]) bounds[5]=b[5];
            }
        }
        if (hasBounds) {
            double cx = (bounds[0] + bounds[1]) / 2.0;
            double cy = (bounds[2] + bounds[3]) / 2.0;
            double cz = (bounds[4] + bounds[5]) / 2.0;
            double maxSize = std::max({bounds[1]-bounds[0],
                                       bounds[3]-bounds[2],
                                       bounds[5]-bounds[4]});
            double camDist = maxSize * 1.2;  /* 相机距离=模型最大尺寸的1.2倍
                                              * Camera distance = 1.2x model size */
            renderer->GetActiveCamera()->SetPosition(cx, cy, cz + camDist);
            renderer->GetActiveCamera()->SetFocalPoint(cx, cy, cz);
            renderer->GetActiveCamera()->SetViewUp(0.0, 1.0, 0.0);
            renderer->ResetCameraClippingRange();
            /* 保存场景初始状态(地板平移后Actor的坐标)
             * Save factory state (actor coordinates after floor shift) */
            saveFactoryState(renderer->GetActiveCamera());
        } else {
            renderer->ResetCamera();
        }
    }

    /* ================================================================
     * 手柄交互:注册VTK 3D事件观察者
     * Controller interaction: register VTK 3D event observers
     *
     * 使用vtkCommand::Button3DEvent和Move3DEvent而非已弃用的GetControllerState()。
     * Uses vtkCommand::Button3DEvent and Move3DEvent instead of deprecated GetControllerState().
     *
     * Button3DEvent:每次手柄按键按下/释放时触发
     *               Fires on every controller button press/release
     *   - 回调数据:vtkEventDataDevice3D* 携带按键类型、动作、世界坐标和方向
     *     Calldata: vtkEventDataDevice3D* carrying button type, action, world pos and direction
     *
     * Move3DEvent:每帧手柄移动时触发
     *             Fires each frame the controller moves
     *
     * 按键映射(HTC Vive):
     * Button mapping (HTC Vive):
     *   Trigger按下  -> 射线拾取 + 开始拖动
     *                   Ray pick + start drag
     *   Trigger释放  -> 停止拖动
     *                   Stop drag
     *   Grip按下     -> 取消选中
     *                   Deselect
     * ================================================================ */

    /* 保存渲染器指针供成员函数回调使用
     * Save renderer pointer for member-function callbacks */
    vrPickRenderer = renderer.Get();
    vrPicker = vtkSmartPointer<vtkPicker>::New();
    vrPicker->SetTolerance(0.025);

    /* Button3DEvent观察者:处理Trigger按下/释放和Grip按下
     * Button3DEvent observer: handle Trigger press/release and Grip press */
    vtkNew<vtkCallbackCommand> btnCb;
    btnCb->SetClientData(this);
    btnCb->SetCallback([](vtkObject*, unsigned long, void* cd, void* callData) {
        auto* self = static_cast<VRRenderThread*>(cd);
        auto* ed   = static_cast<vtkEventDataDevice3D*>(callData);
        if (!ed) return;

        using Input  = vtkEventDataDeviceInput;
        using Action = vtkEventDataAction;

        Input  inp = ed->GetInput();
        Action act = ed->GetAction();

        if (inp == Input::Trigger && act == Action::Press)
            self->onVRTriggerPress(ed, self->vrPickRenderer);
        else if (inp == Input::Trigger && act == Action::Release)
            self->onVRTriggerRelease();
        else if (inp == Input::Grip && act == Action::Press) {
            /* Grip按下:停止拖动并取消选中
             * Grip press: stop drag and deselect */
            self->onVRTriggerRelease();
            if (self->selectedActorIndex >= 0) {
                self->highlightActor(self->selectedActorIndex, false);
                self->selectedActorIndex = -1;
                emit self->vrActorSelected(-1, "");
            }
        }
    });
    interactor->AddObserver(vtkCommand::Button3DEvent, btnCb);

    /* Move3DEvent观察者:每帧平移被拖动的Actor
     * Move3DEvent observer: translate dragged actor each frame */
    vtkNew<vtkCallbackCommand> moveCb;
    moveCb->SetClientData(this);
    moveCb->SetCallback([](vtkObject*, unsigned long, void* cd, void* callData) {
        auto* self = static_cast<VRRenderThread*>(cd);
        auto* ed   = static_cast<vtkEventDataDevice3D*>(callData);
        if (ed) self->onVRControllerMove(ed);
    });
    interactor->AddObserver(vtkCommand::Move3DEvent, moveCb);

    /* ---- 主渲染循环
     *      Main render loop ---- */
    while (!isInterruptionRequested()) {

        /* 消费GUI命令队列(批量取出以减少锁时间)
         * Consume the GUI command queue (batch dequeue to minimize lock time) */
        {
            mutex.lock();
            QQueue<VRCmd> localQueue;
            localQueue.swap(commandQueue);
            mutex.unlock();
            while (!localQueue.isEmpty())
                processCommandVR(localQueue.dequeue(), renderer.Get());
        }

        /* 处理动态添加的Actor
         * Process dynamically added actors */
        processPendingActorsVR(renderer.Get());

        /* 旋转动画:每帧增加0.5度
         * Rotation animation: increment by 0.5 degrees per frame */
        if (isRotating) {
            rotationAngle += 0.5;
            if (rotationAngle >= 360.0) rotationAngle -= 360.0;
            for (int i = 0; i < actorList.size(); ++i)
                if (actorList[i])
                    actorList[i]->SetOrientation(0, rotationAngle, 0);
        }

        renderWindow->Render();
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    interactor->TerminateApp();
    renderWindow->Finalize();
}

/* ================================================================
 * 桌面fallback模式渲染循环
 * Desktop fallback mode render loop
 *
 * 无头显时使用此模式进行预览和功能验证。
 * Used for preview and feature verification when no headset is connected.
 * ================================================================ */

void VRRenderThread::runDesktopMode()
{
    vtkNew<vtkRenderer>               renderer;
    vtkNew<vtkRenderWindow>           renderWindow;
    vtkNew<vtkRenderWindowInteractor> interactor;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(800, 600);
    renderWindow->SetWindowName("VR Preview Mode (No Headset Connected)");
    interactor->SetRenderWindow(renderWindow);
    renderer->SetBackground(0.05, 0.05, 0.15);

    for (vtkActor* actor : actorList) {
        if (actor) renderer->AddActor(actor);
    }

    /* 场景初始化:与VR模式完全相同
     * Scene init: identical to VR mode */
    setupFloorDesktop(renderer.Get());
    setupLightingDesktop(renderer.Get());
    setupSkyboxDesktop(renderer.Get(), renderWindow.Get());

    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();
    saveFactoryState(renderer->GetActiveCamera());

    /* ---- 桌面模式交互:鼠标点击拾取 + 键盘快捷键
     *      Desktop mode interaction: mouse click picking + keyboard shortcuts ---- */
    struct PickState {
        bool clickPending = false;
        int  clickX = 0, clickY = 0;
        char keyPending = 0;
    };
    auto pickState = std::make_shared<PickState>();

    /* 鼠标左键点击回调:记录点击坐标
     * Left mouse button callback: record click coordinates */
    vtkNew<vtkCallbackCommand> clickCb;
    clickCb->SetCallback([](vtkObject* caller, unsigned long, void* cd, void*) {
        auto* inter = static_cast<vtkRenderWindowInteractor*>(caller);
        auto* ps    = static_cast<PickState*>(cd);
        ps->clickPending = true;
        ps->clickX = inter->GetEventPosition()[0];
        ps->clickY = inter->GetEventPosition()[1];
    });
    clickCb->SetClientData(pickState.get());
    interactor->AddObserver(vtkCommand::LeftButtonPressEvent, clickCb);

    /* 键盘按键回调:记录按键字符
     * Keyboard press callback: record key character */
    vtkNew<vtkCallbackCommand> keyCb;
    keyCb->SetCallback([](vtkObject* caller, unsigned long, void* cd, void*) {
        auto* inter = static_cast<vtkRenderWindowInteractor*>(caller);
        auto* ps    = static_cast<PickState*>(cd);
        ps->keyPending = inter->GetKeyCode();
    });
    keyCb->SetClientData(pickState.get());
    interactor->AddObserver(vtkCommand::KeyPressEvent, keyCb);

    renderWindow->Render();

    /* ---- 桌面渲染循环
     *      Desktop render loop ---- */
    while (!isInterruptionRequested()) {

        /* 消费命令队列
         * Consume command queue */
        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        while (!localQueue.isEmpty()) {
            processCommandDesktop(localQueue.dequeue(), renderer.Get());
        }

        processPendingActorsDesktop(renderer.Get());

        /* 旋转动画
         * Rotation animation */
        if (isRotating) {
            rotationAngle += 0.5;
            if (rotationAngle >= 360.0) rotationAngle -= 360.0;
            for (int i = 0; i < actorList.size(); ++i) {
                if (actorList[i]) {
                    actorList[i]->SetOrientation(0, rotationAngle, 0);
                }
            }
        }

        renderWindow->Render();
        interactor->ProcessEvents();

        /* ---- 处理鼠标点击拾取
         *      Handle mouse click picking ---- */
        if (pickState->clickPending) {
            pickState->clickPending = false;
            int hit = pickActorAt(pickState->clickX, pickState->clickY, renderer.Get());

            if (hit >= 0 && hit == selectedActorIndex) {
                /* 再次点击同一Actor:取消选中
                 * Click same actor again: deselect */
                highlightActor(selectedActorIndex, false);
                selectedActorIndex = -1;
                emit vrActorSelected(-1, "");
            } else {
                /* 点击不同Actor或空白区域
                 * Click different actor or empty area */
                if (selectedActorIndex >= 0)
                    highlightActor(selectedActorIndex, false);
                selectedActorIndex = hit;
                if (hit >= 0) {
                    highlightActor(hit, true);
                    QString name = (hit < actorNames.size())
                                   ? actorNames[hit]
                                   : QString("Actor %1").arg(hit);
                    emit vrActorSelected(hit, name);
                } else {
                    emit vrActorSelected(-1, "");
                }
            }
        }

        /* ---- 键盘快捷键(仅在有Actor被选中时生效)
         *      Keyboard shortcuts (only active when an actor is selected)
         *   V=切换可见性  S=切换Slice  K=切换Shrink  C=循环颜色
         *   V=toggle Visible  S=toggle Slice  K=toggle Shrink  C=cycle Colour ---- */
        if (pickState->keyPending != 0 && selectedActorIndex >= 0) {
            char key = pickState->keyPending;
            int  si  = selectedActorIndex;
            switch (key) {
            case 'v': case 'V':
                /* 切换可见性
                 * Toggle visibility */
                if (actorList[si])
                    actorList[si]->SetVisibility(!actorList[si]->GetVisibility());
                break;
            case 's': case 'S':
                /* 切换截面滤镜
                 * Toggle slice filter */
                sliceState[si] = !sliceState[si];
                if (sliceState[si]) smoothState[si] = false;
                rebuildPipeline(si);
                break;
            case 'k': case 'K':
                /* 切换收缩滤镜
                 * Toggle shrink filter */
                shrinkState[si] = !shrinkState[si];
                if (shrinkState[si]) smoothState[si] = false;
                rebuildPipeline(si);
                break;
            case 'c': case 'C': {
                /* 循环切换颜色
                 * Cycle through colour table */
                while (actorColorIdx.size() <= si) actorColorIdx.append(0);
                actorColorIdx[si] = (actorColorIdx[si] + 1) % COLOR_COUNT;
                int ci = actorColorIdx[si];
                actorList[si]->GetProperty()->SetDiffuseColor(
                    colorTable[ci][0] / 255.0,
                    colorTable[ci][1] / 255.0,
                    colorTable[ci][2] / 255.0);
                /* 同步savedColor,确保取消高亮时恢复正确颜色
                 * Sync savedColor so unhighlighting restores the correct colour */
                savedColor[0] = colorTable[ci][0] / 255.0;
                savedColor[1] = colorTable[ci][1] / 255.0;
                savedColor[2] = colorTable[ci][2] / 255.0;
                break;
            }
            default: break;
            }
        }
        pickState->keyPending = 0;

        /* 窗口关闭检测(尺寸变为0×0表示窗口已关闭)
         * Window close detection (size 0×0 means the window was closed) */
        if (renderWindow->GetSize()[0] == 0 && renderWindow->GetSize()[1] == 0) {
            break;
        }

        QThread::msleep(16);  /* 约60fps
                               * ~60fps */
    }
}

/* ================================================================
 * 动态Actor队列处理
 * Dynamic actor queue processing
 *
 * 每帧从待添加队列取出Actor并注册到渲染器。
 * Each frame, dequeue actors from the pending queue and add them to the renderer.
 * 批量处理以减少锁竞争。
 * Batch processing to reduce lock contention.
 * ================================================================ */

void VRRenderThread::processPendingActorsVR(vtkOpenVRRenderer* renderer)
{
    /* 批量取出所有待添加Actor
     * Batch dequeue all pending actors */
    pendingMutex.lock();
    QQueue<ActorPackage> localPending;
    localPending.swap(pendingActors);
    pendingMutex.unlock();

    while (!localPending.isEmpty()) {
        ActorPackage pkg = localPending.dequeue();
        if (!pkg.actor) continue;

        /* 为新Actor创建裁剪滤镜(平面原点设为模型X中心)
         * Create clip filter for new actor (plane origin at model X-centre) */
        double cpXCentre = 0.0;
        if (pkg.reader) {
            double b[6];
            pkg.reader->GetOutput()->GetBounds(b);
            cpXCentre = (b[0] + b[1]) * 0.5;
        }
        vtkSmartPointer<vtkPlane> cp = vtkSmartPointer<vtkPlane>::New();
        cp->SetOrigin(cpXCentre, 0.0, 0.0);
        cp->SetNormal(-1.0, 0.0, 0.0);

        vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
        cf->SetClipFunction(cp.Get());

        vtkSmartPointer<vtkShrinkPolyData> sf = vtkSmartPointer<vtkShrinkPolyData>::New();
        sf->SetShrinkFactor(0.6);

        vtkSmartPointer<vtkSmoothPolyDataFilter> smf = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
        smf->SetNumberOfIterations(20);
        smf->SetRelaxationFactor(0.1);
        smf->FeatureEdgeSmoothingOff();
        smf->BoundarySmoothingOn();

        vtkSmartPointer<vtkGeometryFilter> gf = vtkSmartPointer<vtkGeometryFilter>::New();
        vtkSmartPointer<vtkCleanPolyData> clf = vtkSmartPointer<vtkCleanPolyData>::New();
        vtkSmartPointer<vtkDecimatePro> df = vtkSmartPointer<vtkDecimatePro>::New();
        df->SetTargetReduction(0.9);
        df->PreserveTopologyOn();

        double zMin = 0.0, zMax = 1.0;
        if (pkg.reader) {
            double b[6];
            pkg.reader->GetOutput()->GetBounds(b);
            zMin = b[4];
            zMax = b[5];
            if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }
        }
        vtkSmartPointer<vtkElevationFilter> ef = vtkSmartPointer<vtkElevationFilter>::New();
        ef->SetLowPoint(0.0, 0.0, zMin);
        ef->SetHighPoint(0.0, 0.0, zMax);

        vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
        lut->SetNumberOfTableValues(256);
        lut->SetHueRange(0.667, 0.0);
        lut->Build();

        /* 注册到内部列表
         * Register to internal lists */
        actorList.append(pkg.actor);
        readerList.append(pkg.reader);
        mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
        clipFilters.append(cf);
        shrinkFilters.append(sf);
        smoothFilters.append(smf);
        geometryFilters.append(gf);
        cleanFilters.append(clf);
        decimateFilters.append(df);
        elevationFilters.append(ef);
        elevationLUTs.append(lut);
        clipState.append(pkg.clipOn);
        shrinkState.append(pkg.shrinkOn);
        smoothState.append(pkg.smoothOn);
        decimateState.append(pkg.decimateOn);
        elevationState.append(pkg.elevationOn);
        sliceState.append(pkg.sliceOn);
        actorNames.append("");
        actorColorIdx.append(0);
        rebuildPipeline(actorList.size() - 1);

        /* 将新Actor加入渲染器(下一帧立即生效)
         * Add new actor to renderer (takes effect next frame) */
        renderer->AddActor(pkg.actor);
    }
}

void VRRenderThread::processPendingActorsDesktop(vtkRenderer* renderer)
{
    /* 与VR版逻辑完全相同,仅renderer类型不同
     * Same logic as VR version, only renderer type differs */
    pendingMutex.lock();
    QQueue<ActorPackage> localPending;
    localPending.swap(pendingActors);
    pendingMutex.unlock();

    while (!localPending.isEmpty()) {
        ActorPackage pkg = localPending.dequeue();
        if (!pkg.actor) continue;

        double cpXCentre = 0.0;
        if (pkg.reader) {
            double b[6];
            pkg.reader->GetOutput()->GetBounds(b);
            cpXCentre = (b[0] + b[1]) * 0.5;
        }
        vtkSmartPointer<vtkPlane> cp = vtkSmartPointer<vtkPlane>::New();
        cp->SetOrigin(cpXCentre, 0.0, 0.0);
        cp->SetNormal(-1.0, 0.0, 0.0);

        vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
        cf->SetClipFunction(cp.Get());

        vtkSmartPointer<vtkShrinkPolyData> sf = vtkSmartPointer<vtkShrinkPolyData>::New();
        sf->SetShrinkFactor(0.6);

        vtkSmartPointer<vtkSmoothPolyDataFilter> smf = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
        smf->SetNumberOfIterations(20);
        smf->SetRelaxationFactor(0.1);
        smf->FeatureEdgeSmoothingOff();
        smf->BoundarySmoothingOn();

        vtkSmartPointer<vtkGeometryFilter> gf = vtkSmartPointer<vtkGeometryFilter>::New();
        vtkSmartPointer<vtkCleanPolyData> clf = vtkSmartPointer<vtkCleanPolyData>::New();
        vtkSmartPointer<vtkDecimatePro> df = vtkSmartPointer<vtkDecimatePro>::New();
        df->SetTargetReduction(0.9);
        df->PreserveTopologyOn();

        double zMin = 0.0, zMax = 1.0;
        if (pkg.reader) {
            double b[6];
            pkg.reader->GetOutput()->GetBounds(b);
            zMin = b[4];
            zMax = b[5];
            if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }
        }
        vtkSmartPointer<vtkElevationFilter> ef = vtkSmartPointer<vtkElevationFilter>::New();
        ef->SetLowPoint(0.0, 0.0, zMin);
        ef->SetHighPoint(0.0, 0.0, zMax);

        vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
        lut->SetNumberOfTableValues(256);
        lut->SetHueRange(0.667, 0.0);
        lut->Build();

        actorList.append(pkg.actor);
        readerList.append(pkg.reader);
        mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
        clipFilters.append(cf);
        shrinkFilters.append(sf);
        smoothFilters.append(smf);
        geometryFilters.append(gf);
        cleanFilters.append(clf);
        decimateFilters.append(df);
        elevationFilters.append(ef);
        elevationLUTs.append(lut);
        clipState.append(pkg.clipOn);
        shrinkState.append(pkg.shrinkOn);
        smoothState.append(pkg.smoothOn);
        decimateState.append(pkg.decimateOn);
        elevationState.append(pkg.elevationOn);
        sliceState.append(pkg.sliceOn);
        actorNames.append("");
        actorColorIdx.append(0);
        rebuildPipeline(actorList.size() - 1);

        renderer->AddActor(pkg.actor);
    }
}

/* ================================================================
 * VR手柄事件处理函数
 * VR controller event handler functions
 *
 * 由runVRMode()中注册的静态VTK回调调用。
 * Called from static VTK callbacks registered in runVRMode().
 * ================================================================ */

void VRRenderThread::onVRTriggerPress(vtkEventDataDevice3D* ed,
                                       vtkOpenVRRenderer* ren)
{
    if (!ed || !ren || !vrPicker) return;

    /* 从事件数据获取手柄世界坐标和指向方向
     * Get controller world position and pointing direction from event data */
    const double* pos = ed->GetWorldPosition();
    const double* dir = ed->GetWorldDirection();

    /* 沿射线方向投射10000单位,计算射线终点
     * Project 10000 units along ray direction to compute ray end */
    const double RAY = 10000.0;
    double rayEnd[3] = { pos[0]+dir[0]*RAY, pos[1]+dir[1]*RAY, pos[2]+dir[2]*RAY };
    double p0[3]     = { pos[0], pos[1], pos[2] };

    /* 三维射线与模型Actor做相交测试。
     * Pick3DPoint() accepts a start/end world segment; Pick3DRay() expects an orientation quaternion.
     * Cast the 3D ray against model actors.
     * Pick3DPoint() accepts a start/end world segment; Pick3DRay() expects an orientation quaternion. */
    vrPicker->InitializePickList();
    for (vtkActor* actor : actorList) {
        if (actor) vrPicker->AddPickList(actor);
    }
    vrPicker->PickFromListOn();
    vrPicker->Pick3DPoint(p0, rayEnd, ren);
    vtkActor* hit = vrPicker->GetActor();

    /* 在actorList中查找命中的Actor
     * Look up the hit actor in actorList */
    int hitIdx = -1;
    if (hit)
        for (int i = 0; i < actorList.size(); ++i)
            if (actorList[i] == hit) { hitIdx = i; break; }

    /* 取消前一个选中的高亮(如果选中了不同Actor)
     * Un-highlight previously selected actor (if a different one was hit) */
    if (selectedActorIndex >= 0 && selectedActorIndex != hitIdx)
        highlightActor(selectedActorIndex, false);

    if (hitIdx >= 0) {
        /* 命中Actor:高亮并开始拖动
         * Hit an actor: highlight and start dragging */
        selectedActorIndex = hitIdx;
        highlightActor(hitIdx, true);
        isDragging         = true;
        dragActorIndex     = hitIdx;
        vrDragLastPos[0]   = pos[0];
        vrDragLastPos[1]   = pos[1];
        vrDragLastPos[2]   = pos[2];

        QString name = (hitIdx < actorNames.size())
                       ? actorNames[hitIdx]
                       : QString("Actor %1").arg(hitIdx);
        emit vrActorSelected(hitIdx, name);
    } else {
        /* 未命中:取消选中
         * No hit: deselect */
        selectedActorIndex = -1;
        isDragging         = false;
        dragActorIndex     = -1;
        emit vrActorSelected(-1, "");
    }
}

void VRRenderThread::onVRTriggerRelease()
{
    /* Trigger释放:停止拖动(保留选中高亮)
     * Trigger release: stop dragging (keep selection highlight) */
    isDragging     = false;
    dragActorIndex = -1;
}

void VRRenderThread::onVRControllerMove(vtkEventDataDevice3D* ed)
{
    if (!isDragging || dragActorIndex < 0 || !ed) return;
    if (dragActorIndex >= actorList.size() || !actorList[dragActorIndex]) return;

    const double* pos = ed->GetWorldPosition();

    /* 计算手柄位移增量并叠加到Actor位置。
     * 增量方案比绝对坐标方案更稳定,防止手柄抖动导致Actor跳跃。
     * Calculate position delta and add to actor position.
     * Delta approach is more stable than absolute coordinates, preventing jitter jumps. */
    double dx = pos[0] - vrDragLastPos[0];
    double dy = pos[1] - vrDragLastPos[1];
    double dz = pos[2] - vrDragLastPos[2];

    double* cur = actorList[dragActorIndex]->GetPosition();
    actorList[dragActorIndex]->SetPosition(cur[0]+dx, cur[1]+dy, cur[2]+dz);

    /* 更新上一帧手柄位置
     * Update previous frame controller position */
    vrDragLastPos[0] = pos[0];
    vrDragLastPos[1] = pos[1];
    vrDragLastPos[2] = pos[2];
}

/* ================================================================
 * 命令处理辅助函数
 * Command processing helper function
 *
 * 对单个Actor(精确索引)或所有Actor(idx=-1)设置可见性。
 * Sets visibility on a single actor (by exact index) or all actors (idx=-1).
 * ================================================================ */

static void applyVisibility(QList<vtkActor*>& actors, int idx, bool visible)
{
    if (idx >= 0 && idx < actors.size()) {
        /* 精确定位:只改指定Actor
         * Precise targeting: only modify the specified actor */
        if (actors[idx]) actors[idx]->SetVisibility(visible ? 1 : 0);
    } else {
        /* idx==-1:批量设置全部Actor
         * idx==-1: batch set all actors */
        for (vtkActor* a : actors) {
            if (a) a->SetVisibility(visible ? 1 : 0);
        }
    }
}

/* ================================================================
 * 命令处理——VR模式
 * Command processing — VR mode
 *
 * 每帧从commandQueue取出命令逐条处理。
 * Commands are dequeued and processed one by one each frame.
 * ================================================================ */

void VRRenderThread::processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer)
{
    switch (vcmd.cmd) {

    case CMD_SET_VISIBLE:
        /* 设置可见性:actorIndex指定目标,-1表示全部
         * Set visibility: actorIndex specifies target, -1 means all */
        applyVisibility(actorList, vcmd.actorIndex, vcmd.value > 0.5);
        break;

    case CMD_APPLY_FILTER: {
        /* 解码滤镜命令:value = filterType * 10 + (enabled ? 1 : 0)
         * Decode filter command: value = filterType * 10 + (enabled ? 1 : 0) */
        int encoded    = static_cast<int>(vcmd.value + 0.5);
        int filterType = encoded / 10;
        bool enabled   = (encoded % 10) != 0;
        int idx        = vcmd.actorIndex;

        if (idx < 0 || idx >= actorList.size()) break;

        /* 更新对应滤镜状态标志
         * Update the corresponding filter state flag */
        if (filterType == FILTER_CLIP) {
            clipState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        if (filterType == FILTER_SHRINK) {
            shrinkState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        if (filterType == FILTER_SMOOTH) {
            smoothState[idx] = enabled;
            if (enabled) {
                clipState[idx] = false;
                shrinkState[idx] = false;
                sliceState[idx] = false;
            }
        }
        if (filterType == FILTER_DECIMATE)  decimateState[idx]  = enabled;
        if (filterType == FILTER_ELEVATION) elevationState[idx] = enabled;
        if (filterType == FILTER_SLICE) {
            sliceState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        rebuildPipeline(idx);
        break;
    }

    case CMD_REMOVE_ACTOR: {
        /* 从渲染器和内部列表中移除指定Actor
         * Remove the specified actor from the renderer and internal lists */
        int idx = vcmd.actorIndex;
        if (idx < 0 || idx >= actorList.size()) break;

        vtkActor* a = actorList[idx];
        if (a && renderer) {
            renderer->RemoveActor(a);
            a->Delete();  /* 释放getNewActor()分配的内存
                           * Free memory allocated by getNewActor() */
        }

        /* 用nullptr占位,保持其他Actor的索引不变
         * Replace with nullptr to preserve other actors' indices */
        actorList[idx]     = nullptr;
        readerList[idx]    = nullptr;
        clipFilters[idx]   = nullptr;
        shrinkFilters[idx] = nullptr;
        smoothFilters[idx] = nullptr;
        geometryFilters[idx] = nullptr;
        cleanFilters[idx] = nullptr;
        decimateFilters[idx] = nullptr;
        elevationFilters[idx] = nullptr;
        elevationLUTs[idx] = nullptr;
        clipState[idx] = false;
        shrinkState[idx] = false;
        smoothState[idx] = false;
        decimateState[idx] = false;
        elevationState[idx] = false;
        sliceState[idx] = false;
        break;
    }

    case CMD_SET_LIGHT_INTENSITY:
        /* 实时调整主光源强度(value范围:0.0~2.0)
         * Real-time main light intensity adjustment (value range: 0.0~2.0) */
        mainLightIntensity = vcmd.value;
        if (mainLight) {
            mainLight->SetIntensity(mainLightIntensity);
        }
        break;

    case CMD_START_ROTATE:
        isRotating = true;
        break;

    case CMD_STOP_ROTATE:
        isRotating = false;
        break;

    case CMD_RESET_VIEW:
        /* 在真实VR中不恢复相机(用户物理站立,移动相机会产生不适)
         * Don't restore camera in real VR (user stands physically; moving camera feels wrong) */
        resetModelView(renderer->GetActiveCamera(), renderer,
                       /* 恢复相机
                        * restoreCamera */ false);
        break;

    case CMD_SET_VIEW:
        /* 应用命名视图预设(0=正视图,1=顶视图,2=右视图,3=等轴视图)
         * Apply named view preset (0=Front, 1=Top, 2=Right, 3=Isometric) */
        applyViewPreset(static_cast<int>(vcmd.value + 0.5));
        break;

    /* 颜色通过共享vtkProperty自动同步,无需命令处理
     * Colour syncs automatically via shared vtkProperty, no command needed */
    case CMD_SET_COLOUR_R:
    case CMD_SET_COLOUR_G:
    case CMD_SET_COLOUR_B:
        break;

    default:
        break;
    }
}

/* ================================================================
 * 命令处理——桌面模式
 * Command processing — desktop mode
 *
 * 逻辑与VR模式基本相同,但桌面模式重置视图时也恢复相机。
 * Logic is mostly identical to VR mode, but desktop mode also restores the camera on reset.
 * ================================================================ */

void VRRenderThread::processCommandDesktop(const VRCmd& vcmd, vtkRenderer* renderer)
{
    switch (vcmd.cmd) {

    case CMD_SET_VISIBLE:
        applyVisibility(actorList, vcmd.actorIndex, vcmd.value > 0.5);
        break;

    case CMD_APPLY_FILTER: {
        int encoded    = static_cast<int>(vcmd.value + 0.5);
        int filterType = encoded / 10;
        bool enabled   = (encoded % 10) != 0;
        int idx        = vcmd.actorIndex;

        if (idx < 0 || idx >= actorList.size()) break;

        if (filterType == FILTER_CLIP) {
            clipState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        if (filterType == FILTER_SHRINK) {
            shrinkState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        if (filterType == FILTER_SMOOTH) {
            smoothState[idx] = enabled;
            if (enabled) {
                clipState[idx] = false;
                shrinkState[idx] = false;
                sliceState[idx] = false;
            }
        }
        if (filterType == FILTER_DECIMATE)  decimateState[idx]  = enabled;
        if (filterType == FILTER_ELEVATION) elevationState[idx] = enabled;
        if (filterType == FILTER_SLICE) {
            sliceState[idx] = enabled;
            if (enabled) smoothState[idx] = false;
        }
        rebuildPipeline(idx);
        break;
    }

    case CMD_REMOVE_ACTOR: {
        int idx = vcmd.actorIndex;
        if (idx < 0 || idx >= actorList.size()) break;

        vtkActor* a = actorList[idx];
        if (a && renderer) {
            renderer->RemoveActor(a);
            a->Delete();
        }

        actorList[idx]     = nullptr;
        readerList[idx]    = nullptr;
        clipFilters[idx]   = nullptr;
        shrinkFilters[idx] = nullptr;
        smoothFilters[idx] = nullptr;
        geometryFilters[idx] = nullptr;
        cleanFilters[idx] = nullptr;
        decimateFilters[idx] = nullptr;
        elevationFilters[idx] = nullptr;
        elevationLUTs[idx] = nullptr;
        clipState[idx] = false;
        shrinkState[idx] = false;
        smoothState[idx] = false;
        decimateState[idx] = false;
        elevationState[idx] = false;
        sliceState[idx] = false;
        break;
    }

    case CMD_SET_LIGHT_INTENSITY:
        mainLightIntensity = vcmd.value;
        if (mainLight) mainLight->SetIntensity(mainLightIntensity);
        break;

    /* VR内选中属性修改命令(桌面模式也支持,用于键盘控制)
     * In-VR property modification commands (also supported in desktop for keyboard control) */
    case CMD_VR_SELECT_ACTOR: {
        int idx = vcmd.actorIndex;
        if (selectedActorIndex >= 0) highlightActor(selectedActorIndex, false);
        selectedActorIndex = idx;
        if (idx >= 0) highlightActor(idx, true);
        break;
    }
    case CMD_VR_DESELECT:
        if (selectedActorIndex >= 0) highlightActor(selectedActorIndex, false);
        selectedActorIndex = -1;
        break;

    case CMD_VR_TOGGLE_VISIBLE: {
        int si = selectedActorIndex;
        if (si >= 0 && si < actorList.size() && actorList[si]) {
            bool vis = actorList[si]->GetVisibility();
            actorList[si]->SetVisibility(!vis);
        }
        break;
    }
    case CMD_VR_TOGGLE_SLICE: {
        int si = selectedActorIndex;
        if (si >= 0 && si < actorList.size()) {
            sliceState[si] = !sliceState[si];
            if (sliceState[si]) smoothState[si] = false;
            rebuildPipeline(si);
        }
        break;
    }
    case CMD_VR_TOGGLE_SHRINK: {
        int si = selectedActorIndex;
        if (si >= 0 && si < actorList.size()) {
            shrinkState[si] = !shrinkState[si];
            if (shrinkState[si]) smoothState[si] = false;
            rebuildPipeline(si);
        }
        break;
    }
    case CMD_VR_SET_COLOUR: {
        int si = selectedActorIndex;
        if (si >= 0 && si < actorList.size() && actorList[si]) {
            while (actorColorIdx.size() <= si) actorColorIdx.append(0);
            actorColorIdx[si] = (actorColorIdx[si] + 1) % COLOR_COUNT;
            int ci = actorColorIdx[si];
            actorList[si]->GetProperty()->SetDiffuseColor(
                colorTable[ci][0]/255.0,
                colorTable[ci][1]/255.0,
                colorTable[ci][2]/255.0);
            savedColor[0] = colorTable[ci][0]/255.0;
            savedColor[1] = colorTable[ci][1]/255.0;
            savedColor[2] = colorTable[ci][2]/255.0;
        }
        break;
    }

    case CMD_START_ROTATE:
        isRotating = true;
        break;

    case CMD_STOP_ROTATE:
        isRotating = false;
        break;

    case CMD_RESET_VIEW:
        /* 桌面模式:也恢复相机,使窗口回到有用的观察角度
         * Desktop mode: also restore camera so the window snaps back to a useful angle */
        resetModelView(renderer->GetActiveCamera(), renderer,
                       /* 恢复相机
                        * restoreCamera */ true);
        break;

    case CMD_SET_VIEW:
        applyViewPreset(static_cast<int>(vcmd.value + 0.5));
        break;

    case CMD_SET_COLOUR_R:
    case CMD_SET_COLOUR_G:
    case CMD_SET_COLOUR_B:
        break;

    default:
        break;
    }
}

/* ================================================================
 * VR管线重建
 * VR pipeline rebuild
 *
 * 根据当前滤镜状态重建指定Actor的VTK管线。
 * Rebuilds the VTK pipeline for a specified actor based on current filter states.
 * 路由:STLReader -> [Clip] -> [Shrink] -> [Smooth] -> [Decimate] -> [Elevation] -> Mapper
 * Route: STLReader -> [Clip] -> [Shrink] -> [Smooth] -> [Decimate] -> [Elevation] -> Mapper
 * ================================================================ */

void VRRenderThread::rebuildPipeline(int idx)
{
    if (idx < 0 || idx >= actorList.size()) return;

    vtkActor*     actor  = actorList[idx];
    vtkSTLReader* reader = readerList[idx].Get();
    if (!actor || !reader) return;

    vtkDataSetMapper* mapper = vtkDataSetMapper::SafeDownCast(actor->GetMapper());
    if (!mapper) return;

    /* 从STL读取器开始逐级接入活跃的滤镜
     * Start from the STL reader and chain through active filters */
    vtkAlgorithmOutput* current = reader->GetOutputPort();

    const bool clipOrSlice = clipState[idx] || sliceState[idx];

    if (clipOrSlice) {
        clipFilters[idx]->SetInputConnection(current);
        current = clipFilters[idx]->GetOutputPort();
    }

    if (shrinkState[idx]) {
        if (clipOrSlice) {
            geometryFilters[idx]->SetInputConnection(current);
            current = geometryFilters[idx]->GetOutputPort();
        }
        shrinkFilters[idx]->SetInputConnection(current);
        current = shrinkFilters[idx]->GetOutputPort();
    }

    if (smoothState[idx]) {
        smoothFilters[idx]->SetInputConnection(current);
        current = smoothFilters[idx]->GetOutputPort();
    }

    if (decimateState[idx]) {
        /* 抽取前需要GeometryFilter转换类型+CleanPolyData合并重复点
         * Before decimating: GeometryFilter converts type + CleanPolyData merges duplicate points */
        if (clipOrSlice && !shrinkState[idx]) {
            geometryFilters[idx]->SetInputConnection(current);
            cleanFilters[idx]->SetInputConnection(geometryFilters[idx]->GetOutputPort());
        } else {
            cleanFilters[idx]->SetInputConnection(current);
        }
        decimateFilters[idx]->SetInputConnection(cleanFilters[idx]->GetOutputPort());
        current = decimateFilters[idx]->GetOutputPort();
    }

    if (elevationState[idx]) {
        elevationFilters[idx]->SetInputConnection(current);
        current = elevationFilters[idx]->GetOutputPort();
        /* 启用彩虹色表
         * Enable rainbow colour table */
        mapper->SetLookupTable(elevationLUTs[idx]);
        mapper->SetScalarRange(0.0, 1.0);
        mapper->ScalarVisibilityOn();
    } else {
        /* 关闭标量着色,恢复Actor颜色
         * Disable scalar colouring, restore actor colour */
        mapper->ScalarVisibilityOff();
    }

    mapper->SetInputConnection(current);
    mapper->Update();  /* 强制立即更新管线
                        * Force immediate pipeline update */
}

/* ================================================================
 * 程序生成星空Cubemap
 * Procedurally generated starfield cubemap
 *
 * vtkTexture::CubeMapOn()需要6个面的图像数据。
 * vtkTexture::CubeMapOn() requires image data for 6 faces.
 * 每面使用不同随机种子生成,内容略有差异产生真实感。
 * Each face uses a different seed for subtle variation, creating realism.
 * 不依赖外部贴图文件。
 * No external texture files required.
 * ================================================================ */

static vtkSmartPointer<vtkTexture> generateCubemapTexture()
{
    const int S = 512, NC = 3;  /* 每面512×512 RGB
                                 * 512×512 RGB per face */

    auto clamp = [](int v) -> unsigned char {
        return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
    };

    /* 生成单面星空图(与mainwindow.cpp中的算法相同)
     * Generate a single starfield face (same algorithm as in mainwindow.cpp) */
    auto makeFace = [&](unsigned int seed) -> vtkSmartPointer<vtkImageData> {
        std::srand(seed);
        vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();
        img->SetDimensions(S, S, 1);
        img->AllocateScalars(VTK_UNSIGNED_CHAR, NC);
        unsigned char* buf = static_cast<unsigned char*>(img->GetScalarPointer());

        /* 边界安全的像素叠加(加法混合+clamp)
         * Bounds-safe pixel additive blend with clamp */
        auto addPx = [&](int x, int y, int dr, int dg, int db) {
            if (x < 0 || x >= S || y < 0 || y >= S) return;
            unsigned char* p = buf + (y * S + x) * NC;
            p[0] = clamp((int)p[0] + dr);
            p[1] = clamp((int)p[1] + dg);
            p[2] = clamp((int)p[2] + db);
        };

        /* 深空底色
         * Deep-space base colour */
        for (int i = 0; i < S * S; ++i) {
            int n = std::rand() % 14;
            buf[i*NC+0] = clamp(3  + n/3);
            buf[i*NC+1] = clamp(4  + n/4);
            buf[i*NC+2] = clamp(16 + n);
        }

        /* 星云(每面2个)
         * Nebula blobs (2 per face) */
        for (int k = 0; k < 2; ++k) {
            int cx = std::rand()%S, cy = std::rand()%S;
            int r  = 40 + std::rand()%70;
            int nr = 8+std::rand()%22, ng = 8+std::rand()%28, nb = 30+std::rand()%60;
            for (int dy=-r; dy<=r; ++dy)
            for (int dx=-r; dx<=r; ++dx) {
                float d = std::sqrt((float)(dx*dx+dy*dy));
                if (d > r) continue;
                float a = std::exp(-2.5f*(d/r)*(d/r));
                addPx((cx+dx+S)%S,(cy+dy+S)%S,(int)(nr*a),(int)(ng*a),(int)(nb*a));
            }
        }

        /* 点星(每面120颗)
         * Point stars (120 per face) */
        for (int s = 0; s < 120; ++s) {
            int sx=std::rand()%S, sy=std::rand()%S, t=std::rand()%3;
            int sr, sg, sb;
            if      (t==0){sr=sg=sb=215+std::rand()%40;}       /* 白
                                                                * White */
            else if (t==1){sr=175+std::rand()%55;sg=185+std::rand()%55;sb=255;} /* 蓝白
                                                                                 * Blue-white */
            else          {sr=255;sg=230+std::rand()%25;sb=175+std::rand()%55;} /* 黄
                                                                                 * Yellow */
            int hr=1+std::rand()%3;
            for (int dy=-hr; dy<=hr; ++dy)
            for (int dx=-hr; dx<=hr; ++dx) {
                float d=std::sqrt((float)(dx*dx+dy*dy));
                if (d>hr) continue;
                float a=(d<=0.5f)?1.0f:std::exp(-3.0f*(d/hr)*(d/hr));
                addPx((sx+dx+S)%S,(sy+dy+S)%S,(int)(sr*a),(int)(sg*a),(int)(sb*a));
            }
        }
        return img;
    };

    /* 创建Cubemap贴图:6个面使用不同种子
     * Create cubemap texture: 6 faces with different seeds */
    vtkSmartPointer<vtkTexture> tex = vtkSmartPointer<vtkTexture>::New();
    tex->CubeMapOn();
    tex->InterpolateOn();
    tex->MipmapOn();
    tex->RepeatOff();
    for (int face = 0; face < 6; ++face) {
        tex->SetInputDataObject(face, makeFace(20250428u + (unsigned int)face * 137u));
    }
    return tex;
}

/* 将vtkSkybox加入渲染器的辅助函数
 * Helper: attach a vtkSkybox to the renderer */
static void attachSkybox(vtkRenderer* renderer, vtkSmartPointer<vtkTexture> cubemap)
{
    vtkSmartPointer<vtkSkybox> skybox = vtkSmartPointer<vtkSkybox>::New();
    skybox->SetTexture(cubemap);
    renderer->AddActor(skybox);
    renderer->GradientBackgroundOff();  /* 关闭渐变背景,让Skybox可见
                                         * Disable gradient so Skybox is visible */
}

void VRRenderThread::setupSkybox(vtkOpenVRRenderer* renderer,
                                  vtkOpenVRRenderWindow* /* 渲染窗口
                                                            * renderWindow */)
{
    attachSkybox(renderer, generateCubemapTexture());
}

void VRRenderThread::setupSkyboxDesktop(vtkRenderer* renderer,
                                         vtkRenderWindow* /* 渲染窗口
                                                            * renderWindow */)
{
    attachSkybox(renderer, generateCubemapTexture());
}

/* ================================================================
 * 光照初始化
 * Lighting initialisation
 *
 * 使用主光(Key Light)+补光(Fill Light)两光源方案:
 * Uses a Key + Fill two-light rig:
 *   - 主光:正面暖白光,受CMD_SET_LIGHT_INTENSITY实时控制
 *     Key light: front warm white, real-time controlled by CMD_SET_LIGHT_INTENSITY
 *   - 补光:侧后方冷蓝光,强度固定为主光的50%,提供轮廓感
 *     Fill light: side-rear cool blue, fixed at 50% of key, provides silhouette depth
 * ================================================================ */

void VRRenderThread::setupLighting(vtkOpenVRRenderer* renderer)
{
    /* 主光源:保存到成员变量供强度调整命令使用
     * Key light: saved to member variable for intensity adjustment commands */
    mainLight = vtkSmartPointer<vtkLight>::New();
    mainLight->SetLightTypeToSceneLight();
    mainLight->SetPosition(5.0, 10.0, 15.0);
    mainLight->SetPositional(false);  /* 方向光(无衰减)
                                       * Directional light (no attenuation) */
    mainLight->SetFocalPoint(0.0, 0.0, 0.0);
    mainLight->SetDiffuseColor(1.0, 1.0, 1.0);   /* 白色漫反射
                                                  * White diffuse */
    mainLight->SetAmbientColor(0.3, 0.3, 0.3);   /* 环境光提升基础亮度
                                                  * Ambient raises base brightness */
    mainLight->SetSpecularColor(1.0, 1.0, 1.0);  /* 白色高光
                                                  * White specular */
    mainLight->SetIntensity(mainLightIntensity);
    renderer->AddLight(mainLight);

    /* 补光:冷蓝色,来自侧后方
     * Fill light: cool blue from the side-rear */
    vtkSmartPointer<vtkLight> fillLight = vtkSmartPointer<vtkLight>::New();
    fillLight->SetLightTypeToSceneLight();
    fillLight->SetPosition(-8.0, 5.0, -5.0);
    fillLight->SetPositional(false);
    fillLight->SetFocalPoint(0.0, 0.0, 0.0);
    fillLight->SetDiffuseColor(0.8, 0.9, 1.0);  /* 冷蓝色
                                                 * Cool blue */
    fillLight->SetAmbientColor(0.0, 0.0, 0.0);
    fillLight->SetSpecularColor(0.0, 0.0, 0.0);
    fillLight->SetIntensity(0.4);  /* 主光的50%
                                    * 50% of key light */
    renderer->AddLight(fillLight);
}

void VRRenderThread::setupLightingDesktop(vtkRenderer* renderer)
{
    /* 桌面模式与VR模式完全相同的光照设置
     * Identical lighting setup to VR mode */
    mainLight = vtkSmartPointer<vtkLight>::New();
    mainLight->SetLightTypeToSceneLight();
    mainLight->SetPosition(5.0, 10.0, 15.0);
    mainLight->SetPositional(false);
    mainLight->SetFocalPoint(0.0, 0.0, 0.0);
    mainLight->SetDiffuseColor(1.0, 1.0, 1.0);
    mainLight->SetAmbientColor(0.3, 0.3, 0.3);
    mainLight->SetSpecularColor(1.0, 1.0, 1.0);
    mainLight->SetIntensity(mainLightIntensity);
    renderer->AddLight(mainLight);

    vtkSmartPointer<vtkLight> fillLight = vtkSmartPointer<vtkLight>::New();
    fillLight->SetLightTypeToSceneLight();
    fillLight->SetPosition(-8.0, 5.0, -5.0);
    fillLight->SetPositional(false);
    fillLight->SetFocalPoint(0.0, 0.0, 0.0);
    fillLight->SetDiffuseColor(0.8, 0.9, 1.0);
    fillLight->SetAmbientColor(0.0, 0.0, 0.0);
    fillLight->SetSpecularColor(0.0, 0.0, 0.0);
    fillLight->SetIntensity(0.4);
    renderer->AddLight(fillLight);
}

/* ================================================================
 * 地板创建
 * Floor creation
 *
 * 地板固定在Y=0(代表VR世界的地面)。
 * Floor is fixed at Y=0 (representing the VR world's ground plane).
 *
 * 算法:
 * Algorithm:
 *   1. 计算所有模型Actor的整体包围盒
 *      Compute the overall bounding box of all model actors
 *   2. 将所有Actor整体上移,使模型底部悬浮在合适高度(桌面/展示台高度)
 *      Shift all actors upward so model bottom floats at display height
 *   3. 在Y=0创建覆盖模型水平范围的地板平面
 *      Create a floor plane at Y=0 covering the model's horizontal extent
 * ================================================================ */

static void buildFloorActor(vtkRenderer* renderer)
{
    /* 步骤1:计算所有模型Actor的整体包围盒
     * Step 1: compute overall bounding box of all model actors */
    double sceneBounds[6] = {0,0,0,0,0,0};
    bool   hasBounds      = false;

    vtkActorCollection* actors = renderer->GetActors();
    actors->InitTraversal();
    while (vtkActor* a = actors->GetNextActor()) {
        if (a->GetMapper()) a->GetMapper()->Update();
        double b[6];
        a->GetBounds(b);
        if (b[0] > b[1]) continue;  /* 跳过无效包围盒(如Skybox)
                                     * Skip invalid bounds (e.g. Skybox) */
        if (!hasBounds) {
            for (int i = 0; i < 6; ++i) sceneBounds[i] = b[i];
            hasBounds = true;
        } else {
            if (b[0] < sceneBounds[0]) sceneBounds[0] = b[0];
            if (b[1] > sceneBounds[1]) sceneBounds[1] = b[1];
            if (b[2] < sceneBounds[2]) sceneBounds[2] = b[2];
            if (b[3] > sceneBounds[3]) sceneBounds[3] = b[3];
            if (b[4] < sceneBounds[4]) sceneBounds[4] = b[4];
            if (b[5] > sceneBounds[5]) sceneBounds[5] = b[5];
        }
    }

    /* 步骤2:将所有Actor整体上移
     * Step 2: shift all actors upward
     *
     * DISPLAY_HEIGHT = 模型高度的0.8倍,使零件底部悬浮在合适高度。
     * DISPLAY_HEIGHT = 0.8x model height; keeps the part bottom at a good display height.
     * 此比例自适应不同单位(毫米/米)的模型。
     * This ratio is adaptive across models in different units (mm/m). */
    const double TARGET_FLOOR_Y = 0.0;

    if (hasBounds) {
        double modelBottomY   = sceneBounds[2];
        double modelHeight    = sceneBounds[3] - sceneBounds[2];
        double DISPLAY_HEIGHT = std::max(modelHeight * 0.8, 0.1);
        double shiftY         = (TARGET_FLOOR_Y + DISPLAY_HEIGHT) - modelBottomY;

        /* 对所有Actor应用相同的Y方向位移
         * Apply the same Y-shift to all actors */
        actors->InitTraversal();
        while (vtkActor* a = actors->GetNextActor()) {
            double pos[3];
            a->GetPosition(pos);
            a->SetPosition(pos[0], pos[1] + shiftY, pos[2]);
        }

        /* 平移后重新计算包围盒,用于确定地板范围
         * Recompute bounds after shift to determine floor dimensions */
        for (int i = 0; i < 6; ++i) sceneBounds[i] = 0;
        hasBounds = false;
        actors->InitTraversal();
        while (vtkActor* a = actors->GetNextActor()) {
            double b[6]; a->GetBounds(b);
            if (b[0] > b[1]) continue;
            if (!hasBounds) {
                for (int i=0;i<6;++i) sceneBounds[i]=b[i];
                hasBounds = true;
            } else {
                if (b[0]<sceneBounds[0]) sceneBounds[0]=b[0];
                if (b[1]>sceneBounds[1]) sceneBounds[1]=b[1];
                if (b[4]<sceneBounds[4]) sceneBounds[4]=b[4];
                if (b[5]>sceneBounds[5]) sceneBounds[5]=b[5];
            }
        }
    }

    /* 步骤3:创建地板平面(固定在Y=0,尺寸覆盖模型水平范围的2倍)
     * Step 3: create floor plane (fixed at Y=0, size covers 2x model horizontal extent) */
    double spanX = hasBounds ? (sceneBounds[1] - sceneBounds[0]) : 20.0;
    double spanZ = hasBounds ? (sceneBounds[5] - sceneBounds[4]) : 20.0;
    double halfX = std::max(spanX * 2.0, 15.0);  /* 至少15单位宽
                                                  * At least 15 units wide */
    double halfZ = std::max(spanZ * 2.0, 15.0);
    double cx    = hasBounds ? (sceneBounds[0] + sceneBounds[1]) / 2.0 : 0.0;
    double cz    = hasBounds ? (sceneBounds[4] + sceneBounds[5]) / 2.0 : 0.0;

    vtkNew<vtkPlaneSource> floorPlane;
    floorPlane->SetOrigin(cx - halfX, TARGET_FLOOR_Y, cz - halfZ);
    floorPlane->SetPoint1(cx + halfX, TARGET_FLOOR_Y, cz - halfZ);
    floorPlane->SetPoint2(cx - halfX, TARGET_FLOOR_Y, cz + halfZ);
    floorPlane->SetResolution(20, 20);  /* 细分以获得更好的光照效果
                                         * Subdivide for better lighting */
    floorPlane->Update();

    vtkNew<vtkPolyDataMapper> floorMapper;
    floorMapper->SetInputConnection(floorPlane->GetOutputPort());

    vtkNew<vtkActor> floorActor;
    floorActor->SetMapper(floorMapper);
    floorActor->GetProperty()->SetColor(0.3, 0.3, 0.3);  /* 深灰色地板
                                                          * Dark grey floor */
    floorActor->GetProperty()->SetAmbient(0.5);           /* 较高环境光使地板不过暗
                                                           * Higher ambient keeps floor visible */
    floorActor->GetProperty()->SetDiffuse(0.5);
    floorActor->GetProperty()->SetSpecular(0.1);          /* 轻微高光
                                                           * Slight specular */

    renderer->AddActor(floorActor);
}

void VRRenderThread::setupFloor(vtkOpenVRRenderer* renderer)
{
    buildFloorActor(renderer);
}

void VRRenderThread::setupFloorDesktop(vtkRenderer* renderer)
{
    buildFloorActor(renderer);
}
