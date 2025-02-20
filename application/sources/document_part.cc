#include "document.h"

Document::Part::Part(const dust3d::Uuid& withId)
    : visible(true)
    , locked(false)
    , subdived(false)
    , disabled(false)
    , xMirrored(false)
    , deformThickness(1.0)
    , deformWidth(1.0)
    , deformUnified(false)
    , rounded(false)
    , chamfered(false)
    , color(Qt::white)
    , hasColor(false)
    , dirty(true)
    , cutRotation(0.0)
    , cutFace(dust3d::CutFace::Quad)
    , target(dust3d::PartTarget::Model)
    , colorSolubility(0.0)
    , metalness(0.0)
    , roughness(1.0)
    , hollowThickness(0.0)
    , countershaded(false)
    , smoothCutoffDegrees(0.0)
{
    id = withId.isNull() ? dust3d::Uuid::createUuid() : withId;
}

bool Document::Part::hasPolyFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasSmoothFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasSubdivFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasRoundEndFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasMirrorFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasChamferFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasRotationFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasHollowFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasCutFaceFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasLayerFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasTargetFunction() const
{
    return true;
}

bool Document::Part::hasBaseFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasCombineModeFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasDeformFunction() const
{
    return dust3d::PartTarget::Model == target;
}

bool Document::Part::hasColorFunction() const
{
    return dust3d::PartTarget::Model == target;
}

void Document::Part::setDeformThickness(float toThickness)
{
    if (toThickness < 0)
        toThickness = 0;
    else if (toThickness > 2)
        toThickness = 2;
    deformThickness = toThickness;
}

void Document::Part::setDeformWidth(float toWidth)
{
    if (toWidth < 0)
        toWidth = 0;
    else if (toWidth > 2)
        toWidth = 2;
    deformWidth = toWidth;
}

void Document::Part::setCutRotation(float toRotation)
{
    if (toRotation < -1)
        toRotation = -1;
    else if (toRotation > 1)
        toRotation = 1;
    cutRotation = toRotation;
}

void Document::Part::setCutFace(dust3d::CutFace face)
{
    cutFace = face;
    cutFaceLinkedId = dust3d::Uuid();
}

void Document::Part::setCutFaceLinkedId(const dust3d::Uuid& linkedId)
{
    if (linkedId.isNull()) {
        setCutFace(dust3d::CutFace::Quad);
        return;
    }
    cutFace = dust3d::CutFace::UserDefined;
    cutFaceLinkedId = linkedId;
}

bool Document::Part::deformThicknessAdjusted() const
{
    return fabs(deformThickness - 1.0) >= 0.01;
}

bool Document::Part::deformWidthAdjusted() const
{
    return fabs(deformWidth - 1.0) >= 0.01;
}

bool Document::Part::deformAdjusted() const
{
    return deformThicknessAdjusted() || deformWidthAdjusted() || deformUnified;
}

bool Document::Part::colorSolubilityAdjusted() const
{
    return fabs(colorSolubility - 0.0) >= 0.01;
}

bool Document::Part::metalnessAdjusted() const
{
    return fabs(metalness - 0.0) >= 0.01;
}

bool Document::Part::roughnessAdjusted() const
{
    return fabs(roughness - 1.0) >= 0.01;
}

bool Document::Part::cutRotationAdjusted() const
{
    return fabs(cutRotation - 0.0) >= 0.01;
}

bool Document::Part::hollowThicknessAdjusted() const
{
    return fabs(hollowThickness - 0.0) >= 0.01;
}

bool Document::Part::cutFaceAdjusted() const
{
    return cutFace != dust3d::CutFace::Quad;
}

bool Document::Part::cutAdjusted() const
{
    return cutRotationAdjusted() || cutFaceAdjusted() || hollowThicknessAdjusted();
}

bool Document::Part::isEditVisible() const
{
    return visible && !disabled;
}

void Document::Part::copyAttributes(const Part& other)
{
    visible = other.visible;
    locked = other.locked;
    subdived = other.subdived;
    disabled = other.disabled;
    xMirrored = other.xMirrored;
    deformThickness = other.deformThickness;
    deformWidth = other.deformWidth;
    rounded = other.rounded;
    chamfered = other.chamfered;
    color = other.color;
    hasColor = other.hasColor;
    cutRotation = other.cutRotation;
    cutFace = other.cutFace;
    cutFaceLinkedId = other.cutFaceLinkedId;
    componentId = other.componentId;
    dirty = other.dirty;
    target = other.target;
    colorSolubility = other.colorSolubility;
    countershaded = other.countershaded;
    metalness = other.metalness;
    roughness = other.roughness;
    deformUnified = other.deformUnified;
    smoothCutoffDegrees = other.smoothCutoffDegrees;
    colorImageId = other.colorImageId;
    hollowThickness = other.hollowThickness;
}
