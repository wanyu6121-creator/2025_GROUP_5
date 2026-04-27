/**  @file VRRenderThread.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程类实现
 */

#include "VRRenderThread.h"

#include <vtkSmartPointer.h>
#include <vtkNew.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkActorCollection.h>

VRRenderThread::VRRenderThread(QObject* parent)
    : QThread(parent)
    , isRotating(false)
    , rotationAngle(0.0)
{
}

VRRenderThread::~VRRenderThread()
{
    /* 确保线程已完全退出再销毁对象，防止崩溃 */
    if (isRunning()) {
        requestInterruption();
        wait();
    }
}

void VRRenderThread::addActorOffline(vtkActor* actor)
{
    /* 此函数只应在 start() 之前调用，无需加锁 */
    if (actor != nullptr) {
        actorList.append(actor);
    }
}

void VRRenderThread::issueCommand(int cmd, double value)
{
    /* 加锁保证线程安全：GUI线程和VR线程都可能访问 commandQueue */
    QMutexLocker locker(&mutex);
    commandQueue.enqueue(qMakePair(cmd, value));
}

void VRRenderThread::run()
{
    /* ================================================================
     * 步骤1：初始化 OpenVR 渲染器、渲染窗口和交互器
     * ================================================================ */
    vtkNew<vtkOpenVRRenderer>              renderer;
    vtkNew<vtkOpenVRRenderWindow>          renderWindow;
    vtkNew<vtkOpenVRRenderWindowInteractor> interactor;
    vtkNew<vtkOpenVRCamera>                camera;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(2160, 1200);  /* HTC Vive Pro 2 分辨率 */
    interactor->SetRenderWindow(renderWindow);
    interactor->SetInteractorStyle(nullptr); /* 使用默认VR交互风格 */

    renderer->SetActiveCamera(camera);
    renderer->SetBackground(0.1, 0.1, 0.2); /* 深蓝色背景，比纯黑更有层次感 */

    /* ================================================================
     * 步骤2：将所有离线添加的Actor加入VR渲染器
     * ================================================================ */
    for (vtkActor* actor : actorList) {
        renderer->AddActor(actor);
    }

    /* ================================================================
     * 步骤3：添加场景光照
     * ================================================================ */
    setupLighting(renderer.Get());

    /* ================================================================
     * 步骤4：初始化渲染并重置相机
     * ================================================================ */
    renderWindow->Initialize();
    renderer->ResetCamera();
    renderer->GetActiveCamera()->Azimuth(30);
    renderer->GetActiveCamera()->Elevation(30);
    renderer->ResetCameraClippingRange();

    /* ================================================================
     * 步骤5：主渲染循环
     * 每帧先消费命令队列，再渲染，直到被 requestInterruption() 中断
     * ================================================================ */
    while (!isInterruptionRequested()) {

        /* -- 消费命令队列（加锁，快进快出）-- */
        mutex.lock();
        while (!commandQueue.isEmpty()) {
            auto pair = commandQueue.dequeue();
            mutex.unlock();
            processCommand(pair.first, pair.second); /* 在锁外处理命令，减少锁持有时间 */
            mutex.lock();
        }
        mutex.unlock();

        /* -- 旋转动画 -- */
        if (isRotating) {
            rotationAngle += 0.5; /* 每帧旋转0.5度 */
            renderer->GetActiveCamera()->Azimuth(0.5);
        }

        /* -- 渲染一帧 -- */
        renderWindow->Render();

        /* -- 处理VR事件（手柄输入等），非阻塞 -- */
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* ================================================================
     * 步骤6：清理资源，确保干净退出
     * 必须在VR线程内部完成，不能在主线程中清理VTK VR对象
     * ================================================================ */
    renderWindow->Finalize();
}

void VRRenderThread::processCommand(int cmd, double value)
{
    /* 此函数在VR线程内部调用，可以安全地修改VTK对象 */
    switch (cmd) {

    case CMD_START_ROTATE:
        isRotating = true;
        break;

    case CMD_STOP_ROTATE:
        isRotating = false;
        break;

    case CMD_RESET_VIEW:
        /* 注意：此处需要访问renderer，但renderer是局部变量。
         * 实际项目中可将renderer设为成员变量，此处作为示例展示命令框架 */
        rotationAngle = 0.0;
        isRotating = false;
        break;

    /* 颜色和可见性命令：
     * 由于我们用 newActor->SetProperty(actor->GetProperty()) 共享了属性，
     * GUI那边修改actor属性会自动反映到VR的actor上，
     * 这些命令作为显式同步的备用方案保留 */
    case CMD_SET_COLOUR_R:
    case CMD_SET_COLOUR_G:
    case CMD_SET_COLOUR_B:
    case CMD_SET_VISIBLE:
        /* 共享属性机制已经自动同步，此处无需额外操作 */
        break;

    default:
        break;
    }
}

void VRRenderThread::setupLighting(vtkOpenVRRenderer* renderer)
{
    /* ----------------------------------------------------------------
     * 主光源：模拟头顶自然光
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkLight> mainLight = vtkSmartPointer<vtkLight>::New();
    mainLight->SetLightTypeToSceneLight();
    mainLight->SetPosition(5.0, 10.0, 15.0);
    mainLight->SetPositional(false);           /* false = 方向光，不随距离衰减 */
    mainLight->SetFocalPoint(0.0, 0.0, 0.0);
    mainLight->SetDiffuseColor(1.0, 1.0, 1.0);
    mainLight->SetAmbientColor(0.3, 0.3, 0.3);
    mainLight->SetSpecularColor(1.0, 1.0, 1.0);
    mainLight->SetIntensity(0.8);
    renderer->AddLight(mainLight);

    /* ----------------------------------------------------------------
     * 补光：从另一侧补充阴影面，避免完全黑暗
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkLight> fillLight = vtkSmartPointer<vtkLight>::New();
    fillLight->SetLightTypeToSceneLight();
    fillLight->SetPosition(-8.0, 5.0, -5.0);
    fillLight->SetPositional(false);
    fillLight->SetFocalPoint(0.0, 0.0, 0.0);
    fillLight->SetDiffuseColor(0.8, 0.9, 1.0); /* 略带蓝色的补光，模拟天空漫反射 */
    fillLight->SetAmbientColor(0.0, 0.0, 0.0);
    fillLight->SetSpecularColor(0.0, 0.0, 0.0);
    fillLight->SetIntensity(0.4);
    renderer->AddLight(fillLight);
}
