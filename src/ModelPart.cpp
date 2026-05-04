/**  @file ModelPart.cpp
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   树视图节点实现。每个节点拥有从vtkSTLReader经过最多五个链式滤镜
 *   到vtkDataSetMapper的完整VTK管线。
 *   Tree view node implementation. Each node owns a complete VTK pipeline
 *   from vtkSTLReader through up to five chained filters to vtkDataSetMapper.
 *
 *   P Evans 2022
 */

#include "ModelPart.h"

#include <vtkSmartPointer.h>
#include <vtkDataSetMapper.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkAlgorithmOutput.h>
#include <vtkCleanPolyData.h>
#include <vtkGeometryFilter.h>

/* ================================================================
 * 构造与析构
 * Constructor and Destructor
 * ================================================================ */

ModelPart::ModelPart(const QList<QVariant>& data, ModelPart* parent)
    : m_itemData(data), m_parentItem(parent)
{
    /* 从传入数据中提取初始可见性(第二列存储"true"/"false"字符串)
     * Extract initial visibility from the data (second column stores "true"/"false") */
    if (m_itemData.size() > 1)
        isVisible = (m_itemData.at(1).toString() == "true");
    else
        isVisible = true;

    /* 默认颜色为白色
     * Default colour is white */
    colour[0] = 255; colour[1] = 255; colour[2] = 255;
}

ModelPart::~ModelPart()
{
    /* qDeleteAll会递归调用每个子节点的析构函数,实现级联释放
     * qDeleteAll recursively calls each child's destructor for cascading cleanup */
    qDeleteAll(m_childItems);
}

/* ================================================================
 * 树结构操作
 * Tree structure operations
 * ================================================================ */

void ModelPart::appendChild(ModelPart* item)
{
    /* 设置子节点的父指针,确保树结构正确
     * Set the child's parent pointer to maintain correct tree structure */
    item->m_parentItem = this;
    m_childItems.append(item);
}

void ModelPart::removeChild(int row)
{
    /* 边界检查:防止越界访问
     * Bounds check: prevent out-of-range access */
    if (row < 0 || row >= m_childItems.size()) return;

    /* takeAt从列表中取出但不释放,然后手动delete
     * 子节点析构时会级联删除其所有子节点
     * takeAt removes from the list without deleting; delete then frees memory.
     * The child's destructor recursively frees all its own children. */
    ModelPart* child = m_childItems.takeAt(row);
    delete child;
}

ModelPart* ModelPart::child(int row)
{
    if (row < 0 || row >= m_childItems.size()) return nullptr;
    return m_childItems.at(row);
}

int ModelPart::childCount()  const { return m_childItems.count(); }
int ModelPart::columnCount() const { return m_itemData.count(); }

QVariant ModelPart::data(int column) const
{
    if (column < 0 || column >= m_itemData.size()) return QVariant();
    return m_itemData.at(column);
}

void ModelPart::set(int column, const QVariant& value)
{
    if (column < 0 || column >= m_itemData.size()) return;
    m_itemData.replace(column, value);
}

ModelPart* ModelPart::parentItem() { return m_parentItem; }

int ModelPart::row() const
{
    /* indexOf在父节点的子列表中查找自己的位置
     * indexOf searches for this node's position in the parent's child list */
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<ModelPart*>(this));
    return 0;
}

/* ================================================================
 * 颜色与可见性
 * Colour and visibility
 * ================================================================ */

void ModelPart::setColour(const unsigned char R, const unsigned char G, const unsigned char B)
{
    /* 保存颜色值到内部数组
     * Save colour values to internal array */
    colour[0] = R; colour[1] = G; colour[2] = B;

    /* 同步到QVariant数据列表(供对话框下次打开时读取)
     * Sync to QVariant data list (so the dialog pre-fills correctly next time) */
    while (m_itemData.size() < 5) m_itemData.append(QVariant());
    m_itemData.replace(2, R);
    m_itemData.replace(3, G);
    m_itemData.replace(4, B);

    /* 直接写入共享的vtkProperty,VR Actor(通过SetProperty共享)会自动同步
     * Write directly to the shared vtkProperty;
     * VR actors sharing it via SetProperty() update automatically */
    if (actor != nullptr)
        actor->GetProperty()->SetColor(R / 255.0, G / 255.0, B / 255.0);
}

