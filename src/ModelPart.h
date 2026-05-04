/**  @file ModelPart.h
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   树视图节点类,每个节点代表一个已加载的STL零件。
 *   Tree view node class; each node represents one loaded STL part.
 *
 *   支持五种独立可切换的VTK滤镜:
 *   Supports five independently toggleable VTK filters:
 *     1. Clip      - 在模型X轴中心处截断几何体
 *                    Cuts geometry at the model's X-centre
 *     2. Shrink    - 将每个面向其质心收缩,产生间隙效果
 *                    Pulls each face toward its centroid, creating visible gaps
 *     3. Smooth    - Laplacian平滑,软化锐边
 *                    Laplacian smoothing, softens sharp edges
 *     4. Decimate  - 减少多边形数量(减少90%)
 *                    Reduces polygon count (by 90%)
 *     5. Elevation - 按Z高度映射彩虹色表
 *                    Maps Z height to a rainbow colour table
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

/* 滤镜头文件
 * Filter header files */
#include <vtkClipDataSet.h>
#include <vtkShrinkPolyData.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkDecimatePro.h>
#include <vtkElevationFilter.h>
#include <vtkPlane.h>
#include <vtkPolyDataMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkCleanPolyData.h>
#include <vtkGeometryFilter.h>

class ModelPart {
public:
    /** 构造函数
     *  Constructor
     * @param data   节点属性列表(名称、可见性、RGB颜色等)
     *               Node attribute list (name, visibility, RGB colour etc.)
     * @param parent 父节点指针
     *               Parent node pointer
     */
    ModelPart(const QList<QVariant>& data, ModelPart* parent = nullptr);

    /** 析构函数——递归释放所有子节点
     *  Destructor — recursively frees all child nodes */
    ~ModelPart();

    /** 添加子节点
     *  Add a child node.
     * @param item 子节点指针(必须已用new分配)
     *             Child pointer (must be heap-allocated)
     */
    void appendChild(ModelPart* item);

    /** 移除并删除指定行的子节点。
     *  Remove and delete the child at the given row.
     *  调用方负责在调用此函数前先调用beginRemoveRows/endRemoveRows。
     *  Caller is responsible for calling beginRemoveRows/endRemoveRows first.
     * @param row 要删除的子节点的行号(从0开始)
     *            Zero-based index of the child to remove
     */
    void removeChild(int row);

    /** @return 指定行的子节点,越界返回nullptr
     *          Child at row, or nullptr if out of range */
    ModelPart* child(int row);

    /** @return 子节点数量
     *          Number of child nodes */
    int childCount() const;

    /** @return 数据列数量
     *          Number of data columns */
    int columnCount() const;

    /** @return 指定列的数据(QVariant类型)
     *          Data for the given column as a QVariant */
    QVariant data(int column) const;

    /** 设置指定列的数据
     *  Set data for a column.
     * @param column 列索引
     * Column index
     * @param value  要设置的值
     * Value to set
     */
    void set(int column, const QVariant& value);

    /** @return 父节点指针
     *          Pointer to parent node */
    ModelPart* parentItem();

    /** @return 当前节点相对于父节点的行索引
     *          Row index of this node relative to its parent */
    int row() const;

    /** 设置RGB颜色(0-255)。颜色直接写入共享的vtkProperty,
     *  通过SetProperty共享属性的VR Actor会自动同步。
     *  Set RGB colour (0-255). Writes directly to the shared vtkProperty
     *  so VR actors sharing the property update automatically.
     * @param R 红色通道
     * Red channel
     * @param G 绿色通道
     * Green channel
     * @param B 蓝色通道
     * Blue channel
     */
    void setColour(const unsigned char R, const unsigned char G, const unsigned char B);

    unsigned char getColourR(); /**< @return 红色通道0-255
                                 * Red channel 0-255 */
    unsigned char getColourG(); /**< @return 绿色通道0-255
                                 * Green channel 0-255 */
    unsigned char getColourB(); /**< @return 蓝色通道0-255
                                 * Blue channel 0-255 */

