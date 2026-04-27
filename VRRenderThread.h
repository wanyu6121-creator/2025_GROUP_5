/**  @file VRRenderThread.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程类 - 在独立线程中运行OpenVR渲染循环
 *   与Qt主线程通过命令队列进行线程安全的通信
 */

#ifndef VRRENDERTHREAD_H
#define VRRENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QPair>

/* VTK OpenVR 相关头文件 */
#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>
#include <vtkActorCollection.h>
#include <vtkLight.h>

/**
 * @brief 跨线程命令类型枚举
 *
 * GUI线程通过 issueCommand() 发送这些命令，
 * VR线程在每帧渲染前消费命令队列并执行对应操作。
 */
enum VRCommand {
    CMD_SET_COLOUR_R  = 1,  /**< 设置当前目标Actor的红色分量 (val: 0.0-1.0) */
    CMD_SET_COLOUR_G  = 2,  /**< 设置当前目标Actor的绿色分量 (val: 0.0-1.0) */
    CMD_SET_COLOUR_B  = 3,  /**< 设置当前目标Actor的蓝色分量 (val: 0.0-1.0) */
    CMD_SET_VISIBLE   = 4,  /**< 设置当前目标Actor的可见性 (val: 1.0=可见, 0.0=隐藏) */
    CMD_START_ROTATE  = 5,  /**< 开始模型旋转动画 */
    CMD_STOP_ROTATE   = 6,  /**< 停止模型旋转动画 */
    CMD_RESET_VIEW    = 7,  /**< 重置相机视角 */
};

/**
 * @brief VR渲染线程类
 *
 * 继承自QThread，在独立线程中运行完整的OpenVR渲染循环。
 * 与GUI线程的通信通过线程安全的命令队列实现，
 * 避免直接跨线程访问VTK对象导致崩溃。
 *
 * 典型用法：
 * @code
 *   vrThread = new VRRenderThread(this);
 *   // 在启动前添加actor（线程安全）
 *   vrThread->addActorOffline(someActor);
 *   vrThread->start();
 *   // 线程运行期间通过命令更新
 *   vrThread->issueCommand(CMD_SET_VISIBLE, 0.0);
 *   // 停止线程
 *   vrThread->requestInterruption();
 *   vrThread->wait();
 * @endcode
 */
class VRRenderThread : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param parent Qt父对象指针，用于Qt对象树管理
     */
    explicit VRRenderThread(QObject* parent = nullptr);

    /**
     * @brief 析构函数，确保线程安全退出
     */
    ~VRRenderThread() override;

    /**
     * @brief 在线程启动前离线添加Actor（线程安全）
     *
     * 必须在调用 start() 之前调用此函数。
     * 这些Actor是从 ModelPart::getNewActor() 获得的独立Actor，
     * 与GUI渲染器中的Actor完全分开，但共享同一份vtkProperty。
     *
     * @param actor 指向要在VR中渲染的vtkActor的指针（裸指针，由VRRenderThread接管生命周期）
     */
    void addActorOffline(vtkActor* actor);

    /**
     * @brief 向VR线程发送命令（线程安全）
     *
     * 可在VR线程运行期间从任意线程调用。
     * 命令会被放入队列，在下一帧渲染前被消费执行。
     *
     * @param cmd   命令类型，见 VRCommand 枚举
     * @param value 命令参数值（含义取决于命令类型）
     */
    void issueCommand(int cmd, double value);

protected:
    /**
     * @brief VR渲染主循环（在独立线程中执行）
     *
     * 初始化OpenVR渲染器，将所有离线添加的Actor加入场景，
     * 然后进入渲染循环直到被 requestInterruption() 中断。
     * 每帧渲染前先消费并执行命令队列中的所有待处理命令。
     */
    void run() override;

private:
    /**
     * @brief 处理单条命令（在VR线程内部调用）
     * @param cmd   命令类型
     * @param value 命令参数值
     */
    void processCommand(int cmd, double value);

    /**
     * @brief 设置VR场景光照
     * @param renderer 要添加光照的渲染器
     */
    void setupLighting(vtkOpenVRRenderer* renderer);

    /* ---- 离线Actor列表（线程启动前填充，之后只读）---- */
    QList<vtkActor*>  actorList;        /**< 存储所有要在VR中渲染的Actor指针 */

    /* ---- 线程安全命令队列 ---- */
    QMutex            mutex;            /**< 保护 commandQueue 的互斥锁 */
    QQueue<QPair<int,double>> commandQueue; /**< 待处理命令的队列 */

    /* ---- 动画状态 ---- */
    bool              isRotating;       /**< 是否正在执行旋转动画 */
    double            rotationAngle;    /**< 当前累积旋转角度（度） */
};

#endif // VRRENDERTHREAD_H