unsigned char ModelPart::getColourR() { return colour[0]; }
unsigned char ModelPart::getColourG() { return colour[1]; }
unsigned char ModelPart::getColourB() { return colour[2]; }

void ModelPart::setVisible(bool isVis)
{
    isVisible = isVis;

    /* 同步到数据列表,确保TreeView的Visible列正确显示
     * Sync to data list so the TreeView Visible column displays correctly */
    if (m_itemData.size() > 1)
        m_itemData.replace(1, isVisible ? "true" : "false");

    /* 直接控制VTK Actor的可见性
     * Directly control the VTK actor's visibility */
    if (actor != nullptr)
        actor->SetVisibility(isVisible ? 1 : 0);
}

bool ModelPart::visible() { return isVisible; }

/* ================================================================
 * STL加载与VTK管线构建
 * STL loading and VTK pipeline construction
 * ================================================================ */

void ModelPart::loadSTL(QString fileName)
{
    /* ---- STL读取器
     *      STL reader ---- */
    file = vtkSmartPointer<vtkSTLReader>::New();
    file->SetFileName(fileName.toStdString().c_str());
    file->Update();  /* 立即读取文件,使GetBounds()可用
                      * Read the file immediately so GetBounds() is available */

    /* ---- 滤镜1:裁剪(Clip)----
     *      Filter 1: Clip
     * 裁剪平面的原点设为模型X轴中心,法线为-X方向。
     * 这样无论模型在世界坐标系中的位置如何,裁剪总是从中间切开。
     * The clip plane origin is set to the model's X-centre so the cut
     * always bisects the model regardless of its world position. */
    clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);   /* setClip()会在启用时更新此值
                                            * Updated in setClip() when enabled */
    clipPlane->SetNormal(-1.0, 0.0, 0.0);
    clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    clipFilter->SetClipFunction(clipPlane.Get());

    /* ---- 滤镜2:收缩(Shrink)----
     *      Filter 2: Shrink
     * vtkShrinkPolyData将每个多边形面向其质心收缩,在面之间产生可见间隙。
     * 系数0.6意味着每个面缩小到原始大小的60%。
     * vtkShrinkPolyData pulls each polygon face toward its centroid,
     * creating visible gaps between faces. Factor 0.6 = 60% of original size. */
    shrinkFilter = vtkSmartPointer<vtkShrinkPolyData>::New();
    shrinkFilter->SetShrinkFactor(0.6);

    /* ---- 滤镜3:平滑(Smooth)----
     *      Filter 3: Smooth
     * Laplacian平滑:20次迭代将每个顶点移向其邻居的平均位置,软化锐边。
     * Laplacian smoothing: 20 iterations move each vertex toward the
     * average of its neighbours, softening sharp edges. */
    smoothFilter = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    smoothFilter->SetNumberOfIterations(20);
    smoothFilter->SetRelaxationFactor(0.1);
    smoothFilter->FeatureEdgeSmoothingOff();  /* 保留特征边
                                               * Preserve feature edges */
    smoothFilter->BoundarySmoothingOn();       /* 平滑边界
                                                * Smooth boundary edges */

    /* ---- 滤镜4:抽取(Decimate)----
     *      Filter 4: Decimate
     * vtkDecimatePro严格要求输入为干净的vtkPolyData(无重复点)。
     * vtkGeometryFilter将ClipDataSet输出的UnstructuredGrid转换为PolyData。
     * vtkCleanPolyData合并重复顶点,这是vtkDecimatePro正确工作的硬性要求。
     * vtkDecimatePro strictly requires clean vtkPolyData with no duplicate points.
     * vtkGeometryFilter converts UnstructuredGrid (from ClipDataSet) to PolyData.
     * vtkCleanPolyData merges duplicate vertices — a hard requirement for vtkDecimatePro. */
    cleanFilter    = vtkSmartPointer<vtkCleanPolyData>::New();
    geometryFilter = vtkSmartPointer<vtkGeometryFilter>::New();
    decimateFilter = vtkSmartPointer<vtkDecimatePro>::New();
    decimateFilter->SetTargetReduction(0.9);  /* 减少90%的多边形
                                               * Reduce polygon count by 90% */
    decimateFilter->PreserveTopologyOn();      /* 防止产生孔洞
                                                * Prevent holes forming */

    /* ---- 滤镜5:高度色彩(Elevation)----
     *      Filter 5: Elevation
     * 将Z坐标映射到蓝色(低)→红色(高)的彩虹色表。
     * 边界从模型实际Z范围计算,确保颜色始终充分展开。
     * Maps Z coordinate to a blue (low) to red (high) rainbow colour table.
     * Bounds are calculated from the model's actual Z range so colours always span fully. */
    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
    double zMin = bounds[4], zMax = bounds[5];
    /* 防止平面模型(zMin==zMax)导致除零错误
     * Guard against flat models where zMin==zMax causing divide-by-zero */
    if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }

    elevationFilter = vtkSmartPointer<vtkElevationFilter>::New();
    elevationFilter->SetLowPoint(0.0, 0.0, zMin);
    elevationFilter->SetHighPoint(0.0, 0.0, zMax);

    /* 彩虹色表:蓝色(低)→红色(高)
     * Rainbow lookup table: blue (low) -> red (high) */
    elevationLUT = vtkSmartPointer<vtkLookupTable>::New();
    elevationLUT->SetNumberOfTableValues(256);
    elevationLUT->SetHueRange(0.667, 0.0);  /* 色相范围:蓝色(0.667)到红色(0.0)
                                             * Hue: blue (0.667) to red (0.0) */
    elevationLUT->Build();

    /* ---- Mapper和Actor----
     *      Mapper and Actor
     * 使用vtkDataSetMapper而非vtkPolyDataMapper,因为ClipDataSet输出
     * UnstructuredGrid类型,而vtkPolyDataMapper只接受PolyData。
     * Use vtkDataSetMapper instead of vtkPolyDataMapper because ClipDataSet
     * outputs UnstructuredGrid which vtkPolyDataMapper cannot handle. */
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    actor  = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    /* 应用初始颜色和可见性
     * Apply initial colour and visibility */
    actor->GetProperty()->SetColor(colour[0] / 255.0, colour[1] / 255.0, colour[2] / 255.0);
    actor->SetVisibility(isVisible ? 1 : 0);

    /* 根据初始标志状态(默认全部关闭)连接管线
     * Wire the pipeline based on initial flag states (all off by default) */
    updatePipeline();
}

