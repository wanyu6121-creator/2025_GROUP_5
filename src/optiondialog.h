/**  @file optiondialog.h
 *
 *   EEEE2076 - 软件工程与VR项目
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   用于编辑 ModelPart 名称、颜色和可见性的对话框。
 *   Dialog for editing a ModelPart's name, colour, and visibility.
 *   提供三个 RGB 微调框、一个打开 QColorDialog 的 "Pick Colour..." 按钮和实时颜色预览。
 *   Provides three RGB spin boxes, a "Pick Colour..." button that opens QColorDialog, and a live colour preview swatch.
 */

#ifndef OPTIONDIALOG_H
#define OPTIONDIALOG_H

#include <QDialog>
#include <QColor>

namespace Ui {
class OptionDialog;
}

class OptionDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @brief Constructor
     * @param parent 父窗口部件
     * @param parent parent widget
     */
    explicit OptionDialog(QWidget *parent = nullptr);

    /** 析构函数。
     *  Destructor. */
    ~OptionDialog();

    /**
     * @brief 使用当前零件属性预填对话框。
     * @brief Pre-fill the dialog with the current part properties.
     * @param name    树中显示的零件名称
     * @param name    part name shown in the tree
     * @param r       红色通道 0-255
     * @param r       red channel 0-255
     * @param g       绿色通道 0-255
     * @param g       green channel 0-255
     * @param b       蓝色通道 0-255
     * @param b       blue channel 0-255
     * @param visible 当前可见性标志
     * @param visible current visibility flag
     */
    void setInitialData(const QString &name, int r, int g, int b, bool visible);

    /** @return 编辑后的零件名称
     *          Edited part name */
    QString getName() const;

    /** @return 红色通道 0-255
     *          Red channel 0-255 */
    int getR() const;

    /** @return 绿色通道 0-255
     *          Green channel 0-255 */
    int getG() const;

    /** @return 蓝色通道 0-255
     *          Blue channel 0-255 */
    int getB() const;

    /** @return 可见性复选框状态
     *          Visibility checkbox state */
    bool getIsVisible() const;

private slots:
    /**
     * @brief 打开 QColorDialog;确认后将选中的 RGB 写回三个微调框。
     * @brief Opens QColorDialog; on acceptance writes chosen RGB back into the three spin boxes.
     *        同时刷新预览色块。
     *        Also refreshes the preview swatch.
     */
    void onPickColour();

    /**
     * @brief 任意 RGB 微调框数值变化时调用。
     * @brief Called whenever any of the three RGB spin boxes change value.
     *        保持预览色块与微调框数值同步。
     *        Keeps the preview swatch in sync with the spin box values.
     */
    void onSpinBoxChanged();

private:
    /**
     * @brief 重新绘制 colourSwatch 标签背景以匹配 (r, g, b)。
     * @brief Repaint the colourSwatch label background to match (r, g, b).
     * @param r 红色 0-255
     * @param r red 0-255
     * @param g 绿色 0-255
     * @param g green 0-255
     * @param b 蓝色 0-255
     * @param b blue 0-255
     */
    void updateSwatch(int r, int g, int b);

    Ui::OptionDialog *ui;
};

#endif /* 结束 OPTIONDIALOG_H 包含保护
        * End OPTIONDIALOG_H include guard */
