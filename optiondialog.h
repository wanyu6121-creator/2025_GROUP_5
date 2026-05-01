/**  @file optiondialog.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Dialog for editing a ModelPart's name, colour, and visibility.
 *   Provides three RGB spin boxes for manual entry, a "Pick Colour..."
 *   button that opens QColorDialog, and a live colour preview swatch.
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
     * @brief Constructor
     * @param parent parent widget
     */
    explicit OptionDialog(QWidget *parent = nullptr);

    /** Destructor */
    ~OptionDialog();

    /**
     * @brief Pre-fill the dialog with the current part properties.
     * @param name    part name shown in the tree
     * @param r       red channel 0-255
     * @param g       green channel 0-255
     * @param b       blue channel 0-255
     * @param visible current visibility flag
     */
    void setInitialData(const QString &name, int r, int g, int b, bool visible);

    /** @return edited part name */
    QString getName() const;

    /** @return red channel 0-255 */
    int getR() const;

    /** @return green channel 0-255 */
    int getG() const;

    /** @return blue channel 0-255 */
    int getB() const;

    /** @return visibility checkbox state */
    bool getIsVisible() const;

private slots:
    /**
     * @brief Opens QColorDialog; on acceptance writes chosen RGB back into
     *        the three spin boxes and refreshes the preview swatch.
     */
    void onPickColour();

    /**
     * @brief Called whenever any of the three RGB spin boxes change value.
     *        Keeps the preview swatch in sync with the spin box values.
     */
    void onSpinBoxChanged();

private:
    /**
     * @brief Repaint the colourSwatch label background to match (r, g, b).
     * @param r red 0-255
     * @param g green 0-255
     * @param b blue 0-255
     */
    void updateSwatch(int r, int g, int b);

    Ui::OptionDialog *ui;
};

#endif // OPTIONDIALOG_H
