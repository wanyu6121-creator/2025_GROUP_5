/**  @file VRRenderThread.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程实现
 *
 *   支持两种渲染模式（自动检测头显）：
 *     - VR模式：连接HTC Vive头显时使用 vtkOpenVRRenderWindow
 *     - 桌面模式：无头显时自动fallback，使用普通 vtkRenderWindow 预览
 *
 *   创意功能：
 *     - Skybox 天空盒（CMD_SET_LIGHT_INTENSITY / setupSkybox）
 *     - 光照强度实时控制（CMD_SET_LIGHT_INTENSITY，value=0.0~2.0）
 *
 *   加分功能：
 *     - 动态添加Actor（queueAddActor + processPendingActors）
 *     - 动态移除Actor（CMD_REMOVE_ACTOR）
 */

#include "VRRenderThread.h"

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
 * 【B方案】颜色循环表（CMD_VR_SET_COLOUR 依次切换）
 * ================================================================ */
const int VRRenderThread::colorTable[VRRenderThread::COLOR_COUNT][3] = {
    {255, 255, 255},   /* 白 */
    {220,  50,  50},   /* 红 */
    {50,  180,  50},   /* 绿 */
    {50,  120, 220},   /* 蓝 */
    {220, 180,  50},   /* 黄 */
    {180,  50, 220},   /* 紫 */
};

/* ================================================================
 * 构造 / 析构
 * ================================================================ */

VRRenderThread::VRRenderThread(QObject* parent)
    : QThread(parent)
    , isRotating(false)
    , mainLightIntensity(0.8)
    , selectedActorIndex(-1)
    , rotationAngle(0.0)
    , initCamSaved(false)
{
    savedColor[0] = savedColor[1] = savedColor[2] = 1.0;
}

VRRenderThread::~VRRenderThread()
{
    if (isRunning()) {
        requestInterruption();
        wait(5000);
    }
    for (vtkActor* a : actorList) {
        if (a) a->Delete();
    }
}

/* ================================================================
 * 【B方案】辅助：高亮 / 取消高亮
 * ================================================================ */
void VRRenderThread::highlightActor(int idx, bool on)
{
    if (idx < 0 || idx >= actorList.size() || !actorList[idx]) return;
    vtkProperty* prop = actorList[idx]->GetProperty();
    if (on) {
        /* 保存原色 */
        prop->GetDiffuseColor(savedColor);
        /* 亮黄色高亮 + 加粗边缘线 */
        prop->SetDiffuseColor(1.0, 0.95, 0.0);
        prop->SetEdgeVisibility(1);
        prop->SetEdgeColor(1.0, 1.0, 0.0);
        prop->SetLineWidth(3.0);
    } else {
        /* 恢复原色 */
        prop->SetDiffuseColor(savedColor);
        prop->SetEdgeVisibility(0);
        prop->SetLineWidth(1.0);
    }
}

/* ================================================================
 * 【B方案】辅助：鼠标坐标射线拾取 → actor 索引
 * ================================================================ */
int VRRenderThread::pickActorAt(int x, int y, vtkRenderer* renderer)
{
    vtkNew<vtkPropPicker> picker;
    picker->Pick(x, y, 0, renderer);
    vtkActor* hit = picker->GetActor();
    if (!hit) return -1;
    for (int i = 0; i < actorList.size(); ++i) {
        if (actorList[i] == hit) return i;
    }
    return -1;
}

/* ================================================================
 * 【B方案】注册零件名称
 * ================================================================ */
void VRRenderThread::setActorName(int index, const QString& name)
{
    while (actorNames.size() <= index) actorNames.append("");
    actorNames[index] = name;
}

/* ================================================================
 * 公共接口
 * ================================================================ */

