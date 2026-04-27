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
#include <vtkClipDataSet.h>
#include <vtkShrinkFilter.h>
#include <vtkPlane.h>
#include <vtkPolyDataMapper.h>
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
     * @param R 红色分量
     * @param G 绿色分量
     * @param B 蓝色分量
     */
    void setColour(const unsigned char R, const unsigned char G, const unsigned char B);

    unsigned char getColourR(); /**< @return 红色分量 0-255 */
    unsigned char getColourG(); /**< @return 绿色分量 0-255 */
    unsigned char getColourB(); /**< @return 蓝色分量 0-255 */

    /** 设置可见性
     * @param isVisible true表示可见
     */
    void setVisible(bool isVisible);

    /** 获取可见性 @return true表示可见 */
    bool visible();

    /** 加载STL文件，创建完整VTK pipeline（包含滤镜）
     * @param fileName STL文件完整路径
     */
    void loadSTL(QString fileName);

    /** 获取GUI侧Actor（用于QVtkOpenGLNativeWidget渲染）
     * @return GUI Actor智能指针
     */
    vtkSmartPointer<vtkActor> getActor();

    /** 为VR创建独立的新Actor
     *
     *  为同一个STLReader创建新的Mapper和Actor，
     *  并根据当前滤镜状态（isClipped/isShrunk）构建对应的VR pipeline。
     *  通过SetProperty共享属性，使颜色修改自动同步到VR。
     *
     *  注意：滤镜状态在VR Actor创建时快照，后续滤镜变化需通过
     *  VRRenderThread命令队列通知VR线程重建pipeline。
     *
     *  @return 新Actor裸指针（由VRRenderThread接管生命周期），
     *          未加载STL时返回nullptr
     */
    vtkActor* getNewActor();

    /** 启用或禁用裁剪滤镜
     *  在x=0处沿x轴法线方向裁剪几何体
     *  @param enabled true启用，false禁用
     */
    void setClip(bool enabled);

    /** 启用或禁用收缩滤镜
     *  将每个cell向其质心收缩固定比例
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
     *  滤镜flag为false时对应滤镜被跳过
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
    vtkSmartPointer<vtkClipDataSet>  clipFilter;     /**< 裁剪滤镜：在x=0处截断几何体 */
    vtkSmartPointer<vtkShrinkFilter> shrinkFilter;   /**< 收缩滤镜：将cell向质心收缩 */

    bool isClipped = false;  /**< 裁剪滤镜是否激活 */
    bool isShrunk  = false;  /**< 收缩滤镜是否激活 */
};

#endif // VIEWER_MODELPART_H
