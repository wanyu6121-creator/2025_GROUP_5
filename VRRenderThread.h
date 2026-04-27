/**  @file VRRenderThread.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程 - 支持VR模式和桌面预览模式
 *
 *   VR模式：连接HTC Vive头显时自动使用
 *   桌面模式：无头显时自动fallback，用普通窗口预览
 */

#ifndef VRRENDERTHREAD_H
#define VRRENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QPair>

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkLight.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>

/* OpenVR SDK头文件（用于检测头显是否连接）*/
#include <openvr.h>

/**
 * @brief 跨线程命令枚举
 */
enum VRCommand {
    CMD_SET_COLOUR_R = 1,  /**< 红色分量（共享Property自动同步，保留备用）*/
    CMD_SET_COLOUR_G = 2,  /**< 绿色分量（共享Property自动同步，保留备用）*/
    CMD_SET_COLOUR_B = 3,  /**< 蓝色分量（共享Property自动同步，保留备用）*/
    CMD_SET_VISIBLE  = 4,  /**< 可见性 (1.0=可见, 0.0=隐藏) */
    CMD_START_ROTATE = 5,  /**< 开始旋转动画 */
    CMD_STOP_ROTATE  = 6,  /**< 停止旋转动画 */
    CMD_RESET_VIEW   = 7,  /**< 重置相机视角 */
};

/**
 * @brief VR渲染线程类
 *
 * 在独立QThread中运行渲染循环。
 * 自动检测VR头显：
 *   - 检测到头显 → 使用vtkOpenVRRenderWindow渲染到HTC Vive
 *   - 未检测到  → 回退到桌面预览窗口，功能完全相同
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
     * @brief 析构函数，自动停止线程
     */
    ~VRRenderThread() override;

    /**
     * @brief 在线程启动前添加Actor（仅在start()之前调用）
     * @param actor 通过ModelPart::getNewActor()获得的独立Actor
     */
    void addActorOffline(vtkActor* actor);

    /**
     * @brief 向VR线程发送命令（线程安全）
     * @param cmd   命令类型，见VRCommand枚举
     * @param value 命令参数值
     */
    void issueCommand(int cmd, double value);

protected:
    /**
     * @brief 渲染主入口（自动选择VR模式或桌面模式）
     */
    void run() override;

private:
    /**
     * @brief VR模式渲染循环（连接头显时使用）
     */
    void runVRMode();

    /**
     * @brief 桌面模式渲染循环（无头显时fallback）
     */
    void runDesktopMode();

    /**
     * @brief 在VR模式下处理命令
     * @param cmd      命令类型
     * @param value    命令参数
     * @param renderer VR渲染器
     */
    void processCommandVR(int cmd, double value, vtkOpenVRRenderer* renderer);

    /**
     * @brief 在桌面模式下处理命令
     * @param cmd      命令类型
     * @param value    命令参数
     * @param renderer 桌面渲染器
     */
    void processCommandDesktop(int cmd, double value, vtkRenderer* renderer);

    /**
     * @brief 初始化VR场景光照
     * @param renderer VR渲染器
     */
    void setupLighting(vtkOpenVRRenderer* renderer);

    /**
     * @brief 初始化桌面场景光照
     * @param renderer 桌面渲染器
     */
    void setupLightingDesktop(vtkRenderer* renderer);

    /**
     * @brief 创建VR场景地板
     * @param renderer VR渲染器
     */
    void setupFloor(vtkOpenVRRenderer* renderer);

    /**
     * @brief 创建桌面场景地板
     * @param renderer 桌面渲染器
     */
    void setupFloorDesktop(vtkRenderer* renderer);

    /* ---- 离线Actor列表 ---- */
    QList<vtkActor*>          actorList;     /**< 所有模型Actor指针 */

    /* ---- 线程安全命令队列 ---- */
    QMutex                    mutex;         /**< 保护commandQueue的互斥锁 */
    QQueue<QPair<int,double>> commandQueue;  /**< 待处理命令队列 */

    /* ---- 动画状态 ---- */
    bool                      isRotating;    /**< 是否正在旋转 */
};

#endif // VRRENDERTHREAD_H
