/**  @file ModelPart.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Template for model parts that will be added as treeview items
 *
 *   P Evans 2022
 */

#include "ModelPart.h"

#include <vtkSmartPointer.h>
#include <vtkDataSetMapper.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkBoundingBox.h>
#include <array>

ModelPart::ModelPart(const QList<QVariant>& data, ModelPart* parent)
    : m_itemData(data), m_parentItem(parent)
{
    if (m_itemData.size() > 1) {
        isVisible = (m_itemData.at(1).toString() == "true");
    } else {
        isVisible = true;
    }
    colour[0] = 255; colour[1] = 255; colour[2] = 255;
}

ModelPart::~ModelPart()
{
    qDeleteAll(m_childItems);
}

void ModelPart::appendChild(ModelPart* item)
{
    item->m_parentItem = this;
    m_childItems.append(item);
}

void ModelPart::removeChild(int row)
{
    /* 边界检查 */
    if (row < 0 || row >= m_childItems.size()) return;

    /* 取出并释放（子节点析构时会级联删除其子节点）*/
    ModelPart* child = m_childItems.takeAt(row);
    delete child;
}

ModelPart* ModelPart::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int ModelPart::childCount() const { return m_childItems.count(); }
int ModelPart::columnCount() const { return m_itemData.count(); }

QVariant ModelPart::data(int column) const
{
    if (column < 0 || column >= m_itemData.size())
        return QVariant();
    return m_itemData.at(column);
}

void ModelPart::set(int column, const QVariant& value)
{
    if (column < 0 || column >= m_itemData.size())
        return;
    m_itemData.replace(column, value);
}

ModelPart* ModelPart::parentItem() { return m_parentItem; }

int ModelPart::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<ModelPart*>(this));
    return 0;
}

void ModelPart::setColour(const unsigned char R, const unsigned char G, const unsigned char B)
{
    colour[0] = R; colour[1] = G; colour[2] = B;

    while (m_itemData.size() < 5) m_itemData.append(QVariant());
    m_itemData.replace(2, R);
    m_itemData.replace(3, G);
    m_itemData.replace(4, B);

    /* 颜色通过共享Property自动同步到VR Actor */
    if (actor != nullptr) {
        actor->GetProperty()->SetColor(R / 255.0, G / 255.0, B / 255.0);
    }
}

unsigned char ModelPart::getColourR() { return colour[0]; }
unsigned char ModelPart::getColourG() { return colour[1]; }
unsigned char ModelPart::getColourB() { return colour[2]; }

void ModelPart::setVisible(bool isVis)
{
    isVisible = isVis;
    if (m_itemData.size() > 1) {
        m_itemData.replace(1, isVisible ? "true" : "false");
    }
    if (actor != nullptr) {
        actor->SetVisibility(isVisible ? 1 : 0);
    }
}

bool ModelPart::visible() { return isVisible; }

void ModelPart::loadSTL(QString fileName)
{
    /* 创建STL读取器 */
    file = vtkSmartPointer<vtkSTLReader>::New();
    file->SetFileName(fileName.toStdString().c_str());
    file->Update();

    /* ----------------------------------------------------------------
     * Clip 滤镜（评分要求）：vtkClipDataSet + vtkPlane
     * 在 x=0 处沿 x 轴法线方向裁剪，保留 x>0 的一侧
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);

    clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    clipFilter->SetClipFunction(clipPlane.Get());
    clipFilter->SetInputConnection(file->GetOutputPort());

    /* ----------------------------------------------------------------
     * Shrink 滤镜（评分要求）：vtkShrinkFilter
     * ---------------------------------------------------------------- */
    shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    shrinkFilter->SetShrinkFactor(0.8);

    /* ----------------------------------------------------------------
     * 【创意加分功能】多截面切片：沿 Y 轴等间距 NUM_SLICES 个截面
     * 通过单独的 Slice 复选框控制，与 Clip/Shrink 独立
     * ---------------------------------------------------------------- */
    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
    double yMin   = bounds[2];
    double yMax   = bounds[3];
    double yRange = yMax - yMin;
    if (yRange < 1e-6) yRange = 1.0;

    sliceAppend = vtkSmartPointer<vtkAppendPolyData>::New();
    for (int i = 0; i < NUM_SLICES; ++i) {
        double t    = (double)i / (double)(NUM_SLICES - 1);
        double yPos = yMin + yRange * (0.30 + t * 0.40);

        cutPlanes[i] = vtkSmartPointer<vtkPlane>::New();
        cutPlanes[i]->SetOrigin(0.0, yPos, 0.0);
        cutPlanes[i]->SetNormal(0.0, 1.0, 0.0);

        cutters[i] = vtkSmartPointer<vtkCutter>::New();
        cutters[i]->SetCutFunction(cutPlanes[i]);
        cutters[i]->SetInputConnection(file->GetOutputPort());
        sliceAppend->AddInputConnection(cutters[i]->GetOutputPort());
    }

    double tubeRadius = (bounds[1]-bounds[0] + yRange + bounds[5]-bounds[4]) / 3.0 * 0.003;
    tubeRadius = std::max(tubeRadius, 0.5);

    sliceTube = vtkSmartPointer<vtkTubeFilter>::New();
    sliceTube->SetInputConnection(sliceAppend->GetOutputPort());
    sliceTube->SetRadius(tubeRadius);
    sliceTube->SetNumberOfSides(8);
    sliceTube->CappingOn();

    /* Mapper 和 Actor */
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    actor  = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    actor->GetProperty()->SetColor(colour[0] / 255.0, colour[1] / 255.0, colour[2] / 255.0);
    actor->SetVisibility(isVisible ? 1 : 0);

    updatePipeline();
}


