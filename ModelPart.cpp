/**  @file ModelPart.cpp
 *
 *   EEEE2076 - Software Engineering & VR Project
 *
 *   Template for model parts that will be added as treeview items.
 *   Implements five independently toggleable VTK filters:
 *     1. Clip      – vtkClipDataSet: cuts at x=0 along -x normal
 *     2. Shrink    – vtkShrinkFilter: pulls cells toward centroid (factor 0.8)
 *     3. Smooth    – vtkSmoothPolyDataFilter: Laplacian smoothing (20 iterations)
 *     4. Decimate  – vtkDecimatePro: reduces polygon count by 50%
 *     5. Elevation – vtkElevationFilter: maps Z height to a rainbow colour table
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

ModelPart::ModelPart(const QList<QVariant>& data, ModelPart* parent)
    : m_itemData(data), m_parentItem(parent)
{
    if (m_itemData.size() > 1)
        isVisible = (m_itemData.at(1).toString() == "true");
    else
        isVisible = true;

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
    if (row < 0 || row >= m_childItems.size()) return;
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

    /* Colour is written to the shared vtkProperty so VR actors
     * that share it via SetProperty() update automatically. */
    if (actor != nullptr)
        actor->GetProperty()->SetColor(R / 255.0, G / 255.0, B / 255.0);
}

unsigned char ModelPart::getColourR() { return colour[0]; }
unsigned char ModelPart::getColourG() { return colour[1]; }
unsigned char ModelPart::getColourB() { return colour[2]; }

void ModelPart::setVisible(bool isVis)
{
    isVisible = isVis;
    if (m_itemData.size() > 1)
        m_itemData.replace(1, isVisible ? "true" : "false");
    if (actor != nullptr)
        actor->SetVisibility(isVisible ? 1 : 0);
}

bool ModelPart::visible() { return isVisible; }

void ModelPart::loadSTL(QString fileName)
{
    /* ---- STL reader ---- */
    file = vtkSmartPointer<vtkSTLReader>::New();
    file->SetFileName(fileName.toStdString().c_str());
    file->Update();

    /* ---- Filter 1: Clip ----
     * Plane at origin with -x normal: keeps everything on the +x side */
    vtkSmartPointer<vtkPlane> clipPlane = vtkSmartPointer<vtkPlane>::New();
    clipPlane->SetOrigin(0.0, 0.0, 0.0);
    clipPlane->SetNormal(-1.0, 0.0, 0.0);
    clipFilter = vtkSmartPointer<vtkClipDataSet>::New();
    clipFilter->SetClipFunction(clipPlane.Get());

    /* ---- Filter 2: Shrink ----
     * Pulls each cell 20% toward its own centroid */
    shrinkFilter = vtkSmartPointer<vtkShrinkFilter>::New();
    shrinkFilter->SetShrinkFactor(0.8);

    /* ---- Filter 3: Smooth ----
     * Laplacian smoothing — 20 iterations softens sharp edges visibly */
    smoothFilter = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    smoothFilter->SetNumberOfIterations(20);
    smoothFilter->SetRelaxationFactor(0.1);
    smoothFilter->FeatureEdgeSmoothingOff();
    smoothFilter->BoundarySmoothingOn();

    /* ---- Filter 4: Decimate ----
     * vtkDecimatePro requires clean vtkPolyData with no duplicate points.
     * vtkCleanPolyData merges duplicates first.
     * vtkGeometryFilter converts UnstructuredGrid (from ClipDataSet) to
     * PolyData so decimate can accept it in any pipeline combination. */
    cleanFilter    = vtkSmartPointer<vtkCleanPolyData>::New();
    geometryFilter = vtkSmartPointer<vtkGeometryFilter>::New();
    decimateFilter = vtkSmartPointer<vtkDecimatePro>::New();
    decimateFilter->SetTargetReduction(0.9);
    decimateFilter->PreserveTopologyOn();

    /* ---- Filter 5: Elevation ----
     * Maps Z coordinate to a blue→red rainbow lookup table.
     * Bounds are set relative to the model's actual Z extent after loading. */
    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
    double zMin = bounds[4], zMax = bounds[5];
    /* Guard against flat models where zMin == zMax */
    if (zMax - zMin < 1e-6) { zMin -= 1.0; zMax += 1.0; }

    elevationFilter = vtkSmartPointer<vtkElevationFilter>::New();
    elevationFilter->SetLowPoint(0.0, 0.0, zMin);
    elevationFilter->SetHighPoint(0.0, 0.0, zMax);

    /* Rainbow lookup table: blue (low) → red (high) */
    elevationLUT = vtkSmartPointer<vtkLookupTable>::New();
    elevationLUT->SetNumberOfTableValues(256);
    elevationLUT->SetHueRange(0.667, 0.0);  /* blue → red */
    elevationLUT->Build();

    /* ---- Mapper and actor ----
     * vtkDataSetMapper handles all VTK dataset types (PolyData,
     * UnstructuredGrid from ClipDataSet, etc.) without type errors. */
    mapper = vtkSmartPointer<vtkDataSetMapper>::New();
    actor  = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);

    actor->GetProperty()->SetColor(colour[0] / 255.0, colour[1] / 255.0, colour[2] / 255.0);
    actor->SetVisibility(isVisible ? 1 : 0);

    /* Wire the pipeline based on initial filter flags (all off by default) */
    updatePipeline();
}

