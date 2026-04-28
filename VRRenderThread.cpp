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
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>

/* ================================================================
 * 构造 / 析构
 * ================================================================ */

VRRenderThread::VRRenderThread(QObject* parent)
    : QThread(parent)
    , isRotating(false)
    , mainLightIntensity(0.8)
{
}

VRRenderThread::~VRRenderThread()
{
    /* 确保线程安全停止 */
    if (isRunning()) {
        requestInterruption();
        wait(5000);
    }
    /* 释放由 getNewActor() 创建的裸指针Actor */
    for (vtkActor* a : actorList) {
        if (a) a->Delete();
    }
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
    mapperList.append(vtkSmartPointer<vtkDataSetMapper>::New()); /* 占位，未使用 */
    clipState.append(clipOn);
    shrinkState.append(shrinkOn);

    return idx;
}

void VRRenderThread::clearActors()
{
    /* 清空所有列表，但不Delete Actor（由析构函数负责）*/
    actorList.clear();
    readerList.clear();
    mapperList.clear();
    clipFilters.clear();
    shrinkFilters.clear();
    clipState.clear();
    shrinkState.clear();
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

    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

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

        /* 4. 旋转动画：每帧偏转0.5度 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
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

    /* 添加所有模型Actor */
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
    renderWindow->Render();

    /* ---- 桌面渲染循环 ---- */
    while (!isInterruptionRequested()) {

        /* 批量取出命令队列 */
        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        while (!localQueue.isEmpty()) {
            processCommandDesktop(localQueue.dequeue(), renderer.Get());
        }

        /* 处理动态添加的Actor */
        processPendingActorsDesktop(renderer.Get());

        /* 旋转动画 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        renderWindow->Render();
        interactor->ProcessEvents();

        /* 用户关闭预览窗口时退出 */
        if (renderWindow->GetSize()[0] == 0 && renderWindow->GetSize()[1] == 0) {
            break;
        }

        QThread::msleep(16);   /* ~60 fps */
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
        /* 解码：filterType * 10 + (enabled ? 1 : 0)
         * filterType: 0=clip, 1=shrink */
        int encoded    = static_cast<int>(vcmd.value + 0.5);
        int filterType = encoded / 10;
        bool enabled   = (encoded % 10) != 0;
        int idx        = vcmd.actorIndex;

        if (idx < 0 || idx >= actorList.size()) break;

        /* 更新状态并重建该Actor的pipeline */
        if (filterType == FILTER_CLIP)   clipState[idx]   = enabled;
        if (filterType == FILTER_SHRINK) shrinkState[idx] = enabled;
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
        if (renderer) {
            renderer->ResetCamera();
            renderer->GetActiveCamera()->Azimuth(30);
            renderer->GetActiveCamera()->Elevation(30);
            renderer->ResetCameraClippingRange();
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

        if (filterType == FILTER_CLIP)   clipState[idx]   = enabled;
        if (filterType == FILTER_SHRINK) shrinkState[idx] = enabled;
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
        if (renderer) {
            renderer->ResetCamera();
            renderer->GetActiveCamera()->Azimuth(30);
            renderer->GetActiveCamera()->Elevation(30);
            renderer->ResetCameraClippingRange();
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

    /* 取出Actor当前绑定的Mapper（由 ModelPart::getNewActor() 创建）*/
    vtkDataSetMapper* mapper =
        vtkDataSetMapper::SafeDownCast(actor->GetMapper());
    if (!mapper) return;

    vtkClipDataSet*  cf = clipFilters[idx].Get();
    vtkShrinkFilter* sf = shrinkFilters[idx].Get();

    bool doClip   = clipState[idx];
    bool doShrink = shrinkState[idx];

    /* 根据两个flag的组合选择pipeline路由 */
    if (doClip && doShrink) {
        /* reader → clip → shrink → mapper */
        cf->SetInputConnection(reader->GetOutputPort());
        sf->SetInputConnection(cf->GetOutputPort());
        mapper->SetInputConnection(sf->GetOutputPort());

    } else if (doClip) {
        /* reader → clip → mapper */
        cf->SetInputConnection(reader->GetOutputPort());
        mapper->SetInputConnection(cf->GetOutputPort());

    } else if (doShrink) {
        /* reader → shrink → mapper */
        sf->SetInputConnection(reader->GetOutputPort());
        mapper->SetInputConnection(sf->GetOutputPort());

    } else {
        /* reader → mapper（无滤镜）*/
        mapper->SetInputConnection(reader->GetOutputPort());
    }

    mapper->Update();
}

/* ================================================================
 * 背景渐变（渐变背景取代Skybox，兼容所有VTK版本）
 * ================================================================ */

void VRRenderThread::setupSkybox(vtkOpenVRRenderer* renderer,
                                  vtkOpenVRRenderWindow* renderWindow)
{
    /* vtkSkybox 需要 cubemap 格式贴图，程序化2D图像无法驱动它。
     * 改用 VTK 原生渐变背景：底部深蓝 → 顶部接近黑色，
     * 视觉效果类似太空背景，且在所有 VTK 版本下 100% 可用。 */
    renderer->SetBackground(0.10, 0.10, 0.30);   /* 底：深蓝 */
    renderer->SetBackground2(0.30, 0.50, 0.80);  /* 顶：亮蓝 */
    renderer->GradientBackgroundOn();
}

void VRRenderThread::setupSkyboxDesktop(vtkRenderer* renderer,
                                         vtkRenderWindow* renderWindow)
{
    /* 与VR模式相同的渐变背景 */
    renderer->SetBackground(0.10, 0.10, 0.30);
    renderer->SetBackground2(0.30, 0.50, 0.80);
    renderer->GradientBackgroundOn();
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

static void buildFloorActor(vtkRenderer* renderer)
{
    /* 20x20单位的灰色地板平面，y=-1使其位于模型正下方 */
    vtkNew<vtkPlaneSource> floorPlane;
    floorPlane->SetOrigin(-10.0, -1.0, -10.0);
    floorPlane->SetPoint1( 10.0, -1.0, -10.0);
    floorPlane->SetPoint2(-10.0, -1.0,  10.0);
    floorPlane->SetResolution(10, 10);
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
