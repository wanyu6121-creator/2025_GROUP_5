#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include "ModelPartList.h"
#include "VRRenderThread.h"

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkGenericOpenGLRenderWindow.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

public slots:
    /* ---- 原有槽函数 ---- */
    void handleButton();
    void handleTreeClicked();
    void on_actionOpen_File_triggered();

    /**
     * @brief 【加分功能A-1】批量目录加载
     *
     * 打开目录选择对话框，用 QDirIterator 递归遍历所有 .stl 文件，
     * 目录结构映射为树状父子节点，文件名作为叶节点名称。
     */
    void on_actionOpen_Directory_triggered();
    void handleOptionsButton();
    void on_actionItem_Options_triggered();
    void updateRender();
    void updateRenderFromTree(const QModelIndex& index);

    /** 裁剪滤镜复选框切换
     * @param checked true表示勾选
     */
    void handleClipToggle(bool checked);

    /** 收缩滤镜复选框切换
     * @param checked true表示勾选
     */
    void handleShrinkToggle(bool checked);

    /* ---- VR控制槽函数 ---- */
    /** 启动VR渲染线程（自动遍历树并注册所有Actor）*/
    void handleStartVR();

    /** 停止VR渲染线程 */
    void handleStopVR();

    /** 切换VR场景旋转动画开/关 */
    void handleToggleRotate();

    /** 重置VR相机视角 */
    void handleResetView();

    /**
     * @brief 【创意功能】光照强度滑块变化槽
     *
     * 将滑块值（0~100）映射为光照强度（0.0~2.0），
     * 通过 CMD_SET_LIGHT_INTENSITY 命令实时同步到VR线程。
     *
     * @param value 滑块当前值（0~100）
     */
    void handleLightIntensityChanged(int value);

    /**
     * @brief 【加分功能】删除选中的树节点
     *
     * 从 ModelPartList 移除节点，同步从GUI渲染器移除Actor，
     * 并通过 CMD_REMOVE_ACTOR 命令通知VR线程移除对应Actor。
     */
    void handleDeleteNode();

signals:
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** 遍历整棵树，为每个已加载STL的零件创建VR Actor并注册 */
    void populateVRActors();

    /** 递归辅助：遍历树节点注册VR Actor
     * @param index 当前节点的QModelIndex
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    /** 查找零件在vrThread中的actorIndex
     * @param part ModelPart指针
     * @return actorIndex，未注册返回-1
     */
    int getActorIndex(ModelPart* part) const;

    Ui::MainWindow*                               ui;
    ModelPartList*                                partList;
    vtkSmartPointer<vtkRenderer>                  renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow>  renderWindow;

    /** VR渲染线程实例，nullptr表示VR未运行 */
    VRRenderThread*                               vrThread;

    /** 旋转动画当前状态 */
    bool                                          isVRRotating;

    /** ModelPart指针 → VR actorIndex 映射表 */
    QMap<ModelPart*, int>                         actorIndexMap;
};

#endif // MAINWINDOW_H
