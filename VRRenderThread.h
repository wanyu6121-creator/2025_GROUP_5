/**  @file VRRenderThread.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程 - 支持VR模式和桌面预览模式
 *
 *   VR模式：连接HTC Vive头显时自动使用
 *   桌面模式：无头显时自动fallback，用普通窗口预览
 *
 *   创意功能：光照强度可通过 CMD_SET_LIGHT_INTENSITY 命令实时调整
 *   加分功能：Skybox背景、动态增删Actor（CMD_ADD_ACTOR / CMD_REMOVE_ACTOR）
 */

#ifndef VRRENDERTHREAD_H
#define VRRENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QList>

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
#include <vtkSmoothPolyDataFilter.h>
#include <vtkDecimatePro.h>
#include <vtkElevationFilter.h>
#include <vtkLookupTable.h>
#include <vtkCleanPolyData.h>
#include <vtkGeometryFilter.h>
#include <vtkPlane.h>
#include <vtkPropPicker.h>
#include <vtkCellPicker.h>

#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>

/* OpenVR SDK头文件（用于检测头显是否连接）*/
#include <openvr.h>

/**
 * @brief 跨线程命令枚举
 *
 * 通过 issueCommand(cmd, value, actorIndex) 从GUI线程发送到VR线程。
 * actorIndex = -1 表示全局操作。
 */
enum VRCommand {
    CMD_SET_COLOUR_R       = 1,
    CMD_SET_COLOUR_G       = 2,
    CMD_SET_COLOUR_B       = 3,
    CMD_SET_VISIBLE        = 4,
    CMD_START_ROTATE       = 5,
    CMD_STOP_ROTATE        = 6,
    CMD_RESET_VIEW         = 7,
    CMD_APPLY_FILTER       = 8,
    CMD_ADD_ACTOR          = 9,
    CMD_REMOVE_ACTOR       = 10,
    CMD_SET_LIGHT_INTENSITY= 11,
    /* 【B方案】VR内手柄/鼠标射线拾取属性修改 */
    CMD_VR_SELECT_ACTOR    = 12, /**< 选中 actor（高亮显示）*/
    CMD_VR_DESELECT        = 13, /**< 取消选中 */
    CMD_VR_TOGGLE_VISIBLE  = 14, /**< 切换选中 actor 可见性 */
    CMD_VR_TOGGLE_SLICE    = 15, /**< 切换选中 actor 截面滤镜 */
    CMD_VR_TOGGLE_SHRINK   = 16, /**< 切换选中 actor 收缩滤镜 */
    CMD_VR_SET_COLOUR      = 17, /**< 循环切换选中 actor 颜色 */
};

/**
 * @brief 过滤器类型常量（用于CMD_APPLY_FILTER的value编码）
 *
 * value = filterType * 10 + (enabled ? 1 : 0)
 */
static const int FILTER_CLIP      = 0;  /**< clip filter — cuts geometry at x=0 */
static const int FILTER_SHRINK    = 1;  /**< shrink filter — pulls cells toward centroid */
static const int FILTER_SMOOTH    = 2;  /**< smooth filter — Laplacian smoothing */
static const int FILTER_DECIMATE  = 3;  /**< decimate filter — 50% polygon reduction */
static const int FILTER_ELEVATION = 4;  /**< elevation filter — Z-height rainbow colouring */

/**
 * @brief VR命令结构体，携带命令类型、参数值和目标Actor索引
 */
struct VRCmd {
    int    cmd;        /**< 命令类型，见VRCommand枚举 */
    double value;      /**< 参数值 */
    int    actorIndex; /**< 目标Actor索引，-1表示全局操作 */

    /** @param c 命令  @param v 参数  @param idx Actor索引（默认-1=全局）*/
    VRCmd(int c, double v, int idx = -1)
        : cmd(c), value(v), actorIndex(idx) {}
};

/**
 * @brief 动态添加Actor时携带的完整数据包
 *
 * 用于 CMD_ADD_ACTOR 命令，包含Actor本体及其pipeline所需的全部组件。
 * 由 MainWindow 在GUI线程中准备，通过线程安全队列传递给VR线程。
 */
