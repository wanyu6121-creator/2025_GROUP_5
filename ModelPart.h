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

class ModelPart {
public:
    /** 构造函数
     * @param data  该节点的属性列表（名称、可见性等）
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

    /** 返回子节点数量
     * @return 子节点数量
     */
    int childCount() const;

    /** 返回列数（属性数量）
     * @return 列数
     */
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

    /** 返回父节点指针
     * @return 父节点指针
     */
    ModelPart* parentItem();

    /** 返回本节点相对于父节点的行索引
     * @return 行索引
     */
    int row() const;

    /** 设置零件颜色（RGB分量，0-255）
     * @param R 红色分量
     * @param G 绿色分量
     * @param B 蓝色分量
     */
    void setColour(const unsigned char R, const unsigned char G, const unsigned char B);

    /** 返回红色分量 @return 0-255 */
    unsigned char getColourR();
    /** 返回绿色分量 @return 0-255 */
    unsigned char getColourG();
    /** 返回蓝色分量 @return 0-255 */
    unsigned char getColourB();

    /** 设置可见性
     * @param isVisible true表示可见
     */
    void setVisible(bool isVisible);

    /** 获取可见性
     * @return true表示可见
     */
    bool visible();

    /** 加载STL文件，创建GUI侧的mapper和actor
     * @param fileName STL文件完整路径
     */
    void loadSTL(QString fileName);

    /** 获取GUI侧的Actor（用于QVtkOpenGLNativeWidget渲染）
     * @return 智能指针指向GUI Actor
     */
    vtkSmartPointer<vtkActor> getActor();

    /** 为VR创建一个独立的新Actor
     *
     *  VR渲染器不能与GUI渲染器共用同一个Actor，
     *  此函数为同一个STLReader创建新的Mapper和Actor，
     *  并通过 SetProperty 共享属性（颜色、可见性等），
     *  使GUI的修改自动反映到VR中。
     *
     *  @return 新Actor的裸指针（由VRRenderThread接管生命周期）
     *          如果尚未加载STL文件则返回nullptr
     */
    vtkActor* getNewActor();

private:
    QList<ModelPart*>             m_childItems;   /**< 子节点列表 */
    QList<QVariant>               m_itemData;     /**< 节点属性数据列表 */
    ModelPart*                    m_parentItem;   /**< 父节点指针 */

    bool                          isVisible;      /**< 可见性标志 */

    vtkSmartPointer<vtkSTLReader> file;           /**< STL文件读取器（GUI和VR共享同一个数据源）*/
    vtkSmartPointer<vtkMapper>    mapper;         /**< GUI侧Mapper */
    vtkSmartPointer<vtkActor>     actor;          /**< GUI侧Actor */
    vtkColor3<unsigned char>      colour;         /**< 当前颜色（R,G,B 0-255）*/
};

#endif // VIEWER_MODELPART_H