    /** 设置可见性,直接更新GUI侧Actor。
     *  Set visibility. Updates the GUI actor directly.
     * @param isVisible true=显示
     * true to show, false to hide
     */
    void setVisible(bool isVisible);

    /** @return 当前是否可见
     *          True if part is currently visible */
    bool visible();

    /** 加载STL文件并构建完整的VTK管线。
     *  Load an STL file and build the full VTK pipeline.
     *  无论初始状态如何,五个滤镜对象都在此处创建;
     *  updatePipeline()负责根据当前标志连接活跃的滤镜。
     *  All five filter objects are created here regardless of their initial state;
     *  updatePipeline() wires only the active ones based on current flags.
     * @param fileName STL文件完整路径
     * Full path to the STL file
     */
    void loadSTL(QString fileName);

    /** @return GUI侧Actor,未调用loadSTL时返回nullptr
     *          GUI-side actor, or nullptr if loadSTL has not been called */
    vtkSmartPointer<vtkActor> getActor();

    /** @return STLReader裸指针(供VRRenderThread使用,无需重新读取文件)
     *          Raw pointer to the STLReader (for VRRenderThread, avoids re-reading file) */
    vtkSTLReader* getReader() { return file.Get(); }

    /** 为VR渲染创建一个独立的新Actor。
     *  Create a new independent actor for VR rendering.
     *  与GUI Actor共享vtkProperty,颜色修改自动同步到VR。
     *  Shares the vtkProperty with the GUI actor so colour changes
     *  are reflected in VR automatically without extra commands.
     * @return 新Actor裸指针(调用方负责生命周期),未加载STL返回nullptr
     *         New vtkActor* (caller owns lifetime), nullptr if not loaded
     */
    vtkActor* getNewActor();

    /* ---- 滤镜切换接口
     *      Filter toggle interface ---- */

    /** 启用或禁用裁剪滤镜(在模型X轴中心处沿-X法线裁剪)。
     *  Enable or disable the clip filter (cuts at the model's X-centre along -X normal).
     * @param enabled true=启用
     * true to apply
     */
    void setClip(bool enabled);

    /** 启用或禁用收缩滤镜(将每个面向其质心收缩)。
     *  Enable or disable the shrink filter (pulls each face toward its centroid).
     * @param enabled true=启用
     * true to apply
     */
    void setShrink(bool enabled);

    /** 启用或禁用平滑滤镜(Laplacian平滑,20次迭代)。
     *  Enable or disable the smooth filter (Laplacian smoothing, 20 iterations).
     *  注意:与Clip滤镜不兼容(类型不匹配),启用Smooth时自动禁用Clip。
     *  Note: incompatible with Clip filter (type mismatch); enabling Smooth disables Clip automatically.
     * @param enabled true=启用
     * true to apply
     */
    void setSmooth(bool enabled);

    /** 启用或禁用抽取滤镜(减少90%多边形数量)。
     *  Enable or disable the decimate filter (reduces polygon count by 90%).
     * @param enabled true=启用
     * true to apply
     */
    void setDecimate(bool enabled);

    /** 启用或禁用高度色彩滤镜(按Z高度映射彩虹色表)。
     *  Enable or disable the elevation filter (colours geometry by Z height).
     * @param enabled true=启用
     * true to apply
     */
    void setElevation(bool enabled);

    bool getClip()      { return isClipped;    } /**< @return 裁剪滤镜是否激活
                                                  * True if clip filter active */
    bool getShrink()    { return isShrunk;     } /**< @return 收缩滤镜是否激活
                                                  * True if shrink filter active */
    bool getSmooth()    { return isSmoothed;   } /**< @return 平滑滤镜是否激活
                                                  * True if smooth filter active */
    bool getDecimate()  { return isDecimated;  } /**< @return 抽取滤镜是否激活
                                                  * True if decimate filter active */
    bool getElevation() { return isElevated;   } /**< @return 高度色彩滤镜是否激活
                                                  * True if elevation filter active */