struct ActorPackage {
    vtkActor*                          actor;        /**< 新Actor裸指针 */
    vtkSmartPointer<vtkSTLReader>      reader;       /**< STL读取器 */
    bool                               clipOn;       /**< 初始裁剪状态 */
    bool                               shrinkOn;     /**< 初始收缩状态 */
};

/**
 * @brief VR渲染线程类
 *
 * 在独立QThread中运行渲染循环，自动检测VR头显并选择模式：
 *   - 检测到头显 → vtkOpenVRRenderWindow（HTC Vive）
 *   - 未检测到  → 普通 vtkRenderWindow 桌面预览
 *
 * 特色功能：
 *   - Skybox 天空盒背景（VR/桌面模式均支持）
 *   - 光照强度实时控制（CMD_SET_LIGHT_INTENSITY）
 *   - 动态增删Actor（CMD_ADD_ACTOR / CMD_REMOVE_ACTOR）
 *   - 每帧消费命令队列，所有属性变化无需重启VR
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
     * @brief 析构函数，自动停止线程并释放Actor
     */
    ~VRRenderThread() override;

    /**
     * @brief 【B方案】为 actor 注册名称（用于选中时显示）
     * @param index actor索引
     * @param name  零件名称
     */
    void setActorName(int index, const QString& name);

Q_SIGNALS:
    /**
     * @brief 【B方案】VR内选中/取消选中零件时发出，通知GUI更新状态栏
     * @param actorIndex 被选中的索引，-1 = 取消选中
     * @param partName   零件名称
     */
    void vrActorSelected(int actorIndex, const QString& partName);

public:
    /**
     * @brief 在线程启动前批量添加Actor（仅在start()之前调用）
     */
    int addActorOffline(vtkActor* actor,
                        vtkSTLReader* reader = nullptr,
                        bool clipOn = false,
                        bool shrinkOn = false);

    /**
     * @brief 清空Actor列表（VR重启前调用，不Delete Actor）
     */
    void clearActors();

    /**
     * @brief 向VR线程发送命令（线程安全，可从任意线程调用）
     * @param cmd        命令类型，见VRCommand枚举
     * @param value      命令参数值
     * @param actorIndex 目标Actor索引（-1=全局）
     */
    void issueCommand(int cmd, double value, int actorIndex = -1);

    /**
     * @brief 动态添加Actor（VR运行时调用，线程安全）
     *
     * 将ActorPackage推入待添加队列，渲染循环下一帧处理。
     *
     * @param pkg 包含actor、reader和初始滤镜状态的数据包
     */
    void queueAddActor(const ActorPackage& pkg);

protected:
    /**
     * @brief 渲染主入口（QThread::run覆盖），自动选择VR或桌面模式
     */
    void run() override;