/* ================================================================
 * 管线重建
 * Pipeline rewiring
 * ================================================================ */

void ModelPart::updatePipeline()
{
    /* 保护:管线对象必须已经存在才能连接
     * Guard: pipeline objects must exist before we can connect them */
    if (!file || !mapper) return;

    /* 从STL读取器的输出开始,逐步通过活跃的滤镜链。
     * 未激活的滤镜被跳过,前一级直接连接到后一级。
     * Start from the STL reader's output and pass through each active filter.
     * Inactive filters are skipped; the previous stage connects directly to the next. */
    vtkAlgorithmOutput* currentOutput = file->GetOutputPort();

    /* 裁剪滤镜:输出类型为UnstructuredGrid
     * Clip filter: output type is UnstructuredGrid */
    if (isClipped) {
        clipFilter->SetInputConnection(currentOutput);
        currentOutput = clipFilter->GetOutputPort();
    }

    /* 收缩滤镜:输入和输出均为PolyData
     * Shrink filter: input and output are both PolyData */
    if (isShrunk) {
        shrinkFilter->SetInputConnection(currentOutput);
        currentOutput = shrinkFilter->GetOutputPort();
    }

    /* 平滑滤镜:仅接受PolyData输入。
     * 如果之前有Clip滤镜,则输出为UnstructuredGrid,不能直接连接Smooth。
     * 这种组合已被setClip()和setSmooth()在设置时自动禁止。
     * Smooth filter: only accepts PolyData input.
     * If Clip is active before it, output is UnstructuredGrid which Smooth cannot accept.
     * This combination is automatically prevented by setClip() and setSmooth(). */
    if (isSmoothed) {
        smoothFilter->SetInputConnection(currentOutput);
        currentOutput = smoothFilter->GetOutputPort();
    }

    /* 抽取滤镜:需要经过GeometryFilter和CleanPolyData预处理。
     * GeometryFilter将任意VTK数据集类型转换为PolyData。
     * CleanPolyData合并重复点,这是DecimatePro的硬性要求。
     * Decimate filter: needs GeometryFilter and CleanPolyData preprocessing.
     * GeometryFilter converts any VTK dataset type to PolyData.
     * CleanPolyData merges duplicate points — a hard requirement for DecimatePro. */
    if (isDecimated) {
        geometryFilter->SetInputConnection(currentOutput);
        cleanFilter->SetInputConnection(geometryFilter->GetOutputPort());
        decimateFilter->SetInputConnection(cleanFilter->GetOutputPort());
        currentOutput = decimateFilter->GetOutputPort();
    }

    /* 高度色彩滤镜:激活时启用彩虹色表,关闭时恢复Actor本身的颜色
     * Elevation filter: enable rainbow LUT when active, restore actor colour when off */
    if (isElevated) {
        elevationFilter->SetInputConnection(currentOutput);
        currentOutput = elevationFilter->GetOutputPort();

        /* 应用彩虹色表到Mapper
         * Apply the rainbow lookup table to the mapper */
        vtkDataSetMapper* dsMapper = vtkDataSetMapper::SafeDownCast(mapper.Get());
        if (dsMapper) {
            dsMapper->SetLookupTable(elevationLUT);
            dsMapper->SetScalarRange(0.0, 1.0);
            dsMapper->ScalarVisibilityOn();
        }
    } else {
        /* 关闭标量着色,恢复vtkProperty中存储的Actor颜色
         * Disable scalar colouring so the actor's vtkProperty colour is used */
        vtkDataSetMapper* dsMapper = vtkDataSetMapper::SafeDownCast(mapper.Get());
        if (dsMapper) dsMapper->ScalarVisibilityOff();
    }

    /* 将最终输出连接到Mapper
     * Connect the final output to the mapper */
    mapper->SetInputConnection(currentOutput);
}

