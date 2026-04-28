/**  @file ModelPart.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Template for model parts that will be added as treeview items
 *
 *   P Evans 2022
 */

#ifndef VIEWER_MODELPART_H
#define VIEWER_MODELPART_H

#include <QString>
#include <QList>
#include <QVariant>

#include <vtkSmartPointer.h>
#include <vtkMapper.h>
#include <vtkActor.h>
#include <vtkSTLReader.h>
#include <vtkColor.h>

/* 滤镜头文件 */
#include <vtkCutter.h>
#include <vtkPlane.h>
#include <vtkAppendPolyData.h>
#include <vtkTubeFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkShrinkFilter.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>

class ModelPart {
public:
    /** 构造函数
     * @param data   节点属性列表（名称、可见性等）
     * @param parent 父节点指针
     */
    ModelPart(const QList<QVariant>& data, ModelPart* parent = nullptr);

    /** 析构函数，释放所有子节点 */
    ~ModelPart();

    /** 添加子节点
     * @param item 子节点指针（已用new分配）
     */
    void appendChild(ModelPart* item);

    /** 移除并删除指定行的子节点
     *
     * 从 m_childItems 中取出第 row 个子节点并 delete 之。
     * 调用方负责事先通知 QAbstractItemModel（beginRemoveRows/endRemoveRows）。
     *
     * @param row 要移除的子节点行号（0-based）
     */
    void removeChild(int row);

    /** 获取指定行的子节点
     * @param row 行索引
     * @return 子节点指针，越界返回nullptr
     */
    ModelPart* child(int row);

    /** 返回子节点数量 @return 子节点数量 */
    int childCount() const;

    /** 返回列数 @return 列数 */
    int columnCount() const;

    /** 返回指定列的数据
     * @param column 列索引
     * @return QVariant类型的数据
     */
    QVariant data(int column) const;

    /** 设置指定列的数据
     * @param column 列索引
     * @param value  要设置的值
     */
    void set(int column, const QVariant& value);

    /** 返回父节点指针 @return 父节点指针 */
    ModelPart* parentItem();

    /** 返回本节点相对于父节点的行索引 @return 行索引 */
    int row() const;

    /** 设置零件颜色（RGB 0-255）
     * 颜色直接写入共享Property，VR Actor自动同步。
     * @param R 红色分量
     * @param G 绿色分量
     * @param B 蓝色分量
     */
    void setColour(const unsigned char R, const unsigned char G, const unsigned char B);

    unsigned char getColourR(); /**< @return 红色分量 0-255 */
    unsigned char getColourG(); /**< @return 绿色分量 0-255 */
    unsigned char getColourB(); /**< @return 蓝色分量 0-255 */

    /** 设置可见性（仅更新GUI侧Actor；VR侧通过命令队列同步）
     * @param isVisible true表示可见
     */
    void setVisible(bool isVisible);

    /** 获取可见性 @return true表示可见 */
    bool visible();

    /** 加载STL文件，创建完整VTK pipeline（包含滤镜占位）
     * @param fileName STL文件完整路径
     */
    void loadSTL(QString fileName);

    /** 获取GUI侧Actor（用于QVtkOpenGLNativeWidget渲染）
     * @return GUI Actor智能指针，未加载STL时返回nullptr
     */
    vtkSmartPointer<vtkActor> getActor();

    /** 获取STL读取器指针（供VRRenderThread注册，实现过滤器同步）
     * @return STLReader裸指针，未加载STL时返回nullptr
     */
    vtkSTLReader* getReader() { return file.Get(); }

    /** 为VR创建独立的新Actor
     *
     *  为同一个STLReader创建新的Mapper和Actor，
     *  通过SetProperty共享属性，颜色修改自动同步到VR。
     *
     *  注意：此函数只创建actor并设置好初始pipeline；
     *  后续过滤器变化通过 VRRenderThread::issueCommand(CMD_APPLY_FILTER)
     *  通知VR线程调用 rebuildPipeline() 重建。
     *
     *  @return 新Actor裸指针（调用方负责生命周期），
     *          未加载STL时返回nullptr
     */
    vtkActor* getNewActor();

    /** 启用或禁用裁剪滤镜（GUI侧）
     *  @param enabled true启用，false禁用
     */
    void setClip(bool enabled);

    /** 启用或禁用收缩滤镜（GUI侧）
     *  @param enabled true启用，false禁用
     */
    void setShrink(bool enabled);

    /** @return 裁剪滤镜是否当前激活 */
    bool getClip()   { return isClipped; }

    /** @return 收缩滤镜是否当前激活 */
    bool getShrink() { return isShrunk; }

private:
    /** 根据当前滤镜状态重新连接GUI侧VTK pipeline
     *  路由：STLReader → [ClipFilter] → [ShrinkFilter] → Mapper
     */
    void updatePipeline();

    QList<ModelPart*>                m_childItems;   /**< 子节点列表 */
    QList<QVariant>                  m_itemData;     /**< 节点属性数据列表 */
    ModelPart*                       m_parentItem;   /**< 父节点指针 */

    bool                             isVisible;      /**< 可见性标志 */

    /* VTK pipeline对象 */
    vtkSmartPointer<vtkSTLReader>    file;           /**< STL文件读取器（GUI和VR共享数据源）*/
    vtkSmartPointer<vtkMapper>       mapper;         /**< GUI侧Mapper */
    vtkSmartPointer<vtkActor>        actor;          /**< GUI侧Actor */
    vtkColor3<unsigned char>         colour;         /**< 当前颜色（R,G,B 0-255）*/

    /* 滤镜对象（在loadSTL中创建，通过flag控制是否接入pipeline）*/

    /**
     * 多截面切片滤镜（替代原单平面 clip）
     *
     * 沿模型的主要轴方向（Y轴，对应赛车高度方向）等间距切 NUM_SLICES 个截面，
     * 每个截面用 vtkCutter 切出轮廓线，合并为一个 vtkAppendPolyData，
     * 再用 vtkTubeFilter 加粗，呈现工程图纸剖视截面效果。
     */
    static constexpr int NUM_SLICES = 12;  /**< 截面数量 */
    vtkSmartPointer<vtkAppendPolyData>   sliceAppend;    /**< 合并所有截面输出 */
    vtkSmartPointer<vtkTubeFilter>       sliceTube;      /**< 截面线加粗 */
    vtkSmartPointer<vtkCutter>           cutters[NUM_SLICES]; /**< 各截面切割器 */
    vtkSmartPointer<vtkPlane>            cutPlanes[NUM_SLICES]; /**< 各截面平面 */

    vtkSmartPointer<vtkClipDataSet>      clipFilter;     /**< 保留原clip（shrink组合用）*/
    vtkSmartPointer<vtkShrinkFilter>     shrinkFilter;   /**< 收缩滤镜 */

    bool isClipped = false;  /**< 裁剪滤镜是否激活 */
    bool isShrunk  = false;  /**< 收缩滤镜是否激活 */
};

#endif // VIEWER_MODELPART_H
