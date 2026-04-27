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

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkLight.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkClipDataSet.h>
#include <vtkShrinkFilter.h>
#include <vtkPlane.h>

#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>

/* OpenVR SDK头文件（用于检测头显是否连接）*/
#include <openvr.h>

/**
 * @brief 跨线程命令枚举
 *
 * 命令通过 issueCommand(cmd, value, actorIndex) 发送。
 * actorIndex = -1 表示全局操作（如 ResetView、全局旋转）。
 */
enum VRCommand {
    CMD_SET_COLOUR_R = 1,  /**< 红色分量（共享Property自动同步，保留备用）*/
    CMD_SET_COLOUR_G = 2,  /**< 绿色分量 */
    CMD_SET_COLOUR_B = 3,  /**< 蓝色分量 */
    CMD_SET_VISIBLE  = 4,  /**< 可见性 (value>0.5=可见), actorIndex指定目标Actor */
    CMD_START_ROTATE = 5,  /**< 开始旋转动画 */
    CMD_STOP_ROTATE  = 6,  /**< 停止旋转动画 */
    CMD_RESET_VIEW   = 7,  /**< 重置相机视角 */
    CMD_APPLY_FILTER = 8,  /**< 过滤器同步: value=(filterType*10+enabled), actorIndex指定目标 */
    CMD_ADD_ACTOR    = 9,  /**< 动态添加Actor（加分项，value为临时指针地址，不可跨平台）*/
    CMD_REMOVE_ACTOR = 10, /**< 动态移除Actor（加分项，actorIndex指定目标）*/
};

/**
 * @brief 过滤器类型常量（用于CMD_APPLY_FILTER的value编码）
 *
 * value = filterType * 10 + (enabled ? 1 : 0)
 * 例如：裁剪启用 = 0*10+1 = 1；收缩禁用 = 1*10+0 = 10
 */
static const int FILTER_CLIP   = 0;  /**< 裁剪滤镜 */
static const int FILTER_SHRINK = 1;  /**< 收缩滤镜 */

/**
 * @brief VR命令结构体
 */
struct VRCmd {
    int    cmd;        /**< 命令类型，见VRCommand枚举 */
    double value;      /**< 参数值 */
    int    actorIndex; /**< 目标Actor在actorList中的索引，-1表示全局 */

    /** @param c 命令类型  @param v 参数  @param idx Actor索引（默认-1=全局）*/
    VRCmd(int c, double v, int idx = -1)
        : cmd(c), value(v), actorIndex(idx) {}
};

/**
 * @brief VR渲染线程类
 *
 * 在独立QThread中运行渲染循环，自动检测VR头显并选择模式。
 * 命令通过线程安全的命令队列传递，渲染循环每帧消费。
 *
 * 使用说明：
 *   1. new VRRenderThread()
 *   2. 调用 addActorOffline() 注册所有Actor，返回值为actorIndex
 *   3. 调用 start() 启动线程
 *   4. 运行期间通过 issueCommand(cmd, value, actorIndex) 控制
 *   5. 停止：requestInterruption() + wait()
 *   6. 重启：clearActors() 后重复步骤2-5
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
     *
     * 同时注册该Actor对应的reader（用于过滤器重建pipeline）。
     *
     * @param actor  通过ModelPart::getNewActor()获得的独立Actor
     * @param reader 该零件的STLReader（与GUI侧共享，只读）
     * @param clipOn   初始裁剪状态
     * @param shrinkOn 初始收缩状态
     * @return 该Actor在actorList中的索引（用于后续issueCommand的actorIndex）
     */
    int addActorOffline(vtkActor* actor,
                        vtkSTLReader* reader = nullptr,
                        bool clipOn = false,
                        bool shrinkOn = false);

    /**
     * @brief 清空Actor列表（VR重启前调用）
     */
    void clearActors();

    /**
     * @brief 向VR线程发送命令（线程安全，可从任意线程调用）
     * @param cmd        命令类型，见VRCommand枚举
     * @param value      命令参数值
     * @param actorIndex 目标Actor索引（-1表示全局操作）
     */
    void issueCommand(int cmd, double value, int actorIndex = -1);

protected:
    /**
     * @brief 渲染主入口（QThread::run覆盖），自动选择VR或桌面模式
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
     * @brief 处理单条命令（VR模式）
     * @param vcmd     命令结构体
     * @param renderer VR渲染器指针
     */
    void processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer);

    /**
     * @brief 处理单条命令（桌面模式）
     * @param vcmd     命令结构体
     * @param renderer 桌面渲染器指针
     */
    void processCommandDesktop(const VRCmd& vcmd, vtkRenderer* renderer);

    /**
     * @brief 重建指定索引Actor的过滤器pipeline
     *
     * 根据 clipState[idx] 和 shrinkState[idx] 重新连接：
     *   reader → [ClipFilter] → [ShrinkFilter] → Mapper
     *
     * @param idx Actor在actorList中的索引
     */
    void rebuildPipeline(int idx);

    /**
     * @brief 初始化VR场景光照（主光+补光）
     * @param renderer VR渲染器
     */
    void setupLighting(vtkOpenVRRenderer* renderer);

    /**
     * @brief 初始化桌面场景光照
     * @param renderer 桌面渲染器
     */
    void setupLightingDesktop(vtkRenderer* renderer);

    /**
     * @brief 创建VR场景地板平面
     * @param renderer VR渲染器
     */
    void setupFloor(vtkOpenVRRenderer* renderer);

    /**
     * @brief 创建桌面场景地板平面
     * @param renderer 桌面渲染器
     */
    void setupFloorDesktop(vtkRenderer* renderer);

    /* ---- Actor列表及其pipeline组件（按索引对应）---- */
    QList<vtkActor*>                          actorList;     /**< 模型Actor裸指针列表 */
    QList<vtkSmartPointer<vtkSTLReader>>      readerList;    /**< 各Actor的STL读取器 */
    QList<vtkSmartPointer<vtkDataSetMapper>>  mapperList;    /**< 各Actor的Mapper */
    QList<vtkSmartPointer<vtkClipDataSet>>    clipFilters;   /**< 各Actor的裁剪滤镜 */
    QList<vtkSmartPointer<vtkShrinkFilter>>   shrinkFilters; /**< 各Actor的收缩滤镜 */
    QList<bool>                               clipState;     /**< 各Actor裁剪状态 */
    QList<bool>                               shrinkState;   /**< 各Actor收缩状态 */

    /* ---- 线程安全命令队列 ---- */
    QMutex          mutex;         /**< 保护commandQueue的互斥锁 */
    QQueue<VRCmd>   commandQueue;  /**< 待处理命令队列（FIFO）*/

    /* ---- 渲染状态 ---- */
    bool            isRotating;    /**< 旋转动画是否激活 */
};

#endif // VRRENDERTHREAD_H
