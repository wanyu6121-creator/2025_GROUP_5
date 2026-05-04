/**     @file ModelPartList.h
  *
  *     EEEE2076 - 软件工程与VR项目
  *     EEEE2076 - Software Engineering & VR Project
  *
  *     ModelPartList树模型类,用于创建树视图。
  *     ModelPartList tree-model class used to create the tree view.
  *
  *     P Evans 2022
  */
  
#ifndef VIEWER_MODELPARTLIST_H
#define VIEWER_MODELPARTLIST_H


#include "ModelPart.h"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>
#include <QString>
#include <QList>

class ModelPart;

class ModelPartList : public QAbstractItemModel {
    Q_OBJECT        /**< Qt 特殊标记,表示该类可能需要在编译前由元对象编译器预处理。
                     * A special Qt tag indicating that this class may require preprocessing before compiling. */
public:
    /** 构造函数。
      *  Constructor.
      *  参数是此类的标准参数,但此示例不使用 data。
      *  Arguments are standard for this class type, but data is not used in this example.
      * @param data 未使用
      *              Not used
      * @param parent 传递给父类构造函数的父对象
      *               Parent object passed to the base-class constructor
      */
    ModelPartList( const QString& data, QObject* parent = NULL );

    /** 析构函数。
      *  Destructor.
      *  释放构造函数中分配的根条目。
      *  Frees the root item allocated in the constructor.
      */
    ~ModelPartList();

    /** 返回列数。
      *  Return the column count.
      * @param parent 未使用
      *               Not used
      * @return 树视图中的列数,例如此处为 "Part"、"Visible"、"Red"、"Green" 和 "Blue"
      *         Number of columns in the tree view, such as "Part", "Visible", "Red", "Green" and "Blue" here
      */
    int columnCount( const QModelIndex& parent ) const;

    /** 返回指定行(条目索引)和列的值。
      *  Return the value for a particular row (item index) and column.
      *  Qt 内部使用此函数取得 TreeView 中要显示的文本。
      *  Qt uses this internally to retrieve the text displayed in the TreeView.
      * @param index Qt 用来指定所需行和列的结构
      *              Qt structure specifying the requested row and column
      * @param role Qt 用来说明数据用途的角色
      *             Qt role describing how the data will be used
      * @return 表示 Qt 类型的通用 QVariant;此处通常为字符串
      *         Generic QVariant representing a Qt type; here it is usually a string
      */
    QVariant data( const QModelIndex& index, int role ) const;

    /** Qt 内部使用的标准函数。
      *  Standard function used internally by Qt.
      * @param index Qt 用来指定所需行和列的结构
      *              Qt structure specifying the requested row and column
      * @return Qt 条目标志
      *         Qt item flags
      */
    Qt::ItemFlags flags( const QModelIndex& index ) const;


    /** Qt 内部使用的标准函数。
      *  Standard function used internally by Qt.
      */
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const;


    /** 获取树中某个位置的有效 QModelIndex。
      *  Get a valid QModelIndex for a location in the tree.
      *  row 是 parent 下的行;未指定 parent 时则位于根节点下。
      *  row is under parent, or under the root when parent is not specified.
      * @param row 条目索引
      *            Item index
      * @param column 列索引,例如零件名称或可见性字符串
      *               Column index, such as part name or visibility string
      * @param parent 行所引用的父节点,通常为树根
      *               Parent that the row is referenced from, usually the tree root
      * @return QModelIndex 结构
      *         QModelIndex structure
     */
    QModelIndex index( int row, int column, const QModelIndex& parent ) const;


    /** 根据条目的 QModelIndex 获取其父节点的 QModelIndex。
      *  Take a QModelIndex for an item and get a QModelIndex for its parent.
      * @param index 条目的索引
      *              Index of the item
      * @return 父节点的索引
      *         Index of the parent
      */
    QModelIndex parent( const QModelIndex& index ) const;

    /** 获取树中某个条目下的行数(子条目数)。
      *  Get the number of rows (items) under an item in the tree.
      * @param parent 父条目,其下的所有子条目都会被计数
      *               Parent item whose children will be counted
      * @return 子节点数量
      *         Number of children
      */
    int rowCount( const QModelIndex& parent ) const;

    /** 获取树根条目的指针。
      *  Get a pointer to the root item of the tree.
      * @return 根条目指针
      *         Root item pointer
      */
    ModelPart* getRootItem();

    /** 向父节点追加一个子节点并返回新子节点索引。
      *  Append a child to the parent and return the new child's index.
      */
    QModelIndex appendChild( QModelIndex& parent, const QList<QVariant>& data );

    /** 从树中移除给定索引处的条目并删除它。
     *  Remove the item at the given index from the tree and delete it.
     *
     *  内部调用 beginRemoveRows 和 endRemoveRows,确保 TreeView 正确更新。
     *  Calls beginRemoveRows and endRemoveRows internally so the TreeView updates correctly.
     *  同时调用 ModelPart::removeChild() 释放内存。
     *  Also calls ModelPart::removeChild() to free memory.
     *
     * @param index 要移除条目的 QModelIndex (必须有效)
     *              QModelIndex of the item to remove (must be valid)
     * @return 成功返回 true,索引无效则返回 false
     *         true on success, false if index was invalid
     */
    bool removeItem( const QModelIndex& index );


private:
    ModelPart *rootItem;    /**< 指向树根条目的指针。
                            * Pointer to the item at the base of the tree. */
};
#endif

