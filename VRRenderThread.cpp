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
}

void VRRenderThread::addActorOffline(vtkActor* actor)
{
    if (actor != nullptr) {
        actorList.append(actor);
    }
}

void VRRenderThread::issueCommand(int cmd, double value)
{
    QMutexLocker locker(&mutex);
    commandQueue.enqueue(qMakePair(cmd, value));
}

void VRRenderThread::run()
{
    /* ================================================================
     * 首先尝试检测VR设备是否可用
     * 使用一个独立的检测块，失败则回退到桌面模式
     * ================================================================ */
    bool vrAvailable = false;

    /* 尝试初始化OpenVR运行时来检测头显 */
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(
        &initError,
        vr::VRApplication_Scene
        );

    if (initError == vr::VRInitError_None && vrSystem != nullptr) {
        vrAvailable = true;
        /* 检测完毕，关闭这次初始化
         * vtkOpenVRRenderWindow会在自己内部重新初始化 */
        vr::VR_Shutdown();
    }

    if (vrAvailable) {
        runVRMode();
    } else {
        runDesktopMode();
    }
}

void VRRenderThread::runVRMode()
{
    /* ================================================================
     * VR模式：连接头显时的完整渲染循环
     * ================================================================ */
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
        renderer->AddActor(actor);
    }

    /* 添加地板和光照 */
    setupFloor(renderer.Get());
    setupLighting(renderer.Get());

    /* 初始化并重置相机 */
    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* 主渲染循环 */
    while (!isInterruptionRequested()) {

        /* 消费命令队列 */
        mutex.lock();
        while (!commandQueue.isEmpty()) {
            auto pair = commandQueue.dequeue();
            mutex.unlock();
            processCommandVR(pair.first, pair.second, renderer.Get());
            mutex.lock();
        }
        mutex.unlock();

        /* 旋转动画 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        renderWindow->Render();
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* 清理 */
    renderWindow->Finalize();
}

void VRRenderThread::runDesktopMode()
{
    /* ================================================================
     * 桌面模式：无头显时的fallback渲染循环
     *
     * 使用普通的vtkRenderWindow在桌面显示一个预览窗口，
     * 功能与VR模式相同（旋转、颜色同步等），只是输出到屏幕而不是头显。
     * 这样可以在没有VR设备的情况下测试所有功能。
     * ================================================================ */
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
        renderer->AddActor(actor);
    }

    /* 添加地板和光照（与VR模式相同）*/
    setupFloorDesktop(renderer.Get());
    setupLightingDesktop(renderer.Get());

    /* 初始化并重置相机 */
    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();
    renderWindow->Render();

    /* ----------------------------------------------------------------
     * 桌面模式渲染循环
     * 使用定时器模式：每帧处理命令队列 + 渲染，
     * 同时响应窗口关闭事件
     * ---------------------------------------------------------------- */
    while (!isInterruptionRequested()) {

        /* 消费命令队列 */
        mutex.lock();
        while (!commandQueue.isEmpty()) {
            auto pair = commandQueue.dequeue();
            mutex.unlock();
            processCommandDesktop(pair.first, pair.second, renderer.Get());
            mutex.lock();
        }
        mutex.unlock();

        /* 旋转动画 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        /* 渲染并处理窗口事件（非阻塞）*/
        renderWindow->Render();
        interactor->ProcessEvents();

        /* 如果用户手动关闭了预览窗口，退出循环 */
        if (!renderWindow->GetSize()[0]) {
            break;
        }

        /* 短暂休眠，避免CPU占用过高（约60fps）*/
        QThread::msleep(16);
    }
}

void VRRenderThread::processCommandVR(int cmd, double value, vtkOpenVRRenderer* renderer)
{
    switch (cmd) {
    case CMD_SET_VISIBLE:
        for (vtkActor* actor : actorList) {
            if (actor != nullptr)
                actor->SetVisibility(value > 0.5 ? 1 : 0);
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
        /* 颜色通过SetProperty共享自动同步 */
        break;
    default:
        break;
    }
}

void VRRenderThread::processCommandDesktop(int cmd, double value, vtkRenderer* renderer)
{
    /* 桌面模式命令处理，逻辑与VR模式相同 */
    switch (cmd) {
    case CMD_SET_VISIBLE:
        for (vtkActor* actor : actorList) {
            if (actor != nullptr)
                actor->SetVisibility(value > 0.5 ? 1 : 0);
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

    /* 补光 */
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
    /* 桌面模式光照，与VR模式相同设置 */
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

void VRRenderThread::setupFloor(vtkOpenVRRenderer* renderer)
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

void VRRenderThread::setupFloorDesktop(vtkRenderer* renderer)
{
    /* 桌面模式地板，与VR模式完全相同 */
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
