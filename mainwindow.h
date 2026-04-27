#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ModelPartList.h"
#include "VRRenderThread.h"      /* 【新增】VR线程头文件 */

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
    void handleButton();
    void handleTreeClicked();
    void on_actionOpen_File_triggered();
    void handleOptionsButton();
    void on_actionItem_Options_triggered();
    void updateRender();
    void updateRenderFromTree(const QModelIndex& index);

    /** 启动VR渲染（槽函数，绑定到"Start VR"按钮）*/
    void handleStartVR();

    /** 停止VR渲染（槽函数，绑定到"Stop VR"按钮）*/
    void handleStopVR();

signals:
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** 遍历整个ModelPartList，为每个已加载STL的零件调用getNewActor()
     *  并将Actor添加到vrThread中，为启动VR做准备 */
    void populateVRActors();

    /** 递归辅助函数，遍历树结构收集VR Actor
     * @param index 当前遍历的树节点索引
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    Ui::MainWindow*                          ui;
    ModelPartList*                           partList;
    vtkSmartPointer<vtkRenderer>             renderer;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;

    /* 【新增】VR渲染线程，nullptr表示VR未运行 */
    VRRenderThread*                          vrThread;
};

#endif // MAINWINDOW_H