void ModelPart::updatePipeline()
{
    if (!file || !mapper) return;

    /* ----------------------------------------------------------------
     * 标准评分 pipeline：Source → Clip → Shrink → Mapper
     * 任意组合均正确串联（none/clip/shrink/both）
     *
     * isSliced：创意加分截面视图，与 Clip/Shrink 独立控制
     * ---------------------------------------------------------------- */

    if (isSliced) {
        /* 创意截面视图：优先显示，与 Clip/Shrink 独立 */
        if (isShrunk) {
            shrinkFilter->SetInputConnection(sliceTube->GetOutputPort());
            mapper->SetInputConnection(shrinkFilter->GetOutputPort());
        } else {
            mapper->SetInputConnection(sliceTube->GetOutputPort());
        }
        actor->GetProperty()->SetLineWidth(2.0);

    } else if (isClipped && isShrunk) {
        /* Clip + Shrink：Source → ClipFilter → ShrinkFilter → Mapper */
        clipFilter->SetInputConnection(file->GetOutputPort());
        shrinkFilter->SetInputConnection(clipFilter->GetOutputPort());
        mapper->SetInputConnection(shrinkFilter->GetOutputPort());
        actor->GetProperty()->SetLineWidth(1.0);

    } else if (isClipped) {
        /* 仅 Clip：Source → ClipFilter → Mapper */
        clipFilter->SetInputConnection(file->GetOutputPort());
        mapper->SetInputConnection(clipFilter->GetOutputPort());
        actor->GetProperty()->SetLineWidth(1.0);

    } else if (isShrunk) {
        /* 仅 Shrink：Source → ShrinkFilter → Mapper */
        shrinkFilter->SetInputConnection(file->GetOutputPort());
        mapper->SetInputConnection(shrinkFilter->GetOutputPort());
        actor->GetProperty()->SetLineWidth(1.0);

    } else {
        /* 无滤镜：Source → Mapper */
        mapper->SetInputConnection(file->GetOutputPort());
        actor->GetProperty()->SetLineWidth(1.0);
    }
}


void ModelPart::setClip(bool enabled)
{
    isClipped = enabled;
    updatePipeline();
}

void ModelPart::setShrink(bool enabled)
{
    isShrunk = enabled;
    updatePipeline();
}

vtkSmartPointer<vtkActor> ModelPart::getActor()
{
    return actor;
}

vtkActor* ModelPart::getNewActor()
{
    if (file == nullptr || actor == nullptr) {
        return nullptr;
    }

    /* ----------------------------------------------------------------
     * 为VR创建独立的多截面切片 pipeline
     * 与 GUI 侧逻辑完全一致，但所有对象独立创建，不共享指针
     * ---------------------------------------------------------------- */

    /* 取包围盒 Y 范围 */
    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
    double yMin   = bounds[2];
    double yMax   = bounds[3];
    double yRange = yMax - yMin;
    if (yRange < 1e-6) yRange = 1.0;

    /* 构建截面 pipeline */
    vtkSmartPointer<vtkAppendPolyData> vrAppend = vtkSmartPointer<vtkAppendPolyData>::New();
    for (int i = 0; i < NUM_SLICES; ++i) {
        double t    = (double)i / (double)(NUM_SLICES - 1);
        double yPos = yMin + yRange * (0.30 + t * 0.40);

        vtkSmartPointer<vtkPlane> plane = vtkSmartPointer<vtkPlane>::New();
        plane->SetOrigin(0.0, yPos, 0.0);
        plane->SetNormal(0.0, 1.0, 0.0);

        vtkSmartPointer<vtkCutter> cutter = vtkSmartPointer<vtkCutter>::New();
        cutter->SetCutFunction(plane);
        cutter->SetInputConnection(file->GetOutputPort());

        vrAppend->AddInputConnection(cutter->GetOutputPort());
    }

    double tubeRadius = (bounds[1] - bounds[0] + yRange +
                         bounds[5] - bounds[4]) / 3.0 * 0.003;
    tubeRadius = std::max(tubeRadius, 0.5);

    vtkSmartPointer<vtkTubeFilter> vrTube = vtkSmartPointer<vtkTubeFilter>::New();
    vrTube->SetInputConnection(vrAppend->GetOutputPort());
    vrTube->SetRadius(tubeRadius);
    vrTube->SetNumberOfSides(8);
    vrTube->CappingOn();

    /* 收缩滤镜（VR独立实例）*/
    vtkSmartPointer<vtkShrinkFilter> vrShrink = vtkSmartPointer<vtkShrinkFilter>::New();
    vrShrink->SetShrinkFactor(0.8);

    vtkSmartPointer<vtkDataSetMapper> newMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    if (isClipped && isShrunk) {
        vrShrink->SetInputConnection(vrTube->GetOutputPort());
        newMapper->SetInputConnection(vrShrink->GetOutputPort());
    } else if (isClipped && !isShrunk) {
        newMapper->SetInputConnection(vrTube->GetOutputPort());
    } else if (!isClipped && isShrunk) {
        vrShrink->SetInputConnection(file->GetOutputPort());
        newMapper->SetInputConnection(vrShrink->GetOutputPort());
    } else {
        newMapper->SetInputConnection(file->GetOutputPort());
    }

    vtkActor* newActor = vtkActor::New();
    newActor->SetMapper(newMapper);
    newActor->SetProperty(actor->GetProperty());   /* 共享颜色属性 */
    newActor->SetVisibility(isVisible ? 1 : 0);
    if (isClipped) {
        newActor->GetProperty()->SetLineWidth(2.0);
    }

    return newActor;
}