private:
    /** @brief VR模式渲染循环 */
    void runVRMode();

    /** @brief 桌面模式渲染循环 */
    void runDesktopMode();

    /**
     * @brief 处理单条命令（VR模式）
     * @param vcmd     命令结构体
     * @param renderer VR渲染器
     */
    void processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer);

    /**
     * @brief 处理单条命令（桌面模式）
     * @param vcmd     命令结构体
     * @param renderer 桌面渲染器
     */
    void processCommandDesktop(const VRCmd& vcmd, vtkRenderer* renderer);

    /**
     * @brief 处理待添加Actor队列，将新Actor注册到renderer（VR模式）
     *
     * 在渲染循环每帧调用，消费 pendingActors 队列。
     * 新Actor同时注册到 actorList / readerList / clipFilters 等列表。
     *
     * @param renderer 当前VR渲染器
     */
    void processPendingActorsVR(vtkOpenVRRenderer* renderer);

    /**
     * @brief 处理待添加Actor队列（桌面模式）
     * @param renderer 桌面渲染器
     */
    void processPendingActorsDesktop(vtkRenderer* renderer);

    /**
     * @brief 根据当前滤镜状态重建指定Actor的VTK pipeline
     *
     * pipeline路由：STLReader → [ClipFilter] → [ShrinkFilter] → Mapper
     *
     * @param idx Actor在actorList中的索引
     */
    void rebuildPipeline(int idx);

    /**
     * @brief 初始化VR场景天空盒（Skybox）
     *
     * 使用纯色环境贴图作为背景，提供immersive深蓝空间感。
     * 若找不到贴图文件则静默退出，不影响其他功能。
     *
     * @param renderer VR渲染器
     * @param renderWindow VR渲染窗口
     */
    void setupSkybox(vtkOpenVRRenderer* renderer,
                     vtkOpenVRRenderWindow* renderWindow);

    /**
     * @brief 初始化桌面场景天空盒（与VR模式相同效果）
     * @param renderer 桌面渲染器
     * @param renderWindow 桌面渲染窗口
     */
    void setupSkyboxDesktop(vtkRenderer* renderer,
                             vtkRenderWindow* renderWindow);

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

    /* ---- Actor列表及对应pipeline组件（按索引一一对应）---- */
    QList<vtkActor*>                          actorList;
    QList<vtkSmartPointer<vtkSTLReader>>      readerList;
    QList<vtkSmartPointer<vtkDataSetMapper>>  mapperList;
    QList<vtkSmartPointer<vtkClipDataSet>>    clipFilters;
    QList<vtkSmartPointer<vtkShrinkFilter>>   shrinkFilters;
    QList<vtkSmartPointer<vtkSmoothPolyDataFilter>> smoothFilters;  /**< Laplacian smooth */
    QList<vtkSmartPointer<vtkDecimatePro>>    decimateFilters;      /**< polygon reduction */
    QList<vtkSmartPointer<vtkElevationFilter>> elevationFilters;    /**< Z-height colour */
    QList<vtkSmartPointer<vtkLookupTable>>    elevationLUTs;        /**< colour tables */
    QList<vtkSmartPointer<vtkCleanPolyData>>  cleanFilters;         /**< pre-decimate clean */
    QList<vtkSmartPointer<vtkGeometryFilter>> geometryFilters;      /**< UG→PolyData conv */
    QList<bool>                               clipState;
    QList<bool>                               shrinkState;
    QList<bool>                               smoothState;    /**< smooth filter active */
    QList<bool>                               decimateState;  /**< decimate filter active */
    QList<bool>                               elevationState; /**< elevation filter active */

    /* ---- 【B方案】VR内选中状态 ---- */
    int              selectedActorIndex;  /**< 当前选中的 actor 索引，-1=无 */
    QList<QString>   actorNames;          /**< 各 actor 对应零件名称 */
    double           savedColor[3];       /**< 选中前的原始颜色（用于取消高亮恢复）*/
    double           rotationAngle;       /**< 旋转动画累计角度（度）*/

    /* 初始相机参数（Reset View 时恢复）*/
    double           initCamPos[3];       /**< 初始相机位置 */
    double           initCamFocal[3];     /**< 初始焦点 */
    double           initCamUp[3];        /**< 初始 ViewUp 向量 */
    bool             initCamSaved;        /**< 是否已保存初始相机 */

    /* 颜色循环表（CMD_VR_SET_COLOUR 依次切换）*/
    static const int  COLOR_COUNT = 6;
    static const int  colorTable[COLOR_COUNT][3]; /**< RGB 0-255 */
    QList<int>        actorColorIdx;       /**< 各 actor 当前在 colorTable 的索引 */

    /** 对指定 actor 应用/取消选中高亮（亮黄色外框）*/
    void highlightActor(int idx, bool on);

    /** 鼠标点击坐标 → actor 索引，-1 = 未命中 */
    int pickActorAt(int x, int y, vtkRenderer* renderer);

    /* ---- 动态添加队列（线程安全）---- */
    QMutex                  pendingMutex;    /**< 保护pendingActors的互斥锁 */
    QQueue<ActorPackage>    pendingActors;   /**< 待添加Actor队列 */

    /* ---- 线程安全命令队列 ---- */
    QMutex          mutex;         /**< 保护commandQueue的互斥锁 */
    QQueue<VRCmd>   commandQueue;  /**< 待处理命令队列（FIFO）*/

    /* ---- 场景状态 ---- */
    bool            isRotating;         /**< 旋转动画是否激活 */
    double          mainLightIntensity; /**< 主光源当前强度（0.0~2.0，默认0.8）*/

    /* ---- 主光源指针（运行时保存，供强度调整命令使用）---- */
    vtkSmartPointer<vtkLight> mainLight; /**< 主光源，由setupLighting创建后保存 */
};

#endif // VRRENDERTHREAD_H
