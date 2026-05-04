/**  @file VRRenderThread.h
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   VR渲染线程 - 支持VR模式和桌面预览模式。
 *   VR render thread - supports VR mode and desktop preview mode.
 *
 *   VR模式:连接HTC Vive头显时自动使用。
 *   VR mode: used automatically when an HTC Vive headset is connected.
 *   桌面模式:无头显时自动回退,用普通窗口预览。
 *   Desktop mode: falls back automatically to a normal preview window when no headset is found.
 *
 *   创意功能:光照强度可通过 CMD_SET_LIGHT_INTENSITY 命令实时调整。
 *   Creative feature: light intensity can be adjusted in real time using CMD_SET_LIGHT_INTENSITY.
 *   加分功能:Skybox背景,动态增删Actor。
 *   Bonus features: Skybox background and dynamic actor add/remove.
 */

#ifndef VRRENDERTHREAD_H
#define VRRENDERTHREAD_H

#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QList>
#include <QVector>
#include <array>

#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkLight.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkProperty.h>
#include <vtkSTLReader.h>
#include <vtkClipDataSet.h>
#include <vtkShrinkPolyData.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkDecimatePro.h>
#include <vtkElevationFilter.h>
#include <vtkLookupTable.h>
#include <vtkCleanPolyData.h>
#include <vtkGeometryFilter.h>
#include <vtkPlane.h>
#include <vtkPropPicker.h>
#include <vtkCellPicker.h>
#include <vtkPicker.h>

#include <vtkOpenVRRenderer.h>
#include <vtkOpenVRRenderWindow.h>
#include <vtkOpenVRRenderWindowInteractor.h>
#include <vtkOpenVRCamera.h>
#include <vtkEventData.h>

/* OpenVR SDK头文件(用于检测头显是否连接)。
 * OpenVR SDK header (used to detect whether a headset is connected). */
#include <openvr.h>

/**
 * @brief 跨线程命令枚举。
 * @brief Cross-thread command enum.
 *
 * 通过 issueCommand(cmd, value, actorIndex) 从GUI线程发送到VR线程。
 * Sent from the GUI thread to the VR thread via issueCommand(cmd, value, actorIndex).
 * actorIndex = -1 表示全局操作。
 * actorIndex = -1 means a global operation.
 */
enum VRCommand {
    CMD_SET_COLOUR_R       = 1,
    CMD_SET_COLOUR_G       = 2,
    CMD_SET_COLOUR_B       = 3,
    CMD_SET_VISIBLE        = 4,
    CMD_START_ROTATE       = 5,
    CMD_STOP_ROTATE        = 6,
    CMD_RESET_VIEW         = 7,  /**< 恢复到已保存快照(没有快照时使用出厂默认值)。
                                  * Restore to saved snapshot (or factory default if none). */
    CMD_APPLY_FILTER       = 8,
    CMD_ADD_ACTOR          = 9,
    CMD_REMOVE_ACTOR       = 10,
    CMD_SET_LIGHT_INTENSITY= 11,
    CMD_SET_VIEW           = 18, /**< 保存当前相机和Actor位置作为命名快照。
                                  * Save current camera and actor positions as a named snapshot. */
    /* 【B方案】VR内手柄和鼠标射线拾取属性修改。
     * Plan B: property editing through VR controller and mouse ray picking. */
    CMD_VR_SELECT_ACTOR    = 12, /**< 选中 actor(高亮显示)。
                                  * Select actor (highlight it). */
    CMD_VR_DESELECT        = 13, /**< 取消选中。
                                  * Deselect actor. */
    CMD_VR_TOGGLE_VISIBLE  = 14, /**< 切换选中 actor 可见性。
                                  * Toggle selected actor visibility. */
    CMD_VR_TOGGLE_SLICE    = 15, /**< 切换选中 actor 截面滤镜。
                                  * Toggle selected actor slice filter. */
    CMD_VR_TOGGLE_SHRINK   = 16, /**< 切换选中 actor 收缩滤镜。
                                  * Toggle selected actor shrink filter. */
    CMD_VR_SET_COLOUR      = 17, /**< 循环切换选中 actor 颜色。
                                  * Cycle selected actor colour. */
};

/**
 * @brief 过滤器类型常量(用于CMD_APPLY_FILTER的value编码)。
 * @brief Filter type constants (for CMD_APPLY_FILTER value encoding).
 *
 * value = filterType * 10 + (enabled ? 1 : 0)
 * value = filterType * 10 + (enabled ? 1 : 0)
 */
