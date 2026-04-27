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
    /* 从传入的data中提取初始可见性 */
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

ModelPart* ModelPart::child(int row)
{
    if (row < 0 || row >= m_childItems.size())
        return nullptr;
    return m_childItems.at(row);
}

int ModelPart::childCount() const
{
    return m_childItems.count();
}

int ModelPart::columnCount() const
{
    return m_itemData.count();
}

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

ModelPart* ModelPart::parentItem()
{
    return m_parentItem;
}

int ModelPart::row() const
{
    if (m_parentItem)
        return m_parentItem->m_childItems.indexOf(const_cast<ModelPart*>(this));
    return 0;
}

void ModelPart::setColour(const unsigned char R, const unsigned char G, const unsigned char B)
{
    colour[0] = R;
    colour[1] = G;
    colour[2] = B;

    /* 同步到数据列表，确保对话框能读到 */
    while (m_itemData.size() < 5) m_itemData.append(QVariant());
    m_itemData.replace(2, R);
    m_itemData.replace(3, G);
    m_itemData.replace(4, B);

    /* 更新GUI侧actor的颜色
     * 由于VR侧actor通过SetProperty共享了同一个vtkProperty对象，
     * 此修改会自动同步到VR渲染中，无需额外操作 */
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

    /* 同步到数据列表 */
    if (m_itemData.size() > 1) {
        m_itemData.replace(1, isVisible ? "true" : "false");
    }

    /* 更新GUI侧actor可见性
     * 同理，VR侧actor共享Property，可见性自动同步 */
    if (actor != nullptr) {
        actor->SetVisibility(isVisible ? 1 : 0);
    }
}

bool ModelPart::visible()
{
    return isVisible;
}

void ModelPart::loadSTL(QString fileName)
{
    /* 创建STL读取器（GUI和VR共享同一个数据源，节省内存）*/
    file = vtkSmartPointer<vtkSTLReader>::New();
    file->SetFileName(fileName.toStdString().c_str());

    /* 创建GUI侧的mapper和actor */
    mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(file->GetOutputPort());

    actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    /* 应用当前保存的颜色和可见性到新建的actor */
    actor->GetProperty()->SetColor(colour[0] / 255.0, colour[1] / 255.0, colour[2] / 255.0);
    actor->SetVisibility(isVisible ? 1 : 0);
}

vtkSmartPointer<vtkActor> ModelPart::getActor()
{
    return actor;
}

vtkActor* ModelPart::getNewActor()
{
    /* 如果还没有加载STL文件，无法创建新Actor */
    if (file == nullptr || actor == nullptr) {
        return nullptr;
    }

    /* ----------------------------------------------------------------
     * 步骤1：为VR创建独立的新Mapper
     * 连接到与GUI侧相同的STLReader（共享数据源，不重复读文件）
     * ---------------------------------------------------------------- */
    vtkSmartPointer<vtkPolyDataMapper> newMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    newMapper->SetInputConnection(file->GetOutputPort());

    /* ----------------------------------------------------------------
     * 步骤2：创建新Actor并连接到新Mapper
     * ---------------------------------------------------------------- */
    vtkActor* newActor = vtkActor::New();
    newActor->SetMapper(newMapper);

    /* ----------------------------------------------------------------
     * 步骤3：共享属性对象（核心！）
     *
     * SetProperty 让新Actor指向与原GUI Actor完全相同的 vtkProperty 对象。
     * 这意味着：
     *   - 调用 actor->GetProperty()->SetColor() 时，newActor的颜色也会改变
     *   - 调用 actor->SetVisibility() 时，需要对两个actor分别设置
     *     （Visibility是Actor级别的，不在Property中）
     *
     * 因此颜色修改自动同步到VR，可见性需要通过命令队列显式同步。
     * ---------------------------------------------------------------- */
    newActor->SetProperty(actor->GetProperty());

    /* 同步初始可见性（不在Property中，需要手动设置）*/
    newActor->SetVisibility(isVisible ? 1 : 0);

    /* 返回裸指针，由调用方（VRRenderThread）负责管理生命周期 */
    return newActor;
}