int VRRenderThread::addActorOffline(vtkActor* actor,
                                    vtkSTLReader* reader,
                                    bool clipOn,
                                    bool shrinkOn)
{
    if (!actor) return -1;

    int idx = actorList.size();
    actorList.append(actor);
    readerList.append(vtkSmartPointer<vtkSTLReader>(reader));

    /* 为该Actor预创建裁剪和收缩滤镜对象，供 rebuildPipeline 使用 */
    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);

    vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
    cf->SetClipFunction(clipPlane.Get());

    vtkSmartPointer<vtkShrinkFilter> sf = vtkSmartPointer<vtkShrinkFilter>::New();
    sf->SetShrinkFactor(0.8);

    clipFilters.append(cf);
    shrinkFilters.append(sf);

    /* ---- Smooth filter ---- */
    vtkSmartPointer<vtkSmoothPolyDataFilter> smf = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    smf->SetNumberOfIterations(20);
    smf->SetRelaxationFactor(0.1);
    smf->FeatureEdgeSmoothingOff();
    smf->BoundarySmoothingOn();
    smoothFilters.append(smf);

    /* ---- Decimate filter (needs clean + geometry conversion) ---- */
    vtkSmartPointer<vtkGeometryFilter>  gf  = vtkSmartPointer<vtkGeometryFilter>::New();
    vtkSmartPointer<vtkCleanPolyData>   clf = vtkSmartPointer<vtkCleanPolyData>::New();
    vtkSmartPointer<vtkDecimatePro>     df  = vtkSmartPointer<vtkDecimatePro>::New();
    df->SetTargetReduction(0.9);
    df->PreserveTopologyOn();
    geometryFilters.append(gf);
    cleanFilters.append(clf);
    decimateFilters.append(df);

    /* ---- Elevation filter + LUT ---- */
    vtkSmartPointer<vtkElevationFilter> ef = vtkSmartPointer<vtkElevationFilter>::New();
    if (reader) {
        reader->Update();
        double bounds[6];
        reader->GetOutput()->GetBounds(bounds);
        double zMin = bounds[4], zMax = bounds[5];
        if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }
        ef->SetLowPoint(0.0, 0.0, zMin);
        ef->SetHighPoint(0.0, 0.0, zMax);
    }
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetHueRange(0.667, 0.0);   /* blue → red */
    lut->Build();
    elevationFilters.append(ef);
    elevationLUTs.append(lut);

    mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
    clipState.append(clipOn);
    shrinkState.append(shrinkOn);
    smoothState.append(false);
    decimateState.append(false);
    elevationState.append(false);
    actorNames.append("");
    actorColorIdx.append(0);

    return idx;
}

void VRRenderThread::clearActors()
{
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
    actorNames.clear();
    actorColorIdx.clear();
    selectedActorIndex = -1;
}

void VRRenderThread::issueCommand(int cmd, double value, int actorIndex)
{
    QMutexLocker locker(&mutex);
    commandQueue.enqueue(VRCmd(cmd, value, actorIndex));
}

void VRRenderThread::queueAddActor(const ActorPackage& pkg)
{
    /* 线程安全地将新Actor推入待添加队列
     * 渲染循环在下一帧调用 processPendingActors 时消费 */
    QMutexLocker locker(&pendingMutex);
    pendingActors.enqueue(pkg);
}

/* ================================================================
 * run() — 自动检测头显，选择VR或桌面模式
 * ================================================================ */

void VRRenderThread::run()
{
    /* 尝试初始化OpenVR运行时，检测头显是否连接 */
    bool vrAvailable = false;
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);

    if (initError == vr::VRInitError_None && vrSystem != nullptr) {
        vrAvailable = true;
        /* 仅用于检测，关闭后由 vtkOpenVRRenderWindow 内部重新初始化 */
        vr::VR_Shutdown();
    }

    if (vrAvailable) {
        runVRMode();
    } else {
        runDesktopMode();
    }
}

/* ================================================================
 * VR 模式渲染循环
 * ================================================================ */