static const int FILTER_CLIP      = 0;  /**< 裁剪滤镜 - 在 x=0 处截断几何体。
                                         * Clip filter - cuts geometry at x=0. */
static const int FILTER_SHRINK    = 1;  /**< 收缩滤镜 - 将单元拉向质心。
                                         * Shrink filter - pulls cells toward centroid. */
static const int FILTER_SMOOTH    = 2;  /**< 平滑滤镜 - Laplacian 平滑。
                                         * Smooth filter - Laplacian smoothing. */
static const int FILTER_DECIMATE  = 3;  /**< 抽取滤镜 - 减少 50% 多边形。
                                         * Decimate filter - 50% polygon reduction. */
static const int FILTER_ELEVATION = 4;  /**< 高度滤镜 - Z 高度彩虹着色。
                                         * Elevation filter - Z-height rainbow colouring. */
static const int FILTER_SLICE     = 5;  /**< 截面滤镜 - 使用裁剪平面显示截面。
                                         * Slice filter - uses a clip plane to show a cross-section. */

/**
 * @brief VR命令结构体,携带命令类型、参数值和目标Actor索引。
 * @brief VR command struct carrying command type, parameter value and target actor index.
 */
struct VRCmd {
    int    cmd;        /**< 命令类型,见VRCommand枚举。
                         * Command type; see VRCommand enum. */
    double value;      /**< 参数值。
                         * Parameter value. */
    int    actorIndex; /**< 目标Actor索引,-1表示全局操作。
                         * Target actor index; -1 means global operation. */

    /** @param c 命令
     *  @param c command
     *  @param v 参数
     *  @param v value
     *  @param idx Actor索引(默认-1=全局)
     *  @param idx actor index (default -1 = global)
     */
    VRCmd(int c, double v, int idx = -1)
        : cmd(c), value(v), actorIndex(idx) {}
};

/**
 * @brief 动态添加Actor时携带的完整数据包。
 * @brief Complete data package carried when dynamically adding an actor.
 *
 * 用于 CMD_ADD_ACTOR 命令,包含Actor本体及其pipeline所需的全部组件。
 * Used by CMD_ADD_ACTOR; contains the actor and all components required by its pipeline.
 * 由 MainWindow 在GUI线程中准备,通过线程安全队列传递给VR线程。
 * Prepared by MainWindow on the GUI thread and passed to the VR thread through a thread-safe queue.
 */
struct ActorPackage {
    vtkActor*                          actor;        /**< 新Actor裸指针。
                                                       * Raw pointer to the new actor. */
    vtkSmartPointer<vtkSTLReader>      reader;       /**< STL读取器。
                                                       * STL reader. */
    bool                               clipOn;       /**< 初始裁剪状态。
                                                       * Initial clip state. */
    bool                               shrinkOn;     /**< 初始收缩状态。
                                                       * Initial shrink state. */
    bool                               smoothOn;     /**< 初始平滑状态。
                                                       * Initial smooth state. */
    bool                               decimateOn;   /**< 初始抽取状态。
                                                       * Initial decimate state. */
    bool                               elevationOn;  /**< 初始高度色彩状态。
                                                       * Initial elevation state. */
    bool                               sliceOn;      /**< 初始截面状态。
                                                       * Initial slice state. */
};

/**
 * @brief VR渲染线程类。
 * @brief VR render thread class.
 *
 * 在独立QThread中运行渲染循环,自动检测VR头显并选择模式:
 * Runs the render loop in an independent QThread, auto-detecting the VR headset and mode:
 *   - 检测到头显 -> vtkOpenVRRenderWindow(HTC Vive)
 *     Headset detected -> vtkOpenVRRenderWindow (HTC Vive)
 *   - 未检测到 -> 普通 vtkRenderWindow 桌面预览
 *     No headset detected -> normal vtkRenderWindow desktop preview
 *
 * 特色功能:
 * Feature highlights:
 *   - Skybox 天空盒背景(VR/桌面模式均支持)
 *     Skybox background (supported in VR and desktop modes)
 *   - 光照强度实时控制(CMD_SET_LIGHT_INTENSITY)
 *     Real-time light intensity control (CMD_SET_LIGHT_INTENSITY)
 *   - 动态增删Actor
 *     Dynamic actor add/remove
 *   - 每帧消费命令队列,所有属性变化无需重启VR
 *     Consumes the command queue each frame so property changes do not require restarting VR
 */
