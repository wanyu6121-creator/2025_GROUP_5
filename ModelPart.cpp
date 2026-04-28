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
     * 创建裁剪滤镜
     * 在x=0处沿x轴法线方向裁剪，法线朝-x使正x侧保留
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);

    clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    clipFilter->SetClipFunction(clipPlane.Get());
    clipFilter->SetInputConnection(file->GetOutputPort());

    /* ----------------------------------------------------------------
     * 创建收缩滤镜，收缩因子0.8
     * ---------------------------------------------------------------- */
    shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    shrinkFilter->SetShrinkFactor(0.8);

    /* ----------------------------------------------------------------
     * 【修复问题3】使用 vtkDataSetMapper 替代 vtkPolyDataMapper
     *
     * 原因：vtkClipDataSet 输出的是 vtkUnstructuredGrid 类型，
     * 而 vtkPolyDataMapper 只能处理 vtkPolyData 类型，
     * 两者不兼容导致模型消失。
     * vtkDataSetMapper 可以处理所有VTK数据类型，包括：
     *   - vtkPolyData（无滤镜时STLReader直接输出）
     *   - vtkUnstructuredGrid（ClipFilter输出）
     *   - ShrinkFilter的输出
     * ---------------------------------------------------------------- */
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    actor  = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    /* 应用初始颜色和可见性 */
    actor->GetProperty()->SetColor(colour[0] / 255.0, colour[1] / 255.0, colour[2] / 255.0);
    actor->SetVisibility(isVisible ? 1 : 0);

    /* 根据当前滤镜状态连接pipeline */
    updatePipeline();
}

void ModelPart::updatePipeline()
{
    if (!file || !mapper) return;

    if (isClipped && isShrunk) {
        /* 两个滤镜都激活：reader → clip → shrink → mapper
         * ClipFilter输出UnstructuredGrid，ShrinkFilter接受任意DataSet，
         * DataSetMapper最终处理ShrinkFilter的输出，类型兼容 */
        clipFilter->SetInputConnection(file->GetOutputPort());
        shrinkFilter->SetInputConnection(clipFilter->GetOutputPort());
        mapper->SetInputConnection(shrinkFilter->GetOutputPort());

    } else if (isClipped && !isShrunk) {
        /* 仅裁剪：reader → clip → mapper */
        clipFilter->SetInputConnection(file->GetOutputPort());
        mapper->SetInputConnection(clipFilter->GetOutputPort());

    } else if (!isClipped && isShrunk) {
        /* 仅收缩：reader → shrink → mapper */
        shrinkFilter->SetInputConnection(file->GetOutputPort());
        mapper->SetInputConnection(shrinkFilter->GetOutputPort());

    } else {
        /* 无滤镜：reader → mapper */
        mapper->SetInputConnection(file->GetOutputPort());
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
     * 为VR创建独立pipeline
     * 同样使用vtkDataSetMapper保证类型兼容
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkPlane> vrClipPlane = vtkSmartPointer<vtkPlane>::New();
    vrClipPlane->SetOrigin(0.0, 0.0, 0.0);
    vrClipPlane->SetNormal(-1.0, 0.0, 0.0);

    vtkSmartPointer<vtkClipDataSet> vrClipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    vrClipFilter->SetClipFunction(vrClipPlane.Get());
    vrClipFilter->SetInputConnection(file->GetOutputPort());

    vtkSmartPointer<vtkShrinkFilter> vrShrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    vrShrinkFilter->SetShrinkFactor(0.8);

    /* VR侧同样用DataSetMapper */
    vtkSmartPointer<vtkDataSetMapper> newMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    /* 根据当前滤镜状态连接VR pipeline */
    if (isClipped && isShrunk) {
        vrClipFilter->SetInputConnection(file->GetOutputPort());
        vrShrinkFilter->SetInputConnection(vrClipFilter->GetOutputPort());
        newMapper->SetInputConnection(vrShrinkFilter->GetOutputPort());

    } else if (isClipped && !isShrunk) {
        vrClipFilter->SetInputConnection(file->GetOutputPort());
        newMapper->SetInputConnection(vrClipFilter->GetOutputPort());

    } else if (!isClipped && isShrunk) {
        vrShrinkFilter->SetInputConnection(file->GetOutputPort());
        newMapper->SetInputConnection(vrShrinkFilter->GetOutputPort());

    } else {
        newMapper->SetInputConnection(file->GetOutputPort());
    }

    vtkActor* newActor = vtkActor::New();
    newActor->SetMapper(newMapper);

    /* 共享属性：颜色自动同步 */
    newActor->SetProperty(actor->GetProperty());

    /* 可见性不在Property中，需手动同步 */
    newActor->SetVisibility(isVisible ? 1 : 0);

    return newActor;
}
