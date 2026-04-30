/**  @file ModelPart.h
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Template for model parts that will be added as treeview items.
 *   Supports five independently toggleable VTK filters:
 *     1. Clip      – cuts geometry at x=0
 *     2. Shrink    – pulls cells toward their centroid
 *     3. Smooth    – softens sharp edges (Laplacian smoothing)
 *     4. Decimate  – reduces polygon count
 *     5. Elevation – colours geometry by height using a lookup table
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

/* Filter includes */
#include <vtkClipDataSet.h>
#include <vtkShrinkFilter.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkDecimatePro.h>
#include <vtkElevationFilter.h>
#include <vtkPlane.h>
#include <vtkAppendPolyData.h>
#include <vtkTubeFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkShrinkFilter.h>
#include <vtkClipDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkLookupTable.h>
#include <vtkCleanPolyData.h>
#include <vtkGeometryFilter.h>

class ModelPart {
public:
    /** Constructor
     * @param data   node attribute list (name, visibility, RGB etc.)
     * @param parent parent node pointer
     */
    ModelPart(const QList<QVariant>& data, ModelPart* parent = nullptr);

    /** Destructor — recursively frees all child nodes */
    ~ModelPart();

    /** Add a child node.
     * @param item child pointer (must be heap-allocated)
     */
    void appendChild(ModelPart* item);

    /** Remove and delete the child at the given row.
     * Caller is responsible for calling beginRemoveRows/endRemoveRows first.
     * @param row zero-based index of the child to remove
     */
    void removeChild(int row);

    /** @return child at row, or nullptr if out of range */
    ModelPart* child(int row);

    /** @return number of child nodes */
    int childCount() const;

    /** @return number of data columns */
    int columnCount() const;

    /** @return data for the given column as a QVariant */
    QVariant data(int column) const;

    /** Set data for a column.
     * @param column column index
     * @param value  value to set
     */
    void set(int column, const QVariant& value);

    /** @return pointer to parent node */
    ModelPart* parentItem();

    /** @return row index of this node relative to its parent */
    int row() const;

    /** Set RGB colour (0-255). Writes directly to the shared vtkProperty
     *  so VR actors sharing the property update automatically.
     * @param R red channel
     * @param G green channel
     * @param B blue channel
     */
    void setColour(const unsigned char R, const unsigned char G, const unsigned char B);

    unsigned char getColourR(); /**< @return red channel 0-255 */
    unsigned char getColourG(); /**< @return green channel 0-255 */
    unsigned char getColourB(); /**< @return blue channel 0-255 */

    /** Set visibility. Updates the GUI actor directly.
     * @param isVisible true to show, false to hide
     */
    void setVisible(bool isVisible);

    /** @return true if part is currently visible */
    bool visible();

    /** Load an STL file and build the full VTK pipeline.
     *  All five filter objects are created here regardless of their
     *  initial state; updatePipeline() then wires only the active ones.
     * @param fileName full path to the STL file
     */
    void loadSTL(QString fileName);

    /** @return GUI-side actor, or nullptr if loadSTL has not been called */
    vtkSmartPointer<vtkActor> getActor();

    /** @return raw pointer to the STLReader (for VRRenderThread pipeline use) */
    vtkSTLReader* getReader() { return file.Get(); }

    /** Create a new independent actor for VR rendering.
     *  Shares the vtkProperty with the GUI actor so colour changes
     *  are reflected in VR automatically without extra commands.
     * @return new vtkActor* (caller owns lifetime), nullptr if not loaded
     */
    vtkActor* getNewActor();

    /* ---- Filter toggles ---- */

    /** Enable or disable the clip filter (cuts at x=0 along -x normal).
     * @param enabled true to apply
     */
    void setClip(bool enabled);

    /** Enable or disable the shrink filter (pulls cells toward centroid).
     * @param enabled true to apply
     */
    void setShrink(bool enabled);

    /** Enable or disable the smooth filter (Laplacian smoothing, 20 iterations).
     * @param enabled true to apply
     */
    void setSmooth(bool enabled);

    /** Enable or disable the decimate filter (reduces polygon count by 50%).
     * @param enabled true to apply
     */
    void setDecimate(bool enabled);

    /** Enable or disable the elevation filter (colours geometry by Z height).
     * @param enabled true to apply
     */
    void setElevation(bool enabled);

    bool getClip()      { return isClipped;    }
    bool getShrink()    { return isShrunk;     }
    bool getSmooth()    { return isSmoothed;   }
    bool getDecimate()  { return isDecimated;  }
    bool getElevation() { return isElevated;   }

    /** Enable or disable the creative slice (cross-section) view.
     * @param enabled true to apply
     */
    void setSlice(bool enabled) { isSliced = enabled; updatePipeline(); }
    bool getSlice() { return isSliced; } /**< @return true if slice view active */

private:
    /** Reconnect the GUI VTK pipeline based on current filter flags.
     *  Active filters are chained in order:
     *    STLReader → [Clip] → [Shrink] → [Smooth] → [Decimate] → [Elevation] → Mapper
     *  Inactive filters are bypassed by connecting the previous stage directly.
     */
    void updatePipeline();

    QList<ModelPart*>   m_childItems;  /**< child node list */
    QList<QVariant>     m_itemData;    /**< column data for this node */
    ModelPart*          m_parentItem;  /**< parent node pointer */

    bool isVisible;  /**< visibility flag */

    /* VTK pipeline objects */
    vtkSmartPointer<vtkSTLReader>           file;          /**< STL reader (shared with VR) */
    vtkSmartPointer<vtkMapper>              mapper;        /**< GUI mapper (vtkDataSetMapper) */
    vtkSmartPointer<vtkActor>              actor;          /**< GUI actor */
    vtkColor3<unsigned char>               colour;         /**< current RGB colour */

    /* Filter objects — created in loadSTL(), toggled via flags */
    vtkSmartPointer<vtkClipDataSet>          clipFilter;     /**< cuts geometry at x=0 */
    vtkSmartPointer<vtkShrinkFilter>         shrinkFilter;   /**< pulls cells toward centroid */
    vtkSmartPointer<vtkSmoothPolyDataFilter> smoothFilter;   /**< Laplacian smoothing */
    vtkSmartPointer<vtkCleanPolyData>        cleanFilter;    /**< removes duplicate points before decimation */
    vtkSmartPointer<vtkGeometryFilter>       geometryFilter; /**< converts UnstructuredGrid to PolyData */
    vtkSmartPointer<vtkDecimatePro>          decimateFilter; /**< polygon count reduction */
    vtkSmartPointer<vtkElevationFilter>      elevationFilter;/**< Z-height colour mapping */
    vtkSmartPointer<vtkLookupTable>          elevationLUT;   /**< colour table for elevation */

    /* Filter state flags */
    bool isClipped   = false;
    bool isShrunk    = false;
    bool isSmoothed  = false;
    bool isDecimated = false;
    bool isElevated  = false;
    bool isSliced    = false; /**< creative cross-section view active */
};

#endif // VIEWER_MODELPART_H