void VRRenderThread::runVRMode()
{
    vtkNew<vtkOpenVRRenderer>               renderer;
    vtkNew<vtkOpenVRRenderWindow>           renderWindow;
    vtkNew<vtkOpenVRRenderWindowInteractor> interactor;
    vtkNew<vtkOpenVRCamera>                 camera;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(2160, 1200);
    interactor->SetRenderWindow(renderWindow);
    renderer->SetActiveCamera(camera);

    /* 有Skybox时背景颜色不可见，但仍设置为深蓝作为备用 */
    renderer->SetBackground(0.05, 0.05, 0.15);

    /* 添加启动时已注册的所有模型Actor */
    for (vtkActor* actor : actorList) {
        if (actor) renderer->AddActor(actor);
    }

    /* 场景初始化（顺序：地板 → 光照 → Skybox）*/
    setupFloor(renderer.Get());
    setupLighting(renderer.Get());
    setupSkybox(renderer.Get(), renderWindow.Get());

    /* 初始化渲染窗口和交互器（手柄控制器在此启用）*/
    renderWindow->Initialize();
    interactor->Initialize();

    /* ---- VR相机初始化：让模型近在眼前 ----
     * ResetCamera() 会把模型推到很远处以适应视口，VR里看起来很小。
     * 改为手动计算：取模型包围盒，把相机放在模型正前方 1~1.5 倍模型尺寸处，
     * 人进入VR时模型就在面前触手可及的位置。 */
    {
        double bounds[6] = {0,0,0,0,0,0};
        bool hasBounds = false;
        vtkActorCollection* acs = renderer->GetActors();
        acs->InitTraversal();
        while (vtkActor* a = acs->GetNextActor()) {
            double b[6]; a->GetBounds(b);
            if (b[0] > b[1]) continue;
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
            /* 模型中心 */
            double cx = (bounds[0] + bounds[1]) / 2.0;
            double cy = (bounds[2] + bounds[3]) / 2.0;
            double cz = (bounds[4] + bounds[5]) / 2.0;

            /* 模型最大尺寸（X/Y/Z中最大的一个）*/
            double sizeX = bounds[1] - bounds[0];
            double sizeY = bounds[3] - bounds[2];
            double sizeZ = bounds[5] - bounds[4];
            double maxSize = std::max({sizeX, sizeY, sizeZ});

            /* 相机放在模型正前方（+Z方向）1.2倍模型尺寸处
             * 视线水平朝向模型中心，高度与模型中心齐平 */
            double camDist = maxSize * 1.2;

            renderer->GetActiveCamera()->SetPosition(cx, cy, cz + camDist);
            renderer->GetActiveCamera()->SetFocalPoint(cx, cy, cz);
            renderer->GetActiveCamera()->SetViewUp(0.0, 1.0, 0.0);
            renderer->ResetCameraClippingRange();

            /* 保存初始相机参数，供 Reset View 恢复 */
            renderer->GetActiveCamera()->GetPosition(initCamPos);
            renderer->GetActiveCamera()->GetFocalPoint(initCamFocal);
            renderer->GetActiveCamera()->GetViewUp(initCamUp);
            initCamSaved = true;
        } else {
            /* 没有模型时回退到默认视角 */
            renderer->ResetCamera();
        }
    }

    /* ---- 主渲染循环 ---- */
    while (!isInterruptionRequested()) {

        /* 1. 批量取出命令队列（swap避免持锁时间过长）*/
        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        /* 2. 处理命令 */
        while (!localQueue.isEmpty()) {
            processCommandVR(localQueue.dequeue(), renderer.Get());
        }

        /* 3. 处理动态添加的Actor */
        processPendingActorsVR(renderer.Get());

        /* 4. 旋转动画：模型绕 Y 轴旋转（跳过地板 Actor）*/
        if (isRotating) {
            rotationAngle += 0.5;
            if (rotationAngle >= 360.0) rotationAngle -= 360.0;
            for (int i = 0; i < actorList.size(); ++i) {
                if (actorList[i]) {
                    actorList[i]->SetOrientation(0, rotationAngle, 0);
                }
            }
        }

        /* 5. 渲染一帧 + 处理手柄交互事件 */
        renderWindow->Render();
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* 清理：终止交互器并释放渲染窗口资源 */
    interactor->TerminateApp();
    renderWindow->Finalize();
}

/* ================================================================
 * 桌面 fallback 模式渲染循环
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

    setupFloorDesktop(renderer.Get());
    setupLightingDesktop(renderer.Get());
    setupSkyboxDesktop(renderer.Get(), renderWindow.Get());

    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* ---- 【B方案】Observer：在循环外注册一次，整个生命周期有效 ---- */
    struct PickState {
        bool clickPending = false;
        int  clickX = 0, clickY = 0;
        char keyPending = 0;
    };
    auto pickState = std::make_shared<PickState>();

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

    vtkNew<vtkCallbackCommand> keyCb;
    keyCb->SetCallback([](vtkObject* caller, unsigned long, void* cd, void*) {
        auto* inter = static_cast<vtkRenderWindowInteractor*>(caller);
        auto* ps    = static_cast<PickState*>(cd);
        ps->keyPending = inter->GetKeyCode();
    });
    keyCb->SetClientData(pickState.get());
    interactor->AddObserver(vtkCommand::KeyPressEvent, keyCb);

    renderWindow->Render();

    /* ---- 桌面渲染循环 ---- */
    while (!isInterruptionRequested()) {

        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        while (!localQueue.isEmpty()) {
            processCommandDesktop(localQueue.dequeue(), renderer.Get());
        }

        processPendingActorsDesktop(renderer.Get());

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

        /* ---- 【B方案】处理鼠标点击拾取 ---- */
        if (pickState->clickPending) {
            pickState->clickPending = false;
            int hit = pickActorAt(pickState->clickX, pickState->clickY, renderer.Get());

            if (hit >= 0 && hit == selectedActorIndex) {
                highlightActor(selectedActorIndex, false);
                selectedActorIndex = -1;
                emit vrActorSelected(-1, "");
            } else {
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

        /* ---- 【B方案】键盘快捷键（选中状态下）----
         *   V=可见性  S=Slice  K=Shrink  C=循环颜色 */
        if (pickState->keyPending != 0 && selectedActorIndex >= 0) {
            char key = pickState->keyPending;
            int  si  = selectedActorIndex;
            switch (key) {
            case 'v': case 'V':
                if (actorList[si])
                    actorList[si]->SetVisibility(!actorList[si]->GetVisibility());
                break;
            case 's': case 'S':
                clipState[si] = !clipState[si];
                rebuildPipeline(si);
                break;
            case 'k': case 'K':
                shrinkState[si] = !shrinkState[si];
                rebuildPipeline(si);
                break;
            case 'c': case 'C': {
                while (actorColorIdx.size() <= si) actorColorIdx.append(0);
                actorColorIdx[si] = (actorColorIdx[si] + 1) % COLOR_COUNT;
                int ci = actorColorIdx[si];
                actorList[si]->GetProperty()->SetDiffuseColor(
                    colorTable[ci][0] / 255.0,
                    colorTable[ci][1] / 255.0,
                    colorTable[ci][2] / 255.0);
                savedColor[0] = colorTable[ci][0] / 255.0;
                savedColor[1] = colorTable[ci][1] / 255.0;
                savedColor[2] = colorTable[ci][2] / 255.0;
                break;
            }
            default: break;
            }
        }
        pickState->keyPending = 0;

        if (renderWindow->GetSize()[0] == 0 && renderWindow->GetSize()[1] == 0) {
            break;
        }

        QThread::msleep(16);
    }
}

/* ================================================================
 * 动态 Actor 队列处理
 * ================================================================ */

void VRRenderThread::processPendingActorsVR(vtkOpenVRRenderer* renderer)
{
    /* 取出所有待添加Actor（批量处理，减少锁竞争）*/
    pendingMutex.lock();
    QQueue<ActorPackage> localPending;
    localPending.swap(pendingActors);
    pendingMutex.unlock();

    while (!localPending.isEmpty()) {
        ActorPackage pkg = localPending.dequeue();
        if (!pkg.actor) continue;

        /* 注册到内部列表（与 addActorOffline 相同逻辑）*/
        vtkSmartPointer<vtkPlane> cp = vtkSmartPointer<vtkPlane>::New();
        cp->SetOrigin(0.0, 0.0, 0.0);
        cp->SetNormal(-1.0, 0.0, 0.0);

        vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
        cf->SetClipFunction(cp.Get());

        vtkSmartPointer<vtkShrinkFilter> sf = vtkSmartPointer<vtkShrinkFilter>::New();
        sf->SetShrinkFactor(0.8);

        actorList.append(pkg.actor);
        readerList.append(pkg.reader);
        mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
        clipFilters.append(cf);
        shrinkFilters.append(sf);
        clipState.append(pkg.clipOn);
        shrinkState.append(pkg.shrinkOn);

        /* 将新Actor加入渲染器（立即在下一帧生效）*/
        renderer->AddActor(pkg.actor);
    }
}

void VRRenderThread::processPendingActorsDesktop(vtkRenderer* renderer)
{
    /* 与VR版逻辑完全相同，仅renderer类型不同 */
    pendingMutex.lock();
    QQueue<ActorPackage> localPending;
    localPending.swap(pendingActors);
    pendingMutex.unlock();

    while (!localPending.isEmpty()) {
        ActorPackage pkg = localPending.dequeue();
        if (!pkg.actor) continue;

        vtkSmartPointer<vtkPlane> cp = vtkSmartPointer<vtkPlane>::New();
        cp->SetOrigin(0.0, 0.0, 0.0);
        cp->SetNormal(-1.0, 0.0, 0.0);

        vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
        cf->SetClipFunction(cp.Get());

        vtkSmartPointer<vtkShrinkFilter> sf = vtkSmartPointer<vtkShrinkFilter>::New();
        sf->SetShrinkFactor(0.8);

        actorList.append(pkg.actor);
        readerList.append(pkg.reader);
        mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New());
        clipFilters.append(cf);
        shrinkFilters.append(sf);
        clipState.append(pkg.clipOn);
        shrinkState.append(pkg.shrinkOn);

        renderer->AddActor(pkg.actor);
    }
}

/* ================================================================
 * 命令处理 — 公共辅助函数
 * ================================================================ */

/** @brief 对单个Actor（或所有Actor）设置可见性 */
static void applyVisibility(QList<vtkActor*>& actors, int idx, bool visible)
{
    if (idx >= 0 && idx < actors.size()) {
        /* 精确定位：只改指定Actor */
        if (actors[idx]) actors[idx]->SetVisibility(visible ? 1 : 0);
    } else {
        /* idx == -1：批量设置全部Actor */
        for (vtkActor* a : actors) {
            if (a) a->SetVisibility(visible ? 1 : 0);
        }
    }
}

/* ================================================================
 * 命令处理 — VR 模式
 * ================================================================ */

void VRRenderThread::processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer)
{
    switch (vcmd.cmd) {

    case CMD_SET_VISIBLE:
        /* 精确可见性控制：actorIndex指定目标，-1表示全部 */
        applyVisibility(actorList, vcmd.actorIndex, vcmd.value > 0.5);
        break;

    case CMD_APPLY_FILTER: {
        /* 解码：filterType * 10 + (enabled ? 1 : 0) */
        int encoded    = static_cast<int>(vcmd.value + 0.5);
        int filterType = encoded / 10;
        bool enabled   = (encoded % 10) != 0;
        int idx        = vcmd.actorIndex;

        if (idx < 0 || idx >= actorList.size()) break;

        if (filterType == FILTER_CLIP)      clipState[idx]      = enabled;
        if (filterType == FILTER_SHRINK)    shrinkState[idx]    = enabled;
        if (filterType == FILTER_SMOOTH)    smoothState[idx]    = enabled;
        if (filterType == FILTER_DECIMATE)  decimateState[idx]  = enabled;
        if (filterType == FILTER_ELEVATION) elevationState[idx] = enabled;
        rebuildPipeline(idx);
        break;
    }

    case CMD_REMOVE_ACTOR: {
        /* 从渲染器和内部列表中移除指定Actor */
        int idx = vcmd.actorIndex;
        if (idx < 0 || idx >= actorList.size()) break;

        vtkActor* a = actorList[idx];
        if (a && renderer) {
            renderer->RemoveActor(a);
            a->Delete();  /* 释放 getNewActor() 分配的内存 */
        }

        /* 用nullptr占位，保持其他Actor索引不变 */
        actorList[idx]     = nullptr;
        readerList[idx]    = nullptr;
        clipFilters[idx]   = nullptr;
        shrinkFilters[idx] = nullptr;
        break;
    }

    case CMD_SET_LIGHT_INTENSITY:
        /* 【创意功能】实时调整主光源强度
         * value范围：0.0（熄灭）~ 2.0（最亮），默认0.8 */
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
        isRotating = false;
        rotationAngle = 0.0;
        for (int i = 0; i < actorList.size(); ++i)
            if (actorList[i]) actorList[i]->SetOrientation(0, 0, 0);
        if (renderer && initCamSaved) {
            renderer->GetActiveCamera()->SetPosition(initCamPos);
            renderer->GetActiveCamera()->SetFocalPoint(initCamFocal);
            renderer->GetActiveCamera()->SetViewUp(initCamUp);
            renderer->ResetCameraClippingRange();
        } else if (renderer) {
            renderer->ResetCamera();
        }
        break;

    /* 颜色通过共享Property自动同步，无需命令处理 */
    case CMD_SET_COLOUR_R:
    case CMD_SET_COLOUR_G:
    case CMD_SET_COLOUR_B:
        break;

    default:
        break;
    }
}