/* ================================================================
 * 滤镜设置函数
 * Filter setter functions
 * ================================================================ */

void ModelPart::setClip(bool enabled)
{
    isClipped = enabled;

    if (enabled && file && clipPlane) {
        /* 启用时将裁剪平面原点更新为模型X轴中心。
         * 这样裁剪始终从模型中间切开,无论模型在世界坐标系中的位置。
         * Update the clip plane origin to the model's X-centre when enabling.
         * This ensures the cut always bisects the model regardless of its world position. */
        double bounds[6];
        file->GetOutput()->GetBounds(bounds);
        double xCentre = (bounds[0] + bounds[1]) * 0.5;
        clipPlane->SetOrigin(xCentre, 0.0, 0.0);
    }

    /* Smooth滤镜要求PolyData输入,而Clip输出UnstructuredGrid,两者类型不兼容。
     * 启用Clip时自动禁用Smooth,避免管线类型错误。
     * Smooth requires PolyData input but Clip outputs UnstructuredGrid — type mismatch.
     * Disable Smooth automatically when Clip is enabled to prevent pipeline errors. */
    if (enabled && isSmoothed) {
        isSmoothed = false;
    }
    updatePipeline();
}

void ModelPart::setShrink(bool enabled)
{
    isShrunk = enabled;
    updatePipeline();
}

void ModelPart::setSmooth(bool enabled)
{
    /* Smooth与Clip不兼容(类型不匹配——见setClip注释)
     * 启用Smooth时自动禁用Clip。
     * Smooth is incompatible with Clip (type mismatch — see setClip comment).
     * Disable Clip automatically when Smooth is enabled. */
    if (enabled && isClipped) {
        isClipped = false;
    }
    isSmoothed = enabled;
    updatePipeline();
}

void ModelPart::setDecimate(bool enabled)
{
    isDecimated = enabled;
    updatePipeline();
}

void ModelPart::setElevation(bool enabled)
{
    isElevated = enabled;
    updatePipeline();
}

/* ================================================================
 * Actor获取
 * Actor accessors
 * ================================================================ */