    /** 启用或禁用截面视图(创意功能)。
     *  Enable or disable the creative slice (cross-section) view.
     * @param enabled true=启用
     * true to apply
     */
    void setSlice(bool enabled) { isSliced = enabled; updatePipeline(); }
    bool getSlice() { return isSliced; } /**< @return 截面视图是否激活
                                          * True if slice view active */

private:
    /** 根据当前滤镜标志重新连接GUI侧VTK管线。
     *  Reconnect the GUI VTK pipeline based on current filter flags.
     *  活跃滤镜按以下顺序链接:
     *  Active filters are chained in order:
     *    STLReader -> [Clip] -> [Shrink] -> [Smooth] -> [Decimate] -> [Elevation] -> Mapper
     *  未激活的滤镜被直接跳过,前一级直接连接到下一级。
     *  Inactive filters are bypassed — the previous stage connects directly to the next.
     */
    void updatePipeline();

    QList<ModelPart*>   m_childItems;  /**< 子节点列表
                                        * Child node list */
    QList<QVariant>     m_itemData;    /**< 本节点的列数据
                                        * Column data for this node */
    ModelPart*          m_parentItem;  /**< 父节点指针
                                        * Parent node pointer */

    bool isVisible;  /**< 可见性标志
                      * Visibility flag */

    /* VTK管线对象
     * VTK pipeline objects */
    vtkSmartPointer<vtkSTLReader>           file;          /**< STL读取器(与VR共享数据源)
                                                            * STL reader (shared with VR) */
    vtkSmartPointer<vtkMapper>              mapper;        /**< GUI侧Mapper(实际为vtkDataSetMapper)
                                                            * GUI mapper (vtkDataSetMapper) */
    vtkSmartPointer<vtkActor>              actor;          /**< GUI侧Actor
                                                            * GUI actor */
    vtkColor3<unsigned char>               colour;         /**< 当前RGB颜色
                                                            * Current RGB colour */

    /* 滤镜对象——在loadSTL()中创建,通过标志控制是否接入管线
     * Filter objects — created in loadSTL(), toggled via flags */
    vtkSmartPointer<vtkClipDataSet>          clipFilter;     /**< 在模型X中心处截断几何体
                                                              * Cuts geometry at model X-centre */
    vtkSmartPointer<vtkPlane>                clipPlane;      /**< 裁剪平面,启用时原点更新为模型边界中心
                                                              * Clip plane; origin updated to model bounds centre on enable */
    vtkSmartPointer<vtkShrinkPolyData>       shrinkFilter;   /**< 将每个面向其质心收缩,产生可见间隙
                                                              * Pulls each face toward centroid, exposing gaps */
    vtkSmartPointer<vtkSmoothPolyDataFilter> smoothFilter;   /**< Laplacian平滑
                                                              * Laplacian smoothing */
    vtkSmartPointer<vtkCleanPolyData>        cleanFilter;    /**< 在抽取前合并重复点
                                                              * Merges duplicate points before decimation */
    vtkSmartPointer<vtkGeometryFilter>       geometryFilter; /**< 将UnstructuredGrid转换为PolyData(Decimate所需)
                                                              * Converts UnstructuredGrid to PolyData for Decimate */
    vtkSmartPointer<vtkDecimatePro>          decimateFilter; /**< 多边形数量减少
                                                              * Polygon count reduction */
    vtkSmartPointer<vtkElevationFilter>      elevationFilter;/**< Z高度色彩映射
                                                              * Z-height colour mapping */
    vtkSmartPointer<vtkLookupTable>          elevationLUT;   /**< 高度滤镜使用的色表
                                                              * Colour table for elevation filter */

    /* 滤镜状态标志
     * Filter state flags */
    bool isClipped   = false; /**< 裁剪滤镜激活
                               * Clip filter active */
    bool isShrunk    = false; /**< 收缩滤镜激活
                               * Shrink filter active */
    bool isSmoothed  = false; /**< 平滑滤镜激活
                               * Smooth filter active */
    bool isDecimated = false; /**< 抽取滤镜激活
                               * Decimate filter active */
    bool isElevated  = false; /**< 高度色彩滤镜激活
                               * Elevation filter active */
    bool isSliced    = false; /**< 创意截面视图激活
                               * Creative cross-section view active */
};

#endif /* 结束 VIEWER_MODELPART_H 包含保护
        * End VIEWER_MODELPART_H include guard */
