/**  @file mainwindow.h
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   主窗口类。拥有VTK渲染器和ModelPartList树模型。
 *   Main window class. Owns the VTK renderer and ModelPartList tree model.
 *
 *   负责处理:文件加载、树交互、五个滤镜切换、VR控制、光照和节点删除。
 *   Handles: file loading, tree interaction, five filter toggles,
 *            VR control, lighting and node deletion.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include "ModelPartList.h"
#include "VRRenderThread.h"

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLight.h>
#include <QCheckBox>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** 构造函数——设置UI、连接信号/槽、初始化VTK渲染器。
     *  Constructor — sets up UI, connects signals/slots, initialises VTK renderer.
     * @param parent 父窗口部件,顶级窗口为nullptr
     *               Parent widget, nullptr for a top-level window
     */
    MainWindow(QWidget* parent = nullptr);

    /** 析构函数——若VR线程正在运行则停止它,释放UI。
     *  Destructor — stops VR thread if running, frees the UI. */
    ~MainWindow();

public slots:
    /* ---- 文件与树操作
     *      File and tree operations ---- */

    /** 添加条目按钮的槽函数(当前显示状态消息)。
     *  Slot for the Add Item button (currently shows a status message). */
    void handleButton();

    /** 用户点击树视图条目时触发。
     *  Fired when the user clicks a tree view item.
     *  更新状态栏并同步所有五个滤镜复选框以反映选中零件的当前状态。
     *  Updates the status bar and syncs all five filter checkboxes
     *  to reflect the selected part's current state. */
    void handleTreeClicked();

    /** 文件>打开文件——以选中节点为父节点打开单个STL文件。
     *  File > Open File — opens a single STL file as a child of the selected node. */
    void on_actionOpen_File_triggered();

    /** 文件>打开目录——递归加载目录中所有STL文件,
     *  将文件夹结构映射为树状父子节点。
     *  File > Open Directory — recursively loads all STL files from a directory,
     *  mapping the folder structure to tree parent/child nodes. */
    void on_actionOpen_Directory_triggered();

    /** 打开预填当前零件属性的选项对话框。
     *  Opens the OptionDialog pre-filled with the selected part's properties. */
    void handleOptionsButton();

    /** actionItem_Options右键菜单动作的转发槽。
     *  Relay slot for the actionItem_Options right-click action. */
    void on_actionItem_Options_triggered();

    /** 遍历完整的ModelPartList树并将所有Actor重新添加到渲染器。
     *  Traverses the full ModelPartList tree and re-adds all actors to the renderer. */
    void updateRender();

    /** updateRender()的递归辅助函数。
     *  Recursive helper for updateRender().
     * @param index 当前处理的树节点
     * Current tree node to process
     */
    void updateRenderFromTree(const QModelIndex& index);

    /* ---- 滤镜切换(GUI侧+同步到VR)
     *      Filter toggles (GUI + VR sync) ---- */

    /** 切换选中零件的裁剪滤镜。
     *  Toggle the clip filter on the selected part.
     *  若与Smooth同时激活则自动禁用Smooth(类型不匹配)。
     *  Disables Smooth automatically if both would be active (type mismatch).
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleClipToggle(bool checked);

    /** 切换选中零件的收缩滤镜。
     *  Toggle the shrink filter on the selected part.
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleShrinkToggle(bool checked);

    /** 切换选中零件的平滑滤镜(Laplacian,20次迭代)。
     *  Toggle the smooth filter (Laplacian, 20 iterations) on the selected part.
     *  若与Clip同时激活则自动禁用Clip(类型不匹配)。
     *  Disables Clip automatically if both would be active (type mismatch).
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleSmoothToggle(bool checked);

    /** 切换选中零件的抽取滤镜(减少90%多边形)。
     *  Toggle the decimate filter (90% polygon reduction) on the selected part.
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleDecimateToggle(bool checked);

    /** 切换选中零件的高度色彩滤镜(Z高度彩虹着色)。
     *  Toggle the elevation filter (Z-height rainbow colouring) on the selected part.
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleElevationToggle(bool checked);

    /** 切换截面视图(创意功能)。
     *  Toggle the slice (cross-section) view (creative feature).
     * @param checked 复选框是否被勾选
     * True if checkbox was just ticked
     */
    void handleSliceToggle(bool checked);

    /* ---- VR控制
     *      VR control ---- */

    /** 启动VR渲染线程,将所有已加载零件注册为VR Actor。
     *  Start the VR render thread, registering all loaded parts as VR actors. */
    void handleStartVR();

    /** 停止VR渲染线程。
     *  Stop the VR render thread. */
    void handleStopVR();

    /** 在VR中开始自动旋转动画。
     *  Start auto-rotation animation in VR. */
    void handleStartRotate();

    /** 在VR中停止自动旋转动画。
     *  Stop auto-rotation animation in VR. */
    void handleStopRotate();

    /** 将所有零件重置到原始位置和方向。
     *  Reset all parts to their original positions and orientation. */
    void handleResetView();

    /** 设置模型为正视图(俯仰=0,偏航=0)。
     *  Set model to Front view (pitch=0, yaw=0). */
    void handleViewFront();

    /** 设置模型为顶视图(俯仰=90,偏航=0)。
     *  Set model to Top view (pitch=90, yaw=0). */
    void handleViewTop();

    /** 设置模型为右视图(俯仰=0,偏航=-90)。
     *  Set model to Right Side view (pitch=0, yaw=-90). */
    void handleViewRight();

    /** 设置模型为等轴视图(俯仰=30,偏航=45)。
     *  Set model to Isometric view (pitch=30, yaw=45). */
    void handleViewIso();

    /** 光照强度滑块变化槽。
     *  Light intensity slider changed slot.
     *  将滑块值(0-100)映射为光照强度(0.0-2.0),同时更新GUI渲染器和VR线程。
     *  Maps slider value (0-100) to intensity (0.0-2.0),
     *  updating both the GUI renderer and the VR thread.
     * @param value 滑块当前值(0-100)
     * Slider value 0-100
     */
    void handleLightIntensityChanged(int value);

    /* ---- 节点管理
     *      Node management ---- */

    /** 删除选中的树节点,从GUI渲染器移除其Actor,
     *  并向VR线程发送CMD_REMOVE_ACTOR命令。
     *  Delete the selected tree node, remove its actor from the GUI renderer,
     *  and send CMD_REMOVE_ACTOR to the VR thread. */
    void handleDeleteNode();

