#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
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
    /** 原有槽函数 */
    void handleButton();
    void handleTreeClicked();
    void on_actionOpen_File_triggered();
    void handleOptionsButton();
    void on_actionItem_Options_triggered();
    void updateRender();
    void updateRenderFromTree(const QModelIndex& index);

    /** 【阶段一】启动VR渲染线程 */
    void handleStartVR();

    /** 【阶段一】停止VR渲染线程 */
    void handleStopVR();

signals:
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** 遍历整棵树，为每个已加载STL的零件创建VR独立Actor并加入vrThread */
    void populateVRActors();

    /** 递归辅助：遍历树节点收集VR Actor
     * @param index 当前节点的QModelIndex
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    Ui::MainWindow*                              ui;
    ModelPartList*                               partList;
    vtkSmartPointer<vtkRenderer>                 renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;

    /** VR渲染线程实例，nullptr表示VR未运行 */
    VRRenderThread*                              vrThread;
};

#endif // MAINWINDOW_H
