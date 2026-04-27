/**  @file VRRenderThread.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程实现
 */

#include "VRRenderThread.h"

#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkCamera.h>

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
    /* 仅在 start() 之前调用，无需加锁 */
    if (actor != nullptr) {
        actorList.append(actor);
    }
}

void VRRenderThread::issueCommand(int cmd, double value)
{
    /* GUI线程调用此函数，必须加锁保护队列 */
    QMutexLocker locker(&mutex);
    commandQueue.enqueue(qMakePair(cmd, value));
}

void VRRenderThread::run()
{
    /* ================================================================
     * 初始化 OpenVR 渲染环境
     * ================================================================ */
    vtkNew<vtkOpenVRRenderer>               renderer;
    vtkNew<vtkOpenVRRenderWindow>           renderWindow;
    vtkNew<vtkOpenVRRenderWindowInteractor> interactor;
    vtkNew<vtkOpenVRCamera>                 camera;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(2160, 1200);
    interactor->SetRenderWindow(renderWindow);

    renderer->SetActiveCamera(camera);
    renderer->SetBackground(0.1, 0.1, 0.2);

    /* ================================================================
     * 将所有离线添加的Actor加入VR渲染器
     * ================================================================ */
    for (vtkActor* actor : actorList) {
        renderer->AddActor(actor);
    }

    /* ================================================================
     * 添加场景光照
     * ================================================================ */
    setupLighting(renderer.Get());

    /* ================================================================
     * 初始化并重置相机
     * ================================================================ */
    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* ================================================================
     * 主渲染循环
     * ================================================================ */
    while (!isInterruptionRequested()) {

        /* -- 消费命令队列 -- */
        mutex.lock();
        while (!commandQueue.isEmpty()) {
            auto pair = commandQueue.dequeue();
            mutex.unlock();
            /* 在锁外处理命令，减少锁持有时间 */
            processCommand(pair.first, pair.second, renderer.Get());
            mutex.lock();
        }
        mutex.unlock();

        /* -- 旋转动画 -- */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        /* -- 渲染一帧 -- */
        renderWindow->Render();

        /* -- 处理VR手柄事件（非阻塞）-- */
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* ================================================================
     * 清理资源（必须在VR线程内完成）
     * ================================================================ */
    renderWindow->Finalize();
}

void VRRenderThread::processCommand(int cmd, double value, vtkOpenVRRenderer* renderer)
{
    /* 此函数在VR线程内调用，可安全修改VTK对象 */
    switch (cmd) {

    case CMD_SET_VISIBLE:
        /* ----------------------------------------------------------------
         * 可见性同步：遍历所有VR Actor，逐个设置可见性
         *
         * 为什么需要这个？
         * 颜色通过 SetProperty 共享自动同步，但 SetVisibility 是
         * Actor级别的属性，不在 vtkProperty 中，必须手动设置每个Actor。
         * ---------------------------------------------------------------- */
        for (vtkActor* actor : actorList) {
            if (actor != nullptr) {
                actor->SetVisibility(value > 0.5 ? 1 : 0);
            }
        }
        break;

    case CMD_START_ROTATE:
        isRotating = true;
        break;

    case CMD_STOP_ROTATE:
        isRotating = false;
        break;

    case CMD_RESET_VIEW:
        /* 重置相机到初始视角 */
        isRotating = false;
        if (renderer != nullptr) {
            renderer->ResetCamera();
            renderer->GetActiveCamera()->Azimuth(30);
            renderer->GetActiveCamera()->Elevation(30);
            renderer->ResetCameraClippingRange();
        }
        break;

    /* 颜色命令：由于共享Property已自动同步，此处无需额外操作
     * 保留这些case是为了将来可以扩展精准控制单个零件 */
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
    /* 主光源：模拟头顶自然光 */
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

    /* 补光：消除纯黑阴影面 */
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
