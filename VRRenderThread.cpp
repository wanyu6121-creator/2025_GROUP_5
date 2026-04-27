/**  @file VRRenderThread.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程实现
 *   支持两种模式：
 *     - VR模式：连接HTC Vive头显时使用
 *     - 桌面模式：无头显时自动fallback，用普通窗口预览
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
{
}

VRRenderThread::~VRRenderThread()
{
    if (isRunning()) {
        requestInterruption();
        wait();
    }
    /* 释放Actor（由VR线程New出来，这里手动Delete）*/
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

    /* ---- 注册reader（可为nullptr，表示该零件无STL数据）---- */
    readerList.append(vtkSmartPointer<vtkSTLReader>(reader));

    /* ---- 为该Actor创建独立的过滤器和mapper ---- */
    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);

    vtkSmartPointer<vtkClipDataSet> cf = vtkSmartPointer<vtkClipDataSet>::New();
    cf->SetClipFunction(clipPlane.Get());

    vtkSmartPointer<vtkShrinkFilter> sf = vtkSmartPointer<vtkShrinkFilter>::New();
    sf->SetShrinkFactor(0.8);

    /* 从Actor的现有Mapper取出input connection以初始化reader指针 */
    vtkSmartPointer<vtkDataSetMapper> mapper = vtkSmartPointer<vtkDataSetMapper>::New();

    clipFilters.append(cf);
    shrinkFilters.append(sf);
    mapperList.append(mapper);
    clipState.append(clipOn);
    shrinkState.append(shrinkOn);

    /* 将新mapper绑定到actor（actor此时已通过getNewActor建立了pipeline）
     * 我们保留actor原有pipeline，rebuildPipeline会在需要时重建 */

    return idx;
}

void VRRenderThread::clearActors()
{
    /* 仅释放引用，不Delete Actor（由调用方决定生命周期）*/
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

/* ================================================================
 * run() — 自动选择模式
 * ================================================================ */

void VRRenderThread::run()
{
    /* 尝试初始化OpenVR运行时来检测头显 */
    bool vrAvailable = false;
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Scene);

    if (initError == vr::VRInitError_None && vrSystem != nullptr) {
        vrAvailable = true;
        /* 关闭检测用的初始化，vtkOpenVRRenderWindow会在内部重新初始化 */
        vr::VR_Shutdown();
    }

    if (vrAvailable) {
        runVRMode();
    } else {
        runDesktopMode();
    }
}

/* ================================================================
 * VR 模式
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
    renderer->SetBackground(0.05, 0.05, 0.15);

    /* 添加所有模型Actor */
    for (vtkActor* actor : actorList) {
        if (actor) renderer->AddActor(actor);
    }

    /* 场景设置 */
    setupFloor(renderer.Get());
    setupLighting(renderer.Get());

    /* 初始化渲染窗口和交互器（手柄控制器在此启用）*/
    renderWindow->Initialize();
    interactor->Initialize();

    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* 主渲染循环 */
    while (!isInterruptionRequested()) {

        /* 消费命令队列（线程安全，批量取出）*/
        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        while (!localQueue.isEmpty()) {
            processCommandVR(localQueue.dequeue(), renderer.Get());
        }

        /* 旋转动画 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        /* 渲染一帧并处理交互器事件（手柄输入在此消费）*/
        renderWindow->Render();
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* 清理 */
    interactor->TerminateApp();
    renderWindow->Finalize();
}

/* ================================================================
 * 桌面 fallback 模式
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

    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();
    renderWindow->Render();

    /* 桌面渲染循环 */
    while (!isInterruptionRequested()) {

        mutex.lock();
        QQueue<VRCmd> localQueue;
        localQueue.swap(commandQueue);
        mutex.unlock();

        while (!localQueue.isEmpty()) {
            processCommandDesktop(localQueue.dequeue(), renderer.Get());
        }

        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        renderWindow->Render();
        interactor->ProcessEvents();

        /* 窗口被用户手动关闭时退出 */
        if (renderWindow->GetSize()[0] == 0 && renderWindow->GetSize()[1] == 0) {
            break;
        }

        QThread::msleep(16);   /* ~60 fps */
    }
}

/* ================================================================
 * 命令处理（共用逻辑，分派到两个模式的函数中）
 * ================================================================ */