class VRRenderThread : public QThread
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @brief Constructor
     * @param parent Qt父对象
     * @param parent Qt parent object
     */
    explicit VRRenderThread(QObject* parent = nullptr);

    /**
     * @brief 析构函数,自动停止线程并释放Actor
     * @brief Destructor; automatically stops the thread and frees actors
     */
    ~VRRenderThread() override;

    /**
     * @brief 【B方案】为 actor 注册名称(用于选中时显示)
     * @brief Plan B: register a name for an actor (for display when selected)
     * @param index actor索引
     * @param index actor index
     * @param name  零件名称
     * @param name  part name
     */
    void setActorName(int index, const QString& name);

Q_SIGNALS:
    /**
     * @brief 【B方案】VR内选中/取消选中零件时发出,通知GUI更新状态栏
     * @brief Plan B: emitted when a part is selected/deselected in VR to update the GUI status bar
     * @param actorIndex 被选中的索引,-1 = 取消选中
     * @param actorIndex selected index; -1 = deselected
     * @param partName   零件名称
     * @param partName   part name
     */
    void vrActorSelected(int actorIndex, const QString& partName);

public:
    /**
     * @brief 在线程启动前批量添加Actor(仅在start()之前调用)
     * @brief Batch-add actors before the thread starts (call only before start())
     */
    int addActorOffline(vtkActor* actor,
                        vtkSTLReader* reader = nullptr,
                        bool clipOn = false,
                        bool shrinkOn = false,
                        bool smoothOn = false,
                        bool decimateOn = false,
                        bool elevationOn = false,
                        bool sliceOn = false);

    /**
     * @brief 清空Actor列表(VR重启前调用,不Delete Actor)
     * @brief Clear the actor list before VR restart without deleting actors
     */
    void clearActors();

    /**
     * @brief 向VR线程发送命令(线程安全,可从任意线程调用)
     * @brief Send a command to the VR thread (thread-safe, callable from any thread)
     * @param cmd        命令类型,见VRCommand枚举
     * @param cmd        command type; see VRCommand enum
     * @param value      命令参数值
     * @param value      command parameter value
     * @param actorIndex 目标Actor索引(-1=全局)
     * @param actorIndex target actor index (-1 = global)
     */
    void issueCommand(int cmd, double value, int actorIndex = -1);

    /**
     * @brief 动态添加Actor(VR运行时调用,线程安全)
     * @brief Dynamically add an actor while VR is running (thread-safe)
     *
     * 将ActorPackage推入待添加队列,渲染循环下一帧处理。
     * Pushes ActorPackage into the pending-add queue; the render loop handles it next frame.
     *
     * @param pkg 包含actor、reader和初始滤镜状态的数据包
     * @param pkg data package containing actor, reader and initial filter states
     */
    void queueAddActor(const ActorPackage& pkg);

protected:
    /**
     * @brief 渲染主入口(QThread::run覆盖),自动选择VR或桌面模式
     * @brief Main render entry point (QThread::run override), automatically selects VR or desktop mode
     */
    void run() override;