vtkSmartPointer<vtkActor> ModelPart::getActor()
{
    return actor;
}

vtkActor* ModelPart::getNewActor()
{
    /* 如果STL尚未加载则返回nullptr
     * Return nullptr if the STL has not been loaded yet */
    if (file == nullptr || actor == nullptr) return nullptr;

    /* 为VR创建一个独立的管线。
     * 复用同一个STLReader作为数据源,避免重新读取文件。
     * Create an independent pipeline for VR.
     * Reuse the same STLReader as data source to avoid re-reading the file. */
    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
    double xCentre = (bounds[0] + bounds[1]) * 0.5;

    /* 为VR管线创建独立的滤镜对象
     * Create independent filter objects for the VR pipeline */
    vtkSmartPointer<vtkPlane> vrClipPlane = vtkSmartPointer<vtkPlane>::New();
    vrClipPlane->SetOrigin(xCentre, 0.0, 0.0);
    vrClipPlane->SetNormal(-1.0, 0.0, 0.0);
    vtkSmartPointer<vtkClipDataSet> vrClip = vtkSmartPointer<vtkClipDataSet>::New();
    vrClip->SetClipFunction(vrClipPlane.Get());

    vtkSmartPointer<vtkShrinkPolyData> vrShrink = vtkSmartPointer<vtkShrinkPolyData>::New();
    vrShrink->SetShrinkFactor(0.6);

    vtkSmartPointer<vtkSmoothPolyDataFilter> vrSmooth = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    vrSmooth->SetNumberOfIterations(20);
    vrSmooth->SetRelaxationFactor(0.1);

    vtkSmartPointer<vtkDecimatePro> vrDecimate = vtkSmartPointer<vtkDecimatePro>::New();
    vrDecimate->SetTargetReduction(0.5);
    vrDecimate->PreserveTopologyOn();

    double zMin = bounds[4], zMax = bounds[5];
    if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }
    vtkSmartPointer<vtkElevationFilter> vrElevation = vtkSmartPointer<vtkElevationFilter>::New();
    vrElevation->SetLowPoint(0.0, 0.0, zMin);
    vrElevation->SetHighPoint(0.0, 0.0, zMax);

    vtkSmartPointer<vtkLookupTable> vrLUT = vtkSmartPointer<vtkLookupTable>::New();
    vrLUT->SetNumberOfTableValues(256);
    vrLUT->SetHueRange(0.667, 0.0);
    vrLUT->Build();

    vtkSmartPointer<vtkDataSetMapper> newMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    /* 镜像当前GUI管线状态,构建与GUI完全相同的VR管线
     * Mirror the current GUI pipeline state to build an identical VR pipeline */
    vtkAlgorithmOutput* currentOutput = file->GetOutputPort();

    if (isClipped) {
        vrClip->SetInputConnection(currentOutput);
        currentOutput = vrClip->GetOutputPort();
    }
    if (isShrunk) {
        vrShrink->SetInputConnection(currentOutput);
        currentOutput = vrShrink->GetOutputPort();
    }
    if (isSmoothed) {
        vrSmooth->SetInputConnection(currentOutput);
        currentOutput = vrSmooth->GetOutputPort();
    }
    if (isDecimated) {
        vrDecimate->SetInputConnection(currentOutput);
        currentOutput = vrDecimate->GetOutputPort();
    }
    if (isElevated) {
        vrElevation->SetInputConnection(currentOutput);
        currentOutput = vrElevation->GetOutputPort();
        newMapper->SetLookupTable(vrLUT);
        newMapper->SetScalarRange(0.0, 1.0);
        newMapper->ScalarVisibilityOn();
    }

    newMapper->SetInputConnection(currentOutput);

    /* 创建新Actor并连接Mapper
     * Create new actor and connect the mapper */
    vtkActor* newActor = vtkActor::New();
    newActor->SetMapper(newMapper);

    /* 共享vtkProperty:GUI侧的颜色修改会自动反映到VR Actor
     * Share the vtkProperty so colour changes in GUI propagate to VR automatically */
    newActor->SetProperty(actor->GetProperty());
    newActor->SetVisibility(isVisible ? 1 : 0);

    return newActor;
}
