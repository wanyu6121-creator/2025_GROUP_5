/**  @file VRRenderThread.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程 - 在独立线程中运行OpenVR渲染循环
 *   通过线程安全命令队列与Qt主线程通信
 */

#ifndef VRRENDERTHREAD_H
#define VRRENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QPair>

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>
#include <vtkLight.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

/**
 * @brief 跨线程命令枚举
 *
 * GUI线程通过 issueCommand() 发送命令，
 * VR线程在每帧渲染前消费并执行。
 */
enum VRCommand {
    CMD_SET_COLOUR_R = 1,  /**< 设置Actor红色分量（自动同步，保留备用）*/
    CMD_SET_COLOUR_G = 2,  /**< 设置Actor绿色分量（自动同步，保留备用）*/
    CMD_SET_COLOUR_B = 3,  /**< 设置Actor蓝色分量（自动同步，保留备用）*/
    CMD_SET_VISIBLE  = 4,  /**< 设置所有Actor可见性 (1.0=可见, 0.0=隐藏) */
    CMD_START_ROTATE = 5,  /**< 开始模型旋转动画 */
    CMD_STOP_ROTATE  = 6,  /**< 停止模型旋转动画 */
    CMD_RESET_VIEW   = 7,  /**< 重置相机视角到初始位置 */
};

/**
 * @brief VR渲染线程类
 *
 * 在独立QThread中运行完整的OpenVR渲染循环。
 * 功能包括：
 *   - 渲染从GUI传入的模型Actor（独立pipeline，共享属性）
 *   - 场景光照（主光源+补光）
 *   - 地板平面（增强空间感）
 *   - 模型旋转动画（可开关）
 *   - 通过命令队列接收GUI的实时更新
 */
class VRRenderThread : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent Qt父对象
     */
    explicit VRRenderThread(QObject* parent = nullptr);

    /**
     * @brief 析构函数，自动请求线程停止并等待退出
     */
    ~VRRenderThread() override;

    /**
     * @brief 在线程启动前添加要渲染的Actor（仅在start()之前调用）
     * @param actor 通过ModelPart::getNewActor()获得的独立VR Actor
     */
    void addActorOffline(vtkActor* actor);

    /**
     * @brief 向VR线程发送命令（线程安全，可在任意线程调用）
     * @param cmd   命令类型，见VRCommand枚举
     * @param value 命令参数值
     */
    void issueCommand(int cmd, double value);

protected:
    /**
     * @brief VR渲染主循环（在独立线程中自动执行）
     *
     * 初始化OpenVR → 添加Actor → 添加地板 → 设置光照
     * → 每帧消费命令队列 → 渲染
     * 直到requestInterruption()被调用后退出并清理资源。
     */
    void run() override;

private:
    /**
     * @brief 在VR线程内执行单条命令
     * @param cmd      命令类型
     * @param value    命令参数值
     * @param renderer 当前VR渲染器
     */
    void processCommand(int cmd, double value, vtkOpenVRRenderer* renderer);

    /**
     * @brief 初始化场景光照（主光源+补光）
     * @param renderer 目标渲染器
     */
    void setupLighting(vtkOpenVRRenderer* renderer);

    /**
     * @brief 创建并添加地板平面Actor到VR场景
     * @param renderer 目标渲染器
     */
    void setupFloor(vtkOpenVRRenderer* renderer);

    /* ---- 离线Actor列表 ---- */
    QList<vtkActor*>          actorList;     /**< 所有VR模型Actor指针 */

    /* ---- 线程安全命令队列 ---- */
    QMutex                    mutex;         /**< 保护commandQueue的互斥锁 */
    QQueue<QPair<int,double>> commandQueue;  /**< 待处理命令队列 */

    /* ---- 动画状态 ---- */
    bool                      isRotating;    /**< 是否正在执行旋转动画 */
};

#endif // VRRENDERTHREAD_H
