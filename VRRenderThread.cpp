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
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>

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
     * 初始化OpenVR渲染环境
     * ================================================================ */
    vtkNew<vtkOpenVRRenderer>               renderer;
    vtkNew<vtkOpenVRRenderWindow>           renderWindow;
    vtkNew<vtkOpenVRRenderWindowInteractor> interactor;
    vtkNew<vtkOpenVRCamera>                 camera;

    renderWindow->AddRenderer(renderer);
    renderWindow->SetSize(2160, 1200);
    interactor->SetRenderWindow(renderWindow);

    renderer->SetActiveCamera(camera);
    renderer->SetBackground(0.05, 0.05, 0.15); /* 深蓝色背景 */

    /* ================================================================
     * 添加所有模型Actor
     * ================================================================ */
    for (vtkActor* actor : actorList) {
        renderer->AddActor(actor);
    }

    /* ================================================================
     * 添加地板（阶段三新增）
     * ================================================================ */
    setupFloor(renderer.Get());

    /* ================================================================
     * 添加场景光照
     * ================================================================ */
    setupLighting(renderer.Get());

    /* ================================================================
     * 初始化渲染并重置相机
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

        /* 消费命令队列 */
        mutex.lock();
        while (!commandQueue.isEmpty()) {
            auto pair = commandQueue.dequeue();
            mutex.unlock();
            processCommand(pair.first, pair.second, renderer.Get());
            mutex.lock();
        }
        mutex.unlock();

        /* 旋转动画：每帧旋转0.5度 */
        if (isRotating) {
            renderer->GetActiveCamera()->Azimuth(0.5);
            renderer->ResetCameraClippingRange();
        }

        /* 渲染一帧 */
        renderWindow->Render();

        /* 处理VR手柄事件（非阻塞）*/
        interactor->DoOneEvent(renderWindow.Get(), renderer.Get());
    }

    /* ================================================================
     * 清理资源（必须在VR线程内完成）
     * ================================================================ */
    renderWindow->Finalize();
}

void VRRenderThread::processCommand(int cmd, double value, vtkOpenVRRenderer* renderer)
{
    switch (cmd) {

    case CMD_SET_VISIBLE:
        /* 遍历所有模型Actor设置可见性
         * 颜色通过SetProperty自动同步，可见性需手动设置 */
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
        /* 重置相机到初始视角并停止旋转 */
        isRotating = false;
        if (renderer != nullptr) {
            renderer->ResetCamera();
            renderer->GetActiveCamera()->Azimuth(30);
            renderer->GetActiveCamera()->Elevation(30);
            renderer->ResetCameraClippingRange();
        }
        break;

    /* 颜色通过SetProperty共享自动同步，此处无需操作 */
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

    /* 补光：消除纯黑阴影面，模拟天空漫反射 */
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
    /* ----------------------------------------------------------------
     * 创建地板平面
     *
     * 使用vtkPlaneSource创建一个大平面作为地板，
     * 放置在模型下方（y=-1），大小为20x20单位。
     * 灰色半透明，增强VR场景的空间感和地面参考感。
     * ---------------------------------------------------------------- */
    vtkNew<vtkPlaneSource> floorPlane;
    floorPlane->SetOrigin(-10.0, -1.0, -10.0);
    floorPlane->SetPoint1( 10.0, -1.0, -10.0);
    floorPlane->SetPoint2(-10.0, -1.0,  10.0);
    floorPlane->SetResolution(10, 10); /* 网格细分，使光照更自然 */
    floorPlane->Update();

    vtkNew<vtkPolyDataMapper> floorMapper;
    floorMapper->SetInputConnection(floorPlane->GetOutputPort());

    vtkNew<vtkActor> floorActor;
    floorActor->SetMapper(floorMapper);

    /* 深灰色地板，模拟展示台效果 */
    floorActor->GetProperty()->SetColor(0.3, 0.3, 0.3);
    floorActor->GetProperty()->SetAmbient(0.5);
    floorActor->GetProperty()->SetDiffuse(0.5);
    floorActor->GetProperty()->SetSpecular(0.1);

    renderer->AddActor(floorActor);
}