void ModelPart::updatePipeline()
{
    /* Guard: pipeline objects must exist before we can connect them */
    if (!file || !mapper) return;

    /* Build an ordered list of active filter input connections.
     * Each active filter takes the output of the previous stage.
     * The order is: Clip → Shrink → Smooth → Decimate → Elevation.
     * Inactive filters are simply skipped. */

    /* Start from the STL reader output */
    vtkAlgorithmOutput* currentOutput = file->GetOutputPort();

    if (isClipped) {
        clipFilter->SetInputConnection(currentOutput);
        currentOutput = clipFilter->GetOutputPort();
    }

    if (isShrunk) {
        shrinkFilter->SetInputConnection(currentOutput);
        currentOutput = shrinkFilter->GetOutputPort();
    }

    if (isSmoothed) {
        /* SmoothPolyDataFilter only accepts vtkPolyData.
         * If clip is active its output is UnstructuredGrid, so smooth
         * must be placed before clip, or we disable smooth when clip is on.
         * Here we place smooth after shrink (which also outputs PolyData)
         * and guard against the clip+smooth combination. */
        smoothFilter->SetInputConnection(currentOutput);
        currentOutput = smoothFilter->GetOutputPort();
    }

    if (isDecimated) {
        /* vtkDecimatePro only accepts vtkPolyData.
         * If the previous stage outputs UnstructuredGrid (e.g. after Clip),
         * vtkGeometryFilter converts it to PolyData first.
         * vtkCleanPolyData then merges duplicate points which is required
         * for vtkDecimatePro to produce correct results. */
        geometryFilter->SetInputConnection(currentOutput);
        cleanFilter->SetInputConnection(geometryFilter->GetOutputPort());
        decimateFilter->SetInputConnection(cleanFilter->GetOutputPort());
        currentOutput = decimateFilter->GetOutputPort();
    }

    if (isElevated) {
        elevationFilter->SetInputConnection(currentOutput);
        currentOutput = elevationFilter->GetOutputPort();

        /* Apply the rainbow lookup table when elevation is active */
        vtkDataSetMapper* dsMapper = vtkDataSetMapper::SafeDownCast(mapper.Get());
        if (dsMapper) {
            dsMapper->SetLookupTable(elevationLUT);
            dsMapper->SetScalarRange(0.0, 1.0);
            dsMapper->ScalarVisibilityOn();
        }
    } else {
        /* Restore per-actor colour when elevation is off */
        vtkDataSetMapper* dsMapper = vtkDataSetMapper::SafeDownCast(mapper.Get());
        if (dsMapper) dsMapper->ScalarVisibilityOff();
    }

    mapper->SetInputConnection(currentOutput);
}

/* ---- Filter setters ---- */

void ModelPart::setClip(bool enabled)
{
    isClipped = enabled;
    /* Smooth filter requires PolyData input; clip outputs UnstructuredGrid.
     * Disable smooth automatically when clip is enabled to avoid a type mismatch. */
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
    /* Cannot combine smooth with clip (type mismatch — see setClip comment) */
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

vtkSmartPointer<vtkActor> ModelPart::getActor()
{
    return actor;
}

vtkActor* ModelPart::getNewActor()
{
    if (file == nullptr || actor == nullptr) return nullptr;

    /* Build an independent VR pipeline mirroring the current GUI state.
     * Uses the same STLReader as the source so no extra file I/O occurs. */
    vtkSmartPointer<vtkPlane> vrClipPlane = vtkSmartPointer<vtkPlane>::New();
    vrClipPlane->SetOrigin(0.0, 0.0, 0.0);
    vrClipPlane->SetNormal(-1.0, 0.0, 0.0);
    vtkSmartPointer<vtkClipDataSet> vrClip = vtkSmartPointer<vtkClipDataSet>::New();
    vrClip->SetClipFunction(vrClipPlane.Get());

    vtkSmartPointer<vtkShrinkFilter> vrShrink = vtkSmartPointer<vtkShrinkFilter>::New();
    vrShrink->SetShrinkFactor(0.8);

    vtkSmartPointer<vtkSmoothPolyDataFilter> vrSmooth = vtkSmartPointer<vtkSmoothPolyDataFilter>::New();
    vrSmooth->SetNumberOfIterations(20);
    vrSmooth->SetRelaxationFactor(0.1);

    vtkSmartPointer<vtkDecimatePro> vrDecimate = vtkSmartPointer<vtkDecimatePro>::New();
    vrDecimate->SetTargetReduction(0.5);
    vrDecimate->PreserveTopologyOn();

    double bounds[6];
    file->GetOutput()->GetBounds(bounds);
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

    /* Mirror the current pipeline state */
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

    vtkActor* newActor = vtkActor::New();
    newActor->SetMapper(newMapper);

    /* Share the property so colour changes in GUI propagate to VR */
    newActor->SetProperty(actor->GetProperty());
    newActor->SetVisibility(isVisible ? 1 : 0);

    return newActor;
}