/* ================================================================
 * 命令处理 — 桌面模式（逻辑与VR模式完全相同）
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

        if (filterType == FILTER_CLIP)      clipState[idx]      = enabled;
        if (filterType == FILTER_SHRINK)    shrinkState[idx]    = enabled;
        if (filterType == FILTER_SMOOTH)    smoothState[idx]    = enabled;
        if (filterType == FILTER_DECIMATE)  decimateState[idx]  = enabled;
        if (filterType == FILTER_ELEVATION) elevationState[idx] = enabled;
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
        break;
    }

    case CMD_SET_LIGHT_INTENSITY:
        mainLightIntensity = vcmd.value;
        if (mainLight) mainLight->SetIntensity(mainLightIntensity);
        break;

    /* ---- 【B方案】VR内选中属性修改命令 ---- */
    case CMD_VR_SELECT_ACTOR: {
        int idx = vcmd.actorIndex;
        if (selectedActorIndex >= 0)
            highlightActor(selectedActorIndex, false);
        selectedActorIndex = idx;
        if (idx >= 0) highlightActor(idx, true);
        break;
    }
    case CMD_VR_DESELECT:
        if (selectedActorIndex >= 0)
            highlightActor(selectedActorIndex, false);
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
            clipState[si] = !clipState[si];
            rebuildPipeline(si);
        }
        break;
    }
    case CMD_VR_TOGGLE_SHRINK: {
        int si = selectedActorIndex;
        if (si >= 0 && si < actorList.size()) {
            shrinkState[si] = !shrinkState[si];
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
        isRotating = false;
        rotationAngle = 0.0;
        for (int i = 0; i < actorList.size(); ++i)
            if (actorList[i]) actorList[i]->SetOrientation(0, 0, 0);
        if (renderer && initCamSaved) {
            renderer->GetActiveCamera()->SetPosition(initCamPos);
            renderer->GetActiveCamera()->SetFocalPoint(initCamFocal);
            renderer->GetActiveCamera()->SetViewUp(initCamUp);
            renderer->ResetCameraClippingRange();
        } else if (renderer) {
            renderer->ResetCamera();
        }
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
 * 过滤器 pipeline 重建
 * ================================================================ */

void VRRenderThread::rebuildPipeline(int idx)
{
    if (idx < 0 || idx >= actorList.size()) return;

    vtkActor*     actor  = actorList[idx];
    vtkSTLReader* reader = readerList[idx].Get();
    if (!actor || !reader) return;

    vtkDataSetMapper* mapper = vtkDataSetMapper::SafeDownCast(actor->GetMapper());
    if (!mapper) return;

    /* Mirror ModelPart::updatePipeline():
     * STLReader → [Clip] → [Shrink] → [Smooth] → [Decimate] → [Elevation] → Mapper */
    vtkAlgorithmOutput* current = reader->GetOutputPort();

    if (clipState[idx]) {
        clipFilters[idx]->SetInputConnection(current);
        current = clipFilters[idx]->GetOutputPort();
    }

    if (shrinkState[idx]) {
        shrinkFilters[idx]->SetInputConnection(current);
        current = shrinkFilters[idx]->GetOutputPort();
    }

    if (smoothState[idx]) {
        smoothFilters[idx]->SetInputConnection(current);
        current = smoothFilters[idx]->GetOutputPort();
    }

    if (decimateState[idx]) {
        geometryFilters[idx]->SetInputConnection(current);
        cleanFilters[idx]->SetInputConnection(geometryFilters[idx]->GetOutputPort());
        decimateFilters[idx]->SetInputConnection(cleanFilters[idx]->GetOutputPort());
        current = decimateFilters[idx]->GetOutputPort();
    }

    if (elevationState[idx]) {
        elevationFilters[idx]->SetInputConnection(current);
        current = elevationFilters[idx]->GetOutputPort();
        mapper->SetLookupTable(elevationLUTs[idx]);
        mapper->SetScalarRange(0.0, 1.0);
        mapper->ScalarVisibilityOn();
    } else {
        mapper->ScalarVisibilityOff();
    }

    mapper->SetInputConnection(current);
    mapper->Update();
}

/* ================================================================
 * 背景渐变（渐变背景取代Skybox，兼容所有VTK版本）
 * ================================================================ */

/* ================================================================
 * 程序生成星空 Cubemap（6面，每面 512×512）
 *
 * vtkTexture::CubeMapOn() 要求传入包含6个分量的 vtkImageData，
 * 每个分量对应一个面（+X -X +Y -Y +Z -Z）。
 * 用固定种子程序生成，不依赖外部文件。
 * ================================================================ */
static vtkSmartPointer<vtkTexture> generateCubemapTexture()
{
    const int S = 512, NC = 3;          /* 每面 512×512，RGB */

    auto clamp = [](int v) -> unsigned char {
        return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
    };

    /* 生成单面星空图 */
    auto makeFace = [&](unsigned int seed) -> vtkSmartPointer<vtkImageData> {
        std::srand(seed);
        vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();
        img->SetDimensions(S, S, 1);
        img->AllocateScalars(VTK_UNSIGNED_CHAR, NC);
        unsigned char* buf = static_cast<unsigned char*>(img->GetScalarPointer());

        auto addPx = [&](int x, int y, int dr, int dg, int db) {
            if (x < 0 || x >= S || y < 0 || y >= S) return;
            unsigned char* p = buf + (y * S + x) * NC;
            p[0] = clamp((int)p[0] + dr);
            p[1] = clamp((int)p[1] + dg);
            p[2] = clamp((int)p[2] + db);
        };

        /* 深空底色 */
        for (int i = 0; i < S * S; ++i) {
            int n = std::rand() % 14;
            buf[i*NC+0] = clamp(3  + n/3);
            buf[i*NC+1] = clamp(4  + n/4);
            buf[i*NC+2] = clamp(16 + n);
        }
        /* 星云（每面2个）*/
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
        /* 点星（每面120颗）*/
        for (int s = 0; s < 120; ++s) {
            int sx=std::rand()%S, sy=std::rand()%S, t=std::rand()%3;
            int sr, sg, sb;
            if      (t==0){sr=sg=sb=215+std::rand()%40;}
            else if (t==1){sr=175+std::rand()%55;sg=185+std::rand()%55;sb=255;}
            else          {sr=255;sg=230+std::rand()%25;sb=175+std::rand()%55;}
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

    /* 创建 cubemap texture：6 个面使用不同种子，内容略有差异 */
    vtkSmartPointer<vtkTexture> tex = vtkSmartPointer<vtkTexture>::New();
    tex->CubeMapOn();
    tex->InterpolateOn();
    tex->MipmapOn();
    tex->RepeatOff();
    /* VTK cubemap：每个面用 SetInputDataObject(index, imageData) 传入 */
    for (int face = 0; face < 6; ++face) {
        tex->SetInputDataObject(face, makeFace(20250428u + (unsigned int)face * 137u));
    }
    return tex;
}

/* 辅助：把 vtkSkybox 加入渲染器 */
static void attachSkybox(vtkRenderer* renderer, vtkSmartPointer<vtkTexture> cubemap)
{
    vtkSmartPointer<vtkSkybox> skybox = vtkSmartPointer<vtkSkybox>::New();
    skybox->SetTexture(cubemap);
    renderer->AddActor(skybox);
    renderer->GradientBackgroundOff();
}

void VRRenderThread::setupSkybox(vtkOpenVRRenderer* renderer,
                                  vtkOpenVRRenderWindow* /*renderWindow*/)
{
    attachSkybox(renderer, generateCubemapTexture());
}

void VRRenderThread::setupSkyboxDesktop(vtkRenderer* renderer,
                                         vtkRenderWindow* /*renderWindow*/)
{
    attachSkybox(renderer, generateCubemapTexture());
}

/* ================================================================
 * 光照
 * ================================================================ */

void VRRenderThread::setupLighting(vtkOpenVRRenderer* renderer)
{
    /* 主光源（正面暖白光）— 保存到成员变量供强度调整命令使用 */
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

    /* 补光（冷蓝色，来自侧后方，提供轮廓感）*/
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

void VRRenderThread::setupLightingDesktop(vtkRenderer* renderer)
{
    /* 桌面模式与VR模式完全相同的光照设置 */
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
 * 地板
 * ================================================================ */

/* ================================================================
 * 地板
 * 地板固定在 Y=0（代表 VR 世界的地面）。
 * 所有模型 Actor 整体向上平移，使其底部位于 Y≈1.2（桌面/展示台高度），
 * 人站在地板上时零件自然处于正前方视线高度。
 * ================================================================ */

static void buildFloorActor(vtkRenderer* renderer)
{
    /* ---- 步骤1：计算所有模型Actor的整体包围盒 ---- */
    double sceneBounds[6] = {0,0,0,0,0,0};
    bool   hasBounds      = false;

    vtkActorCollection* actors = renderer->GetActors();
    actors->InitTraversal();
    while (vtkActor* a = actors->GetNextActor()) {
        if (a->GetMapper()) a->GetMapper()->Update();
        double b[6];
        a->GetBounds(b);
        if (b[0] > b[1]) continue;   /* 跳过无效包围盒 */
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

    /* ---- 步骤2：把所有模型Actor整体上移 ----
     * DISPLAY_HEIGHT = 模型高度的 0.8 倍，使零件底部悬浮在合适高度
     * 无论模型单位是毫米还是米，比例自适应，人站立时零件在正前方。 */
    const double TARGET_FLOOR_Y = 0.0;

    if (hasBounds) {
        double modelBottomY = sceneBounds[2];
        double modelHeight  = sceneBounds[3] - sceneBounds[2];
        double DISPLAY_HEIGHT = std::max(modelHeight * 0.8, 0.1);
        double shiftY = (TARGET_FLOOR_Y + DISPLAY_HEIGHT) - modelBottomY;

        actors->InitTraversal();
        while (vtkActor* a = actors->GetNextActor()) {
            double pos[3];
            a->GetPosition(pos);
            a->SetPosition(pos[0], pos[1] + shiftY, pos[2]);
        }

        /* 平移后重新取包围盒，用于确定地板范围 */
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

    /* ---- 步骤3：创建地板，固定在 Y=0，尺寸覆盖模型水平范围 ---- */
    double spanX = hasBounds ? (sceneBounds[1] - sceneBounds[0]) : 20.0;
    double spanZ = hasBounds ? (sceneBounds[5] - sceneBounds[4]) : 20.0;
    double halfX = std::max(spanX * 2.0, 15.0);
    double halfZ = std::max(spanZ * 2.0, 15.0);
    double cx    = hasBounds ? (sceneBounds[0] + sceneBounds[1]) / 2.0 : 0.0;
    double cz    = hasBounds ? (sceneBounds[4] + sceneBounds[5]) / 2.0 : 0.0;

    vtkNew<vtkPlaneSource> floorPlane;
    floorPlane->SetOrigin(cx - halfX, TARGET_FLOOR_Y, cz - halfZ);
    floorPlane->SetPoint1(cx + halfX, TARGET_FLOOR_Y, cz - halfZ);
    floorPlane->SetPoint2(cx - halfX, TARGET_FLOOR_Y, cz + halfZ);
    floorPlane->SetResolution(20, 20);
    floorPlane->Update();

    vtkNew<vtkPolyDataMapper> floorMapper;
    floorMapper->SetInputConnection(floorPlane->GetOutputPort());

    vtkNew<vtkActor> floorActor;
    floorActor->SetMapper(floorMapper);
    floorActor->GetProperty()->SetColor(0.3, 0.3, 0.3);
    floorActor->GetProperty()->SetAmbient(0.5);
    floorActor->GetProperty()->SetDiffuse(0.5);
    floorActor->GetProperty()->SetSpecular(0.1);

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
