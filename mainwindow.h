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

signals:
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** 遍历整棵树，为每个已加载STL的零件创建VR Actor并注册到vrThread
     *  同时填充 actorIndexMap（ModelPart指针 → actorIndex）
     */
    void populateVRActors();

    /** 递归辅助：遍历树节点，收集VR Actor
     * @param index 当前节点的QModelIndex
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    /** 查找零件在vrThread中注册的actorIndex
     *  @param part ModelPart指针
     *  @return actorIndex（未注册返回-1）
     */
    int getActorIndex(ModelPart* part) const;

    Ui::MainWindow*                               ui;
    ModelPartList*                                partList;
    vtkSmartPointer<vtkRenderer>                  renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow>  renderWindow;

    /** VR渲染线程实例，nullptr表示VR未运行 */
    VRRenderThread*                               vrThread;

    /** 旋转动画当前状态，用于切换按钮文字 */
    bool                                          isVRRotating;

    /** ModelPart指针 → VR actorIndex 映射表
     *  用于将GUI操作精确路由到对应的VR Actor
     */
    QMap<ModelPart*, int>                         actorIndexMap;
};

#endif // MAINWINDOW_H
