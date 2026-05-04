/**     @file ModelPartList.cpp
  *
  *     EEEE2076 - 软件工程与VR项目
  *     EEEE2076 - Software Engineering & VR Project
  *
  *     ModelPartList树模型实现,用于创建树视图。
  *     ModelPartList tree-model implementation used to create the tree view.
  *
  *     P Evans 2022
  */

#include "ModelPartList.h"
#include "ModelPart.h"

ModelPartList::ModelPartList( const QString& data, QObject* parent ) : QAbstractItemModel(parent) {
    /* 可以指定树中每个条目的可见属性数量;根条目充当列标题。
     * Allows specifying the number of visible properties for each tree item; the root item acts as the column headers.
     */
    rootItem = new ModelPart( { tr("Part"), tr("Visible?"),tr("Red"),tr("Green"),tr("Blue") } );
}



ModelPartList::~ModelPartList() {
    delete rootItem;
}


int ModelPartList::columnCount( const QModelIndex& parent ) const {
    Q_UNUSED(parent);

    return rootItem->columnCount();
}


QVariant ModelPartList::data( const QModelIndex& index, int role ) const {
    /* 如果条目索引无效,返回一个新的空 QVariant (QVariant 是可表示任何有效 Qt 类的通用数据类型)。
     * If the item index is not valid, return a new empty QVariant (a generic data type that can represent any valid Qt class).
     */
    if( !index.isValid() )
        return QVariant();

    /* role 表示数据的用途;这里只有 Qt 请求用于创建和显示树视图的数据时才需要处理。
     * role represents what this data will be used for; we only handle Qt requests for creating and displaying the tree view.
     * 如果收到其他请求,返回一个新的空 QVariant。
     * Return a new empty QVariant for any other request.
     */
    if (role != Qt::DisplayRole)
        return QVariant();

    /* 获取 QModelIndex 所引用条目的指针。
     * Get a pointer to the item referred to by the QModelIndex. */
    ModelPart* item = static_cast<ModelPart*>( index.internalPointer() );

    /* 树中的每个条目都有多个列(初始示例中为 "Part" 和 "Visible"),返回 QModelIndex 请求的列。
     * Each item in the tree has several columns ("Part" and "Visible" in the initial example); return the column requested by the QModelIndex.
     */
    return item->data( index.column() );
}


Qt::ItemFlags ModelPartList::flags( const QModelIndex& index ) const {
    if( !index.isValid() )
        return Qt::NoItemFlags;

    return QAbstractItemModel::flags( index );
}


QVariant ModelPartList::headerData( int section, Qt::Orientation orientation, int role ) const {
    if( orientation == Qt::Horizontal && role == Qt::DisplayRole )
        return rootItem->data( section );

    return QVariant();
}


QModelIndex ModelPartList::index(int row, int column, const QModelIndex& parent) const {
    ModelPart* parentItem;
    
    if( !parent.isValid() || !hasIndex(row, column, parent) )
        parentItem = rootItem;              /* 默认选择根节点。
                                       * Default to selecting root. */
    else
        parentItem = static_cast<ModelPart*>(parent.internalPointer());

    ModelPart* childItem = parentItem->child(row);
    if( childItem )
        return createIndex(row, column, childItem);
    
    
    return QModelIndex();
}


QModelIndex ModelPartList::parent( const QModelIndex& index ) const {
    if (!index.isValid())
        return QModelIndex();

    ModelPart* childItem = static_cast<ModelPart*>(index.internalPointer());
    ModelPart* parentItem = childItem->parentItem();

    if( parentItem == rootItem )
        return QModelIndex();

    return createIndex( parentItem->row(), 0, parentItem );
}


int ModelPartList::rowCount( const QModelIndex& parent ) const {
    ModelPart* parentItem;
    if( parent.column() > 0 )
        return 0;

    if( !parent.isValid() )
        parentItem = rootItem;
    else
        parentItem = static_cast<ModelPart*>(parent.internalPointer());

    return parentItem->childCount();
}


ModelPart* ModelPartList::getRootItem() {
    return rootItem; 
}



QModelIndex ModelPartList::appendChild(QModelIndex& parent, const QList<QVariant>& data) {      
    ModelPart* parentPart;

    if (parent.isValid())
        parentPart = static_cast<ModelPart*>(parent.internalPointer());
    else {
        parentPart = rootItem;
        parent = createIndex(0, 0, rootItem );
    }

    beginInsertRows( parent, rowCount(parent), rowCount(parent) ); 

    ModelPart* childPart = new ModelPart( data, parentPart );

    parentPart->appendChild(childPart);

    QModelIndex child = createIndex(0, 0, childPart);

    endInsertRows();

    emit layoutChanged();

    return child;
}


bool ModelPartList::removeItem( const QModelIndex& index )
{
    if ( !index.isValid() )
        return false;

    ModelPart* item       = static_cast<ModelPart*>( index.internalPointer() );
    ModelPart* parentItem = item->parentItem();

    /* 拒绝删除不可见的根条目。
     * Refuse to delete the invisible root item. */
    if ( parentItem == nullptr )
        return false;

    QModelIndex parentIndex = parent( index );   /* 调用 QAbstractItemModel::parent()。
                                             * Call QAbstractItemModel::parent(). */
    int row = item->row();

    /* 通知视图即将移除行。
     * Notify the view that rows are about to be removed. */
    beginRemoveRows( parentIndex, row, row );

    /* 删除节点(ModelPart 析构函数会递归释放子节点)。
     * Delete the node (ModelPart destructor recursively frees children). */
    parentItem->removeChild( row );

    endRemoveRows();

    return true;
}
