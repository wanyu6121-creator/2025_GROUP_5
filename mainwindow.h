/**  @file mainwindow.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Main application window. Owns the VTK renderer and ModelPartList tree.
 *   Handles file loading, tree interaction, five filter toggles, VR control,
 *   lighting, and node deletion.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include "ModelPartList.h"
#include "VRRenderThread.h"

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLight.h>
#include <QCheckBox>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** Constructor -- sets up UI, connects signals/slots, initialises VTK renderer.
     * @param parent parent widget, nullptr for top-level window
     */
    MainWindow(QWidget* parent = nullptr);

    /** Destructor -- stops VR thread if running, frees UI. */
    ~MainWindow();

public slots:
    /* ---- File and tree ---- */

    /** Slot for the Add Item button (currently shows status message). */
    void handleButton();

    /** Fired when user clicks a tree item.
     *  Updates the status bar and synchronises all five filter checkboxes
     *  to reflect the selected part's current state.
     */
    void handleTreeClicked();

    /** File > Open File -- opens a single STL file as a child of the selected node. */
    void on_actionOpen_File_triggered();

    /** File > Open Directory -- recursively loads all STL files from a directory,
     *  mapping the folder structure to tree parent/child nodes.
     */
    void on_actionOpen_Directory_triggered();

    /** Opens the OptionDialog pre-filled with the selected part's properties. */
    void handleOptionsButton();

    /** Relay for the actionItem_Options right-click action. */
    void on_actionItem_Options_triggered();

    /** Traverses the full ModelPartList tree and re-adds all actors to the renderer. */
    void updateRender();

    /** Recursive helper for updateRender().
     * @param index current tree node to process
     */
    void updateRenderFromTree(const QModelIndex& index);

    /* ---- Filter toggles (GUI + VR sync) ---- */

    /** Toggle the clip filter on the selected part.
     *  Disables smooth automatically if both would be active (type mismatch).
     * @param checked true if checkbox was just ticked
     */
    void handleClipToggle(bool checked);

    /** Toggle the shrink filter on the selected part.
     * @param checked true if checkbox was just ticked
     */
    void handleShrinkToggle(bool checked);

    /** Toggle the smooth filter (Laplacian, 20 iterations) on the selected part.
     *  Disables clip automatically if both would be active (type mismatch).
     * @param checked true if checkbox was just ticked
     */
    void handleSmoothToggle(bool checked);

    /** Toggle the decimate filter (50% polygon reduction) on the selected part.
     * @param checked true if checkbox was just ticked
     */
    void handleDecimateToggle(bool checked);

    /** Toggle the elevation filter (Z-height rainbow colouring) on the selected part.
     * @param checked true if checkbox was just ticked
     */
    void handleElevationToggle(bool checked);

    /** Toggle the slice (cross-section) creative feature on the selected part.
     * @param checked true if checkbox was just ticked
     */
    void handleSliceToggle(bool checked);

    /* ---- VR control ---- */

    /** Start the VR render thread, registering all loaded parts as VR actors. */
    void handleStartVR();

    /** Stop the VR render thread. */
    void handleStopVR();

    /** Start auto-rotation animation in VR. */
    void handleStartRotate();

    /** Stop auto-rotation animation in VR. */
    void handleStopRotate();

    /** Reset model -- all parts return to original positions and orientation. */
    void handleResetView();

    /** Set model to Front view (pitch=0, yaw=0). */
    void handleViewFront();
    /** Set model to Top view (pitch=90, yaw=0). */
    void handleViewTop();
    /** Set model to Right Side view (pitch=0, yaw=-90). */
    void handleViewRight();
    /** Set model to Isometric view (pitch=30, yaw=45). */
    void handleViewIso();

    /** Light intensity slider changed.
     *  Maps slider value (0-100) to intensity (0.0-2.0) and updates
     *  both the GUI renderer and the VR thread.
     * @param value slider value 0-100
     */
    void handleLightIntensityChanged(int value);

    /* ---- Node management ---- */

    /** Delete the selected tree node, removing its actor from the GUI renderer
     *  and sending CMD_REMOVE_ACTOR to the VR thread.
     */
    void handleDeleteNode();

signals:
    /** Emitted to display a message in the status bar.
     * @param message text to display
     * @param timeout duration in ms (0 = until next message)
     */
    void statusUpdateMessage(const QString& message, int timeout);

private:
    /** Traverse the full tree and register all loaded parts as VR actors. */
    void populateVRActors();

    /** Recursive helper for populateVRActors().
     * @param index current tree node
     */
    void populateVRActorsFromTree(const QModelIndex& index);

    /** Look up a part's VR actor index.
     * @param part ModelPart pointer to look up
     * @return actor index, or -1 if not registered
     */
    int getActorIndex(ModelPart* part) const;

    /** Helper: apply a filter toggle to the selected part and sync to VR.
     *  Handles the no-selection guard, checkbox revert, updateRender(), and
     *  VR command issue in one place.
     * @param checked    new checkbox state
     * @param filterType FILTER_CLIP / FILTER_SHRINK etc. constant
     * @param checkBox   the checkbox widget to revert if no item selected
     * @param setFn      pointer to the ModelPart setter (e.g. &ModelPart::setClip)
     * @param label      human-readable filter name for the status bar message
     */
    void applyFilterToggle(bool checked,
                           int filterType,
                           QCheckBox* checkBox,
                           void (ModelPart::*setFn)(bool),
                           const QString& label);

    Ui::MainWindow*                              ui;
    ModelPartList*                               partList;       /**< tree model */
    vtkSmartPointer<vtkRenderer>                 renderer;       /**< GUI renderer */
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> renderWindow;  /**< Qt-linked render window */

    VRRenderThread*  vrThread;      /**< VR render thread, nullptr when not running */
    bool             isVRRotating;  /**< current rotation animation state */

    QMap<ModelPart*, int> actorIndexMap; /**< maps ModelPart* to VR actor index */

    vtkSmartPointer<vtkLight> guiKeyLight;  /**< main GUI light (slider-controlled) */
    vtkSmartPointer<vtkLight> guiFillLight; /**< fill GUI light (40% of key) */
};

#endif // MAINWINDOW_H