private:
    /** @brief VR模式渲染循环
     *  @brief VR mode render loop */
    void runVRMode();

    /** @brief 桌面模式渲染循环
     *  @brief Desktop mode render loop */
    void runDesktopMode();

    /**
     * @brief Trigger输入触发Button3DEvent时调用。
     * @brief Called when Button3DEvent fires with Trigger input.
     * @param ed   包含世界位置、方向和动作的事件数据
     * @param ed   Event data containing world position, direction and action.
     * @param ren  用于三维射线拾取的渲染器
     * @param ren  Renderer used for 3-D ray picking.
     */
    void onVRTriggerPress(vtkEventDataDevice3D* ed, vtkOpenVRRenderer* ren);

    /**
     * @brief Trigger释放触发Button3DEvent时调用。
     * @brief Called when Button3DEvent fires with Trigger Release.
     */
    void onVRTriggerRelease();

    /**
     * @brief Move3DEvent时调用:根据当前和上一帧手柄位置差值平移被拖动的actor。
     * @brief Called on Move3DEvent: translates the dragged actor by the
     *        当前和上一帧手柄位置之间的增量。
     *        delta between the current and previous controller position.
     * @param ed  包含当前世界位置的事件数据
     * @param ed  Event data containing the current world position.
     */
    void onVRControllerMove(vtkEventDataDevice3D* ed);

    /**
     * @brief 处理单条命令(VR模式)
     * @brief Process a single command (VR mode)
     * @param vcmd     命令结构体
     * @param vcmd     command struct
     * @param renderer VR渲染器
     * @param renderer VR renderer
     */
    void processCommandVR(const VRCmd& vcmd, vtkOpenVRRenderer* renderer);

    /**
     * @brief 处理单条命令(桌面模式)
     * @brief Process a single command (desktop mode)
     * @param vcmd     命令结构体
     * @param vcmd     command struct
     * @param renderer 桌面渲染器
     * @param renderer desktop renderer
     */
    void processCommandDesktop(const VRCmd& vcmd, vtkRenderer* renderer);

    /**
     * @brief 处理待添加Actor队列,将新Actor注册到renderer(VR模式)
     * @brief Process the pending-actor queue and register new actors with the renderer (VR mode)
     *
     * 在渲染循环每帧调用,消费 pendingActors 队列。
     * Called every frame in the render loop to consume the pendingActors queue.
     * 新Actor同时注册到 actorList、readerList、clipFilters 等列表。
     * The new actor is also registered into actorList, readerList, clipFilters and related lists.
     *
     * @param renderer 当前VR渲染器
     * @param renderer current VR renderer
     */
    void processPendingActorsVR(vtkOpenVRRenderer* renderer);

    /**
     * @brief 处理待添加Actor队列(桌面模式)
     * @brief Process the pending-actor queue (desktop mode)
     * @param renderer 桌面渲染器
     * @param renderer desktop renderer
     */
    void processPendingActorsDesktop(vtkRenderer* renderer);

    /**
     * @brief 根据当前滤镜状态重建指定Actor的VTK pipeline
     * @brief Rebuild the specified actor's VTK pipeline based on current filter states
     *
     * pipeline路由:STLReader -> [ClipFilter] -> [ShrinkFilter] -> Mapper
     * Pipeline route: STLReader -> [ClipFilter] -> [ShrinkFilter] -> Mapper
     *
     * @param idx Actor在actorList中的索引
     * @param idx actor index in actorList
     */
    void rebuildPipeline(int idx);

    /**
     * @brief 初始化VR场景天空盒(Skybox)
     * @brief Initialise the VR scene skybox
     *
     * 使用纯色环境贴图作为背景,提供沉浸式深蓝空间感。
     * Uses a solid environment texture as the background, giving an immersive deep-blue space feel.
     * 若找不到贴图文件则静默退出,不影响其他功能。
     * If the texture file cannot be found, exits silently without affecting other features.
     *
     * @param renderer VR渲染器
     * @param renderer VR renderer
     * @param renderWindow VR渲染窗口
     * @param renderWindow VR render window
     */
    void setupSkybox(vtkOpenVRRenderer* renderer,
                     vtkOpenVRRenderWindow* renderWindow);

    /**
     * @brief 初始化桌面场景天空盒(与VR模式相同效果)
     * @brief Initialise the desktop scene skybox (same effect as VR mode)
     * @param renderer 桌面渲染器
     * @param renderer desktop renderer
     * @param renderWindow 桌面渲染窗口
     * @param renderWindow desktop render window
     */
    void setupSkyboxDesktop(vtkRenderer* renderer,
                             vtkRenderWindow* renderWindow);

    /**
     * @brief 初始化VR场景光照(主光+补光)
     * @brief Initialise VR scene lighting (key light + fill light)
     * @param renderer VR渲染器
     * @param renderer VR renderer
     */
    void setupLighting(vtkOpenVRRenderer* renderer);

    /**
     * @brief 初始化桌面场景光照
     * @brief Initialise desktop scene lighting
     * @param renderer 桌面渲染器
     * @param renderer desktop renderer
     */
    void setupLightingDesktop(vtkRenderer* renderer);

    /**
     * @brief 创建VR场景地板平面
     * @brief Create the VR scene floor plane
     * @param renderer VR渲染器
     * @param renderer VR renderer
     */
    void setupFloor(vtkOpenVRRenderer* renderer);

    /**
     * @brief 创建桌面场景地板平面
     * @brief Create the desktop scene floor plane
     * @param renderer 桌面渲染器
     * @param renderer desktop renderer
     */
    void setupFloorDesktop(vtkRenderer* renderer);

    /* ---- Actor列表及对应pipeline组件(按索引一一对应)----
     *      Actor lists and matching pipeline components (same index order) ---- */
    QList<vtkActor*>                          actorList;
    QList<vtkSmartPointer<vtkSTLReader>>      readerList;
    QList<vtkSmartPointer<vtkDataSetMapper>>  mapperList;
    QList<vtkSmartPointer<vtkClipDataSet>>    clipFilters;
    QList<vtkSmartPointer<vtkShrinkPolyData>> shrinkFilters;
    QList<vtkSmartPointer<vtkSmoothPolyDataFilter>> smoothFilters;  /**< Laplacian 平滑滤镜。
                                                                     * Laplacian smooth filter. */
    QList<vtkSmartPointer<vtkDecimatePro>>    decimateFilters;      /**< 多边形减少滤镜。
                                                                     * Polygon reduction filter. */
    QList<vtkSmartPointer<vtkElevationFilter>> elevationFilters;    /**< Z高度颜色滤镜。
                                                                     * Z-height colour filter. */
    QList<vtkSmartPointer<vtkLookupTable>>    elevationLUTs;        /**< 颜色表。
                                                                     * Colour tables. */
    QList<vtkSmartPointer<vtkCleanPolyData>>  cleanFilters;         /**< 抽取前清理滤镜。
                                                                     * Pre-decimate clean filters. */
    QList<vtkSmartPointer<vtkGeometryFilter>> geometryFilters;      /**< UnstructuredGrid 到 PolyData 的转换滤镜。
                                                                     * UnstructuredGrid-to-PolyData conversion filters. */
    QList<bool>                               clipState;
    QList<bool>                               shrinkState;
    QList<bool>                               smoothState;    /**< 平滑滤镜是否激活。
                                                               * Smooth filter active. */
    QList<bool>                               decimateState;  /**< 抽取滤镜是否激活。
                                                               * Decimate filter active. */
    QList<bool>                               elevationState; /**< 高度色彩滤镜是否激活。
                                                               * Elevation filter active. */
    QList<bool>                               sliceState;     /**< 截面滤镜是否激活。
                                                               * Slice filter active. */

    /* ---- 【B方案】VR内选中状态 ----
     *      Plan B: in-VR selection state ---- */
    int              selectedActorIndex;  /**< 当前选中的 actor 索引,-1=无。
                                           * Currently selected actor index, -1 means none. */
    QList<QString>   actorNames;          /**< 各 actor 对应零件名称。
                                           * Part name corresponding to each actor. */
    double           savedColor[3];       /**< 选中前的原始颜色(用于取消高亮恢复)。
                                           * Original colour before selection (used to restore after un-highlighting). */
    double           rotationAngle;       /**< 旋转动画累计角度(度)。
                                           * Accumulated rotation animation angle in degrees. */

    /* ---- 控制器拖动状态 ----
     *      Controller drag state ---- */
    bool             isDragging;       /**< trigger 按住某个 actor 时为 true。
                                        * true while trigger is held on an actor. */
    int              dragActorIndex;   /**< 正在拖动的 actor 索引,无则为 -1。
                                        * Index of the actor being dragged, -1 if none. */

    /* ---- 出厂初始状态(场景设置时保存一次)----
     *      Factory initial state (saved once at scene setup) ---- */
    double           initCamPos[3];       /**< 初始相机位置。
                                           * Initial camera position. */
    double           initCamFocal[3];     /**< 初始相机焦点。
                                           * Initial camera focal point. */
    double           initCamUp[3];        /**< 初始相机 ViewUp 向量。
                                           * Initial camera ViewUp vector. */
    bool             initCamSaved;        /**< 出厂状态保存后为 true。
                                           * True once factory state has been saved. */

    /**
     * @brief 场景设置时每个 actor 的世界空间位置。
     * @brief World-space position of each actor at scene-setup time.
     *
     * setupFloor() 将 actor 上移后,由 saveFactoryState() 填充。
     * Populated by saveFactoryState() after setupFloor() has shifted actors upward.
     * CMD_RESET_VIEW 将每个 actor 恢复到该位置,使拖动过的零件回到原位。
     * CMD_RESET_VIEW restores every actor to this position so dragged parts snap back to their original place.
     */
    QVector<std::array<double,3>> initActorPositions;

    /**
     * @brief 将当前相机和所有 actor 位置保存为出厂状态。
     * @brief Save the current camera and all actor positions as the factory state.
     * 场景设置后从 runVRMode()/runDesktopMode() 调用一次。
     * Called once from runVRMode()/runDesktopMode() after scene setup.
     * @param camera 场景设置时的活动相机
     * @param camera Active camera at scene-setup time
     */
    void saveFactoryState(vtkCamera* camera);

    /**
     * @brief 将所有 actor 恢复到出厂位置并重置旋转。
     * @brief Restore all actors to their factory positions and reset rotation.
     * 真实VR中不移动相机;桌面回退模式中也会重置相机。
     * The camera is NOT moved in real VR; in desktop fallback it is also reset.
     * @param camera         活动相机(仅桌面模式使用)
     * @param camera         Active camera (used only in desktop mode)
     * @param renderer       渲染器(ResetCameraClippingRange 所需)
     * @param renderer       Renderer (needed for ResetCameraClippingRange)
     * @param restoreCamera  桌面回退为 true,真实VR为 false
     * @param restoreCamera  True in desktop fallback, false in real VR
     */
    void resetModelView(vtkCamera* camera, vtkRenderer* renderer, bool restoreCamera);

    /**
     * @brief 将所有 actor 旋转到命名预设方向。
     * @brief Rotate all actors to a named preset orientation.
     *
     * @param index  0=正视图,1=顶视图,2=右视图,3=等轴视图
     * @param index  0=Front, 1=Top, 2=Right Side, 3=Isometric
     *               超出 [0,3] 的值会限制为正视图。
     *               Values outside [0,3] are clamped to Front.
     */
    void applyViewPreset(int index);

    /* 颜色循环表(CMD_VR_SET_COLOUR 依次切换)。
     * Colour cycle table (cycled by CMD_VR_SET_COLOUR). */
    static const int  COLOR_COUNT = 6;
    static const int  colorTable[COLOR_COUNT][3]; /**< RGB 取值范围 0-255。
                                                    * RGB values range from 0 to 255. */
    QList<int>        actorColorIdx;       /**< 各 actor 当前在 colorTable 的索引。
                                            * Current colorTable index for each actor. */

    /** 对指定 actor 应用/取消选中高亮(亮黄色外框)。
     *  Apply/remove selection highlight on a specified actor (bright yellow outline). */
    void highlightActor(int idx, bool on);

    /** 鼠标点击坐标 -> actor 索引,-1 = 未命中。
     *  Mouse click coordinates -> actor index, -1 = no hit. */
    int pickActorAt(int x, int y, vtkRenderer* renderer);

    /* ---- 动态添加队列(线程安全)----
     *      Dynamic add queue (thread-safe) ---- */
    QMutex                  pendingMutex;    /**< 保护 pendingActors 的互斥锁。
                                              * Mutex protecting pendingActors. */
    QQueue<ActorPackage>    pendingActors;   /**< 待添加Actor队列。
                                              * Pending actor queue. */

    /* ---- 线程安全命令队列 ----
     *      Thread-safe command queue ---- */
    QMutex          mutex;         /**< 保护 commandQueue 的互斥锁。
                                    * Mutex protecting commandQueue. */
    QQueue<VRCmd>   commandQueue;  /**< 待处理命令队列(FIFO)。
                                    * Pending command queue (FIFO). */

    /* ---- 场景状态 ----
     *      Scene state ---- */
    bool            isRotating;         /**< 旋转动画是否激活。
                                         * Whether rotation animation is active. */
    double          mainLightIntensity; /**< 主光源当前强度(0.0~2.0,默认0.8)。
                                         * Current main light intensity (0.0-2.0, default 0.8). */

    /* ---- 主光源指针(设置时保存以便调节强度)----
     *      Main light source pointer (saved at setup for intensity adjustment) ---- */
    vtkSmartPointer<vtkLight> mainLight; /**< 主光源,由 setupLighting 创建。
                                          * Main light, created by setupLighting. */

    /* ---- VR控制器拾取状态(由3D事件回调使用)----
     *      VR controller picking state (used by 3D event callbacks) ---- */
    vtkOpenVRRenderer*              vrPickRenderer; /**< 场景启动时设置,供回调使用。
                                                     * Set at scene startup for callbacks. */
    vtkSmartPointer<vtkPicker>      vrPicker;       /**< 三维射线拾取器。
                                                     * 3-D ray picker. */
    double                          vrDragLastPos[3]; /**< 上一帧控制器位置。
                                                       * Controller position last frame. */
};

#endif /* 结束 VRRENDERTHREAD_H 包含保护
        * End VRRENDERTHREAD_H include guard */