signals:
    /** 发出此信号以在状态栏显示消息。
     *  Emitted to display a message in the status bar.
     * @param message 要显示的文本
     * Text to display
     * @param timeout 显示时长(毫秒),0表示持续显示
     * Duration in ms (0 = until next message)
     */
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** 遍历完整的树并将所有已加载零件注册为VR Actor。
     *  Traverse the full tree and register all loaded parts as VR actors. */
    void populateVRActors();

    /** populateVRActors()的递归辅助函数。
     *  Recursive helper for populateVRActors().
     * @param index 当前树节点
     * Current tree node
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    /** 查找零件的VR Actor索引。
     *  Look up a part's VR actor index.
     * @param part 要查找的ModelPart指针
     * ModelPart pointer to look up
     * @return Actor索引,未注册返回-1
     * Actor index, or -1 if not registered
     */
    int getActorIndex(ModelPart* part) const;

    Ui::MainWindow*                              ui;            /**< Qt生成的UI对象
                                                                 * Qt-generated UI object */
    ModelPartList*                               partList;      /**< 树模型
                                                                 * Tree model */
    vtkSmartPointer<vtkRenderer>                 renderer;      /**< GUI渲染器
                                                                 * GUI renderer */
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow; /**< Qt关联的渲染窗口
                                                                 * Qt-linked render window */

    VRRenderThread*  vrThread;      /**< VR渲染线程,未运行时为nullptr
                                     * VR render thread, nullptr when not running */
    bool             isVRRotating;  /**< 当前旋转动画状态
                                     * Current rotation animation state */

    QMap<ModelPart*, int> actorIndexMap; /**< ModelPart指针到VR Actor索引的映射
                                          * Maps ModelPart* to VR actor index */

    vtkSmartPointer<vtkLight> guiKeyLight;  /**< 主光源(受滑块控制)
                                             * Main GUI light (slider-controlled) */
    vtkSmartPointer<vtkLight> guiFillLight; /**< 补光(强度固定为主光的40%)
                                             * Fill light (fixed at 40% of key light) */
};

#endif /* 结束 MAINWINDOW_H 包含保护
        * End MAINWINDOW_H include guard */