/** @brief 对单个Actor（或所有Actor）设置可见性 */
static void applyVisibility(QList<vtkActor*>& actors, int idx, bool visible)
{
    if (idx >= 0 && idx < actors.size()) {
        if (actors[idx]) actors[idx]->SetVisibility(visible ? 1 : 0);
    } else {
        /* idx == -1：全部Actor */
        for (vtkActor* a : actors) {
            if (a) a->SetVisibility(visible ? 1 : 0);
        }
    }
}

void VRRenderThread::processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer)
{
    switch (vcmd.cmd) {

    case CMD_SET_VISIBLE:
        applyVisibility(actorList, vcmd.actorIndex, vcmd.value > 0.5);
        break;

    case CMD_APPLY_FILTER: {
        /* value 编码：filterType * 10 + (enabled ? 1 : 0)
         * filterType: 0=clip, 1=shrink */
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

    /* 颜色通过共享Property自动同步，无需额外处理 */
    case CMD_SET_COLOUR_R:
    case CMD_SET_COLOUR_G:
    case CMD_SET_COLOUR_B:
        break;

    default:
        break;
    }
}

void VRRenderThread::processCommandDesktop(const VRCmd& vcmd, vtkRenderer* renderer)
{
    /* 桌面模式命令处理，逻辑与VR模式完全相同 */
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
 * 过滤器pipeline重建
 * ================================================================ */

void VRRenderThread::rebuildPipeline(int idx)
{
    if (idx < 0 || idx >= actorList.size()) return;

    vtkActor*       actor  = actorList[idx];
    vtkSTLReader*   reader = readerList[idx].Get();

    if (!actor || !reader) return;

    /* 取出该Actor当前使用的Mapper（由getNewActor创建，已绑定在actor上）*/
    vtkDataSetMapper* mapper =
        vtkDataSetMapper::SafeDownCast(actor->GetMapper());

    if (!mapper) return;

    vtkClipDataSet*  cf = clipFilters[idx].Get();
    vtkShrinkFilter* sf = shrinkFilters[idx].Get();

    bool doClip   = clipState[idx];
    bool doShrink = shrinkState[idx];

    /* 重新连接pipeline：Source → [Clip] → [Shrink] → Mapper */
    if (doClip && doShrink) {
        cf->SetInputConnection(reader->GetOutputPort());
        sf->SetInputConnection(cf->GetOutputPort());
        mapper->SetInputConnection(sf->GetOutputPort());

    } else if (doClip) {
        cf->SetInputConnection(reader->GetOutputPort());
        mapper->SetInputConnection(cf->GetOutputPort());

    } else if (doShrink) {
        sf->SetInputConnection(reader->GetOutputPort());
        mapper->SetInputConnection(sf->GetOutputPort());

    } else {
        mapper->SetInputConnection(reader->GetOutputPort());
    }

    mapper->Update();
}

/* ================================================================
 * 光照
 * ================================================================ */

void VRRenderThread::setupLighting(vtkOpenVRRenderer* renderer)
{
    /* 主光源 */
    vtkSmartPointer<vtkLight> mainLight = vtkSmartPointer<vtkLight>::New();
    mainLight->SetLightTypeToSceneLight();
    mainLight->SetPosition(5.0, 10.0, 15.0);
    mainLight->SetPositional(false);
    mainLight->SetFocalPoint(0.0, 0.0, 0.0);
    mainLight->SetDiffuseColor(1.0, 1.0, 1.0);
    mainLight->SetAmbientColor(0.3, 0.3, 0.3);
    mainLight->SetSpecularColor(1.0, 1.0, 1.0);
    mainLight->SetIntensity(0.8);
    renderer->AddLight(mainLight);

    /* 补光（冷色调，来自侧后方）*/
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
    vtkSmartPointer<vtkLight> mainLight = vtkSmartPointer<vtkLight>::New();
    mainLight->SetLightTypeToSceneLight();
    mainLight->SetPosition(5.0, 10.0, 15.0);
    mainLight->SetPositional(false);
    mainLight->SetFocalPoint(0.0, 0.0, 0.0);
    mainLight->SetDiffuseColor(1.0, 1.0, 1.0);
    mainLight->SetAmbientColor(0.3, 0.3, 0.3);
    mainLight->SetSpecularColor(1.0, 1.0, 1.0);
    mainLight->SetIntensity(0.8);
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
