#include "document.h"
#include "bone_generator.h"
#include "image_forever.h"
#include "mesh_generator.h"
#include "uv_map_generator.h"
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMimeData>
#include <QThread>
#include <QVector3D>
#include <QtCore/qbuffer.h>
#include <dust3d/base/snapshot_xml.h>
#include <dust3d/base/texture_type.h>
#include <functional>
#include <queue>

unsigned long Document::m_maxSnapshot = 1000;

Document::Document()
{
}

Document::~Document()
{
    delete (dust3d::MeshGenerator::GeneratedCacheContext*)m_generatedCacheContext;
    delete m_resultMesh;
    delete textureImage;
    delete textureImageByteArray;
    delete textureNormalImage;
    delete textureNormalImageByteArray;
    delete textureMetalnessImage;
    delete textureMetalnessImageByteArray;
    delete textureRoughnessImage;
    delete textureRoughnessImageByteArray;
    delete textureAmbientOcclusionImage;
    delete textureAmbientOcclusionImageByteArray;
    delete m_resultTextureMesh;
}

const Document::Node* Document::findNode(dust3d::Uuid nodeId) const
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end())
        return nullptr;
    return &it->second;
}

const Document::Edge* Document::findEdge(dust3d::Uuid edgeId) const
{
    auto it = edgeMap.find(edgeId);
    if (it == edgeMap.end())
        return nullptr;
    return &it->second;
}

const Document::Part* Document::findPart(dust3d::Uuid partId) const
{
    auto it = partMap.find(partId);
    if (it == partMap.end())
        return nullptr;
    return &it->second;
}

const Document::Edge* Document::findEdgeByNodes(dust3d::Uuid firstNodeId, dust3d::Uuid secondNodeId) const
{
    const Document::Node* firstNode = nullptr;
    firstNode = findNode(firstNodeId);
    if (nullptr == firstNode) {
        return nullptr;
    }
    for (auto edgeIdIt = firstNode->edgeIds.begin(); edgeIdIt != firstNode->edgeIds.end(); edgeIdIt++) {
        auto edgeIt = edgeMap.find(*edgeIdIt);
        if (edgeIt == edgeMap.end()) {
            continue;
        }
        if (std::find(edgeIt->second.nodeIds.begin(), edgeIt->second.nodeIds.end(), secondNodeId) != edgeIt->second.nodeIds.end())
            return &edgeIt->second;
    }
    return nullptr;
}

void Document::findAllNeighbors(dust3d::Uuid nodeId, std::set<dust3d::Uuid>& neighbors) const
{
    const auto& node = findNode(nodeId);
    if (nullptr == node) {
        return;
    }
    for (const auto& edgeId : node->edgeIds) {
        const auto& edge = findEdge(edgeId);
        if (nullptr == edge) {
            continue;
        }
        const auto& neighborNodeId = edge->neighborOf(nodeId);
        if (neighborNodeId.isNull()) {
            continue;
        }
        if (neighbors.find(neighborNodeId) != neighbors.end()) {
            continue;
        }
        neighbors.insert(neighborNodeId);
        findAllNeighbors(neighborNodeId, neighbors);
    }
}

bool Document::isNodeConnectable(dust3d::Uuid nodeId) const
{
    const auto& node = findNode(nodeId);
    if (nullptr == node)
        return false;
    if (node->edgeIds.size() < 2)
        return true;
    return false;
}

void Document::reduceNode(dust3d::Uuid nodeId)
{
    const Document::Node* node = findNode(nodeId);
    if (nullptr == node) {
        return;
    }
    if (node->edgeIds.size() != 2) {
        return;
    }
    dust3d::Uuid firstEdgeId = node->edgeIds[0];
    dust3d::Uuid secondEdgeId = node->edgeIds[1];
    const Document::Edge* firstEdge = findEdge(firstEdgeId);
    if (nullptr == firstEdge) {
        return;
    }
    const Document::Edge* secondEdge = findEdge(secondEdgeId);
    if (nullptr == secondEdge) {
        return;
    }
    dust3d::Uuid firstNeighborNodeId = firstEdge->neighborOf(nodeId);
    dust3d::Uuid secondNeighborNodeId = secondEdge->neighborOf(nodeId);
    removeNode(nodeId);
    addEdge(firstNeighborNodeId, secondNeighborNodeId);
}

void Document::breakEdge(dust3d::Uuid edgeId)
{
    const Document::Edge* edge = findEdge(edgeId);
    if (nullptr == edge) {
        return;
    }
    if (edge->nodeIds.size() != 2) {
        return;
    }
    dust3d::Uuid firstNodeId = edge->nodeIds[0];
    dust3d::Uuid secondNodeId = edge->nodeIds[1];
    const Document::Node* firstNode = findNode(firstNodeId);
    if (nullptr == firstNode) {
        return;
    }
    const Document::Node* secondNode = findNode(secondNodeId);
    if (nullptr == secondNode) {
        return;
    }
    QVector3D firstOrigin(firstNode->getX(), firstNode->getY(), firstNode->getZ());
    QVector3D secondOrigin(secondNode->getX(), secondNode->getY(), secondNode->getZ());
    QVector3D middleOrigin = (firstOrigin + secondOrigin) / 2;
    float middleRadius = (firstNode->radius + secondNode->radius) / 2;
    removeEdge(edgeId);
    dust3d::Uuid middleNodeId = createNode(dust3d::Uuid::createUuid(), middleOrigin.x(), middleOrigin.y(), middleOrigin.z(), middleRadius, firstNodeId);
    if (middleNodeId.isNull()) {
        return;
    }
    addEdge(middleNodeId, secondNodeId);
}

void Document::reverseEdge(dust3d::Uuid edgeId)
{
    Document::Edge* edge = (Document::Edge*)findEdge(edgeId);
    if (nullptr == edge) {
        return;
    }
    if (edge->nodeIds.size() != 2) {
        return;
    }
    std::swap(edge->nodeIds[0], edge->nodeIds[1]);
    auto part = partMap.find(edge->partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit edgeReversed(edgeId);
    emit skeletonChanged();
}

void Document::removeEdge(dust3d::Uuid edgeId)
{
    const Document::Edge* edge = findEdge(edgeId);
    if (nullptr == edge) {
        return;
    }
    if (isPartReadonly(edge->partId))
        return;
    const Document::Part* oldPart = findPart(edge->partId);
    if (nullptr == oldPart) {
        return;
    }
    QString nextPartName = oldPart->name;
    dust3d::Uuid oldPartId = oldPart->id;
    std::vector<std::vector<dust3d::Uuid>> groups;
    splitPartByEdge(&groups, edgeId);
    std::vector<std::pair<dust3d::Uuid, size_t>> newPartNodeNumMap;
    std::vector<dust3d::Uuid> newPartIds;
    for (auto groupIt = groups.begin(); groupIt != groups.end(); groupIt++) {
        const auto newUuid = dust3d::Uuid::createUuid();
        Document::Part& part = partMap[newUuid];
        part.id = newUuid;
        part.copyAttributes(*oldPart);
        part.name = nextPartName;
        for (auto nodeIdIt = (*groupIt).begin(); nodeIdIt != (*groupIt).end(); nodeIdIt++) {
            auto nodeIt = nodeMap.find(*nodeIdIt);
            if (nodeIt == nodeMap.end()) {
                continue;
            }
            nodeIt->second.partId = part.id;
            part.nodeIds.push_back(nodeIt->first);
            for (auto edgeIdIt = nodeIt->second.edgeIds.begin(); edgeIdIt != nodeIt->second.edgeIds.end(); edgeIdIt++) {
                auto edgeIt = edgeMap.find(*edgeIdIt);
                if (edgeIt == edgeMap.end()) {
                    continue;
                }
                edgeIt->second.partId = part.id;
            }
        }
        addPartToComponent(part.id, findComponentParentId(part.componentId));
        newPartNodeNumMap.push_back({ part.id, part.nodeIds.size() });
        newPartIds.push_back(part.id);
        emit partAdded(part.id);
    }
    for (auto nodeIdIt = edge->nodeIds.begin(); nodeIdIt != edge->nodeIds.end(); nodeIdIt++) {
        auto nodeIt = nodeMap.find(*nodeIdIt);
        if (nodeIt == nodeMap.end()) {
            continue;
        }
        nodeIt->second.edgeIds.erase(std::remove(nodeIt->second.edgeIds.begin(), nodeIt->second.edgeIds.end(), edgeId), nodeIt->second.edgeIds.end());
        emit nodeOriginChanged(nodeIt->first);
    }
    edgeMap.erase(edgeId);
    emit edgeRemoved(edgeId);
    removePart(oldPartId);

    if (!newPartNodeNumMap.empty()) {
        std::sort(newPartNodeNumMap.begin(), newPartNodeNumMap.end(), [&](const std::pair<dust3d::Uuid, size_t>& first, const std::pair<dust3d::Uuid, size_t>& second) {
            return first.second > second.second;
        });
        updateLinkedPart(oldPartId, newPartNodeNumMap[0].first);
    }

    emit skeletonChanged();
}

void Document::removeNode(dust3d::Uuid nodeId)
{
    const Document::Node* node = findNode(nodeId);
    if (nullptr == node) {
        return;
    }
    if (isPartReadonly(node->partId))
        return;
    const Document::Part* oldPart = findPart(node->partId);
    if (nullptr == oldPart) {
        return;
    }
    QString nextPartName = oldPart->name;
    dust3d::Uuid oldPartId = oldPart->id;
    std::vector<std::vector<dust3d::Uuid>> groups;
    splitPartByNode(&groups, nodeId);
    std::vector<std::pair<dust3d::Uuid, size_t>> newPartNodeNumMap;
    std::vector<dust3d::Uuid> newPartIds;
    for (auto groupIt = groups.begin(); groupIt != groups.end(); groupIt++) {
        const auto newUuid = dust3d::Uuid::createUuid();
        Document::Part& part = partMap[newUuid];
        part.id = newUuid;
        part.copyAttributes(*oldPart);
        part.name = nextPartName;
        for (auto nodeIdIt = (*groupIt).begin(); nodeIdIt != (*groupIt).end(); nodeIdIt++) {
            auto nodeIt = nodeMap.find(*nodeIdIt);
            if (nodeIt == nodeMap.end()) {
                continue;
            }
            nodeIt->second.partId = part.id;
            part.nodeIds.push_back(nodeIt->first);
            for (auto edgeIdIt = nodeIt->second.edgeIds.begin(); edgeIdIt != nodeIt->second.edgeIds.end(); edgeIdIt++) {
                auto edgeIt = edgeMap.find(*edgeIdIt);
                if (edgeIt == edgeMap.end()) {
                    continue;
                }
                edgeIt->second.partId = part.id;
            }
        }
        addPartToComponent(part.id, findComponentParentId(part.componentId));
        newPartNodeNumMap.push_back({ part.id, part.nodeIds.size() });
        newPartIds.push_back(part.id);
        emit partAdded(part.id);
    }
    for (auto edgeIdIt = node->edgeIds.begin(); edgeIdIt != node->edgeIds.end(); edgeIdIt++) {
        auto edgeIt = edgeMap.find(*edgeIdIt);
        if (edgeIt == edgeMap.end()) {
            continue;
        }
        dust3d::Uuid neighborId = edgeIt->second.neighborOf(nodeId);
        auto nodeIt = nodeMap.find(neighborId);
        if (nodeIt == nodeMap.end()) {
            continue;
        }
        nodeIt->second.edgeIds.erase(std::remove(nodeIt->second.edgeIds.begin(), nodeIt->second.edgeIds.end(), *edgeIdIt), nodeIt->second.edgeIds.end());
        edgeMap.erase(*edgeIdIt);
        emit edgeRemoved(*edgeIdIt);
    }
    nodeMap.erase(nodeId);
    emit nodeRemoved(nodeId);
    removePart(oldPartId);

    if (!newPartNodeNumMap.empty()) {
        std::sort(newPartNodeNumMap.begin(), newPartNodeNumMap.end(), [&](const std::pair<dust3d::Uuid, size_t>& first, const std::pair<dust3d::Uuid, size_t>& second) {
            return first.second > second.second;
        });
        updateLinkedPart(oldPartId, newPartNodeNumMap[0].first);
    }

    emit skeletonChanged();
}

void Document::addNode(float x, float y, float z, float radius, dust3d::Uuid fromNodeId)
{
    createNode(dust3d::Uuid::createUuid(), x, y, z, radius, fromNodeId);
}

void Document::addNodeWithId(dust3d::Uuid nodeId, float x, float y, float z, float radius, dust3d::Uuid fromNodeId)
{
    createNode(nodeId, x, y, z, radius, fromNodeId);
}

dust3d::Uuid Document::createNode(dust3d::Uuid nodeId, float x, float y, float z, float radius, dust3d::Uuid fromNodeId)
{
    dust3d::Uuid partId;
    const Document::Node* fromNode = nullptr;
    bool newPartAdded = false;
    if (fromNodeId.isNull()) {
        const auto newUuid = dust3d::Uuid::createUuid();
        Document::Part& part = partMap[newUuid];
        part.id = newUuid;
        partId = part.id;
        emit partAdded(partId);
        newPartAdded = true;
    } else {
        fromNode = findNode(fromNodeId);
        if (nullptr == fromNode) {
            return dust3d::Uuid();
        }
        partId = fromNode->partId;
        if (isPartReadonly(partId))
            return dust3d::Uuid();
        auto part = partMap.find(partId);
        if (part != partMap.end())
            part->second.dirty = true;
    }
    Document::Node node(nodeId);
    node.partId = partId;
    node.setRadius(radius);
    node.setX(x);
    node.setY(y);
    node.setZ(z);
    nodeMap[node.id] = node;
    partMap[partId].nodeIds.push_back(node.id);

    emit nodeAdded(node.id);

    if (nullptr != fromNode) {
        Document::Edge edge;
        edge.partId = partId;
        edge.nodeIds.push_back(fromNode->id);
        edge.nodeIds.push_back(node.id);
        edgeMap[edge.id] = edge;

        nodeMap[node.id].edgeIds.push_back(edge.id);
        nodeMap[fromNode->id].edgeIds.push_back(edge.id);

        emit edgeAdded(edge.id);
    }

    if (newPartAdded)
        addPartToComponent(partId, m_currentCanvasComponentId);

    emit skeletonChanged();

    return node.id;
}

void Document::joinNodeAndNeiborsToGroup(std::vector<dust3d::Uuid>* group, dust3d::Uuid nodeId, std::set<dust3d::Uuid>* visitMap, dust3d::Uuid noUseEdgeId)
{
    if (nodeId.isNull() || visitMap->find(nodeId) != visitMap->end())
        return;
    const Document::Node* node = findNode(nodeId);
    if (nullptr == node) {
        return;
    }
    visitMap->insert(nodeId);
    group->push_back(nodeId);
    for (auto edgeIt = node->edgeIds.begin(); edgeIt != node->edgeIds.end(); edgeIt++) {
        if (noUseEdgeId == *edgeIt)
            continue;
        const Document::Edge* edge = findEdge(*edgeIt);
        if (nullptr == edge) {
            continue;
        }
        for (auto nodeIt = edge->nodeIds.begin(); nodeIt != edge->nodeIds.end(); nodeIt++) {
            joinNodeAndNeiborsToGroup(group, *nodeIt, visitMap, noUseEdgeId);
        }
    }
}

void Document::splitPartByNode(std::vector<std::vector<dust3d::Uuid>>* groups, dust3d::Uuid nodeId)
{
    const Document::Node* node = findNode(nodeId);
    std::set<dust3d::Uuid> visitMap;
    visitMap.insert(nodeId);
    for (auto edgeIt = node->edgeIds.begin(); edgeIt != node->edgeIds.end(); edgeIt++) {
        std::vector<dust3d::Uuid> group;
        const Document::Edge* edge = findEdge(*edgeIt);
        if (nullptr == edge) {
            continue;
        }
        joinNodeAndNeiborsToGroup(&group, edge->neighborOf(nodeId), &visitMap, *edgeIt);
        if (!group.empty())
            groups->push_back(group);
    }
}

void Document::splitPartByEdge(std::vector<std::vector<dust3d::Uuid>>* groups, dust3d::Uuid edgeId)
{
    const Document::Edge* edge = findEdge(edgeId);
    if (nullptr == edge) {
        return;
    }
    std::set<dust3d::Uuid> visitMap;
    for (auto nodeIt = edge->nodeIds.begin(); nodeIt != edge->nodeIds.end(); nodeIt++) {
        std::vector<dust3d::Uuid> group;
        joinNodeAndNeiborsToGroup(&group, *nodeIt, &visitMap, edgeId);
        if (!group.empty())
            groups->push_back(group);
    }
}

const Document::Component* Document::findComponentParent(dust3d::Uuid componentId) const
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return nullptr;
    }

    if (component->second.parentId.isNull())
        return &rootComponent;

    return (Document::Component*)findComponent(component->second.parentId);
}

dust3d::Uuid Document::findComponentParentId(dust3d::Uuid componentId) const
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return dust3d::Uuid();
    }

    return component->second.parentId;
}

void Document::moveComponentUp(dust3d::Uuid componentId)
{
    Document::Component* parent = (Document::Component*)findComponentParent(componentId);
    if (nullptr == parent)
        return;

    dust3d::Uuid parentId = findComponentParentId(componentId);

    parent->moveChildUp(componentId);
    parent->dirty = true;
    emit componentChildrenChanged(parentId);
    emit skeletonChanged();
}

void Document::moveComponentDown(dust3d::Uuid componentId)
{
    Document::Component* parent = (Document::Component*)findComponentParent(componentId);
    if (nullptr == parent)
        return;

    dust3d::Uuid parentId = findComponentParentId(componentId);

    parent->moveChildDown(componentId);
    parent->dirty = true;
    emit componentChildrenChanged(parentId);
    emit skeletonChanged();
}

void Document::moveComponentToTop(dust3d::Uuid componentId)
{
    Document::Component* parent = (Document::Component*)findComponentParent(componentId);
    if (nullptr == parent)
        return;

    dust3d::Uuid parentId = findComponentParentId(componentId);

    parent->moveChildToTop(componentId);
    parent->dirty = true;
    emit componentChildrenChanged(parentId);
    emit skeletonChanged();
}

void Document::moveComponentToBottom(dust3d::Uuid componentId)
{
    Document::Component* parent = (Document::Component*)findComponentParent(componentId);
    if (nullptr == parent)
        return;

    dust3d::Uuid parentId = findComponentParentId(componentId);

    parent->moveChildToBottom(componentId);
    parent->dirty = true;
    emit componentChildrenChanged(parentId);
    emit skeletonChanged();
}

void Document::renameComponent(dust3d::Uuid componentId, QString name)
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return;
    }

    if (component->second.name == name)
        return;

    if (!name.trimmed().isEmpty())
        component->second.name = name;
    emit componentNameChanged(componentId);
    emit optionsChanged();
}

void Document::setComponentExpandState(dust3d::Uuid componentId, bool expanded)
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return;
    }

    if (component->second.expanded == expanded)
        return;

    component->second.expanded = expanded;
    emit componentExpandStateChanged(componentId);
    emit optionsChanged();
}

void Document::ungroupComponent(const dust3d::Uuid& componentId)
{
    if (componentId.isNull())
        return;
    Document::Component* component = (Document::Component*)findComponent(componentId);
    if (nullptr == component) {
        qDebug() << "Component not found:" << componentId.toString();
        return;
    }
    if (component->childrenIds.empty())
        return;
    auto childrenIds = component->childrenIds;
    Document::Component* newParent = (Document::Component*)findComponentParent(componentId);
    if (nullptr == newParent) {
        qDebug() << "Expected parent component to be found, component:" << componentId.toString();
        return;
    }
    auto newParentId = newParent->id;
    newParent->replaceChildWithOthers(componentId, childrenIds);
    for (const auto& childId : childrenIds) {
        Document::Component* child = (Document::Component*)findComponent(childId);
        if (nullptr == child)
            continue;
        child->parentId = newParentId;
    }
    componentMap.erase(componentId);
    emit componentRemoved(componentId);
    emit componentChildrenChanged(newParentId);
    emit skeletonChanged();
}

void Document::groupComponents(const std::vector<dust3d::Uuid>& componentIds)
{
    if (componentIds.empty())
        return;

    dust3d::Uuid newParentId;

    Document::Component newParent(dust3d::Uuid::createUuid());
    newParentId = newParent.id;

    auto it = componentIds.begin();

    Document::Component* oldParent = (Document::Component*)findComponentParent(*it);
    if (nullptr == oldParent) {
        qDebug() << "Expected parent component to be found, component:" << it->toString();
        return;
    }
    auto oldParentId = oldParent->id;
    oldParent->replaceChild(*it, newParentId);
    for (++it; it != componentIds.end(); ++it) {
        oldParent->removeChild(*it);
    }

    for (const auto& componentId : componentIds) {
        auto component = componentMap.find(componentId);
        if (component == componentMap.end()) {
            continue;
        }
        component->second.parentId = newParentId;
        newParent.addChild(componentId);
    }

    newParent.parentId = oldParentId;
    newParent.name = tr("Group") + " " + QString::number(componentMap.size() - partMap.size() + 1);
    componentMap.emplace(newParentId, std::move(newParent));

    emit componentChildrenChanged(oldParentId);
    emit componentAdded(newParentId);
    emit skeletonChanged();
}

void Document::createNewChildComponent(dust3d::Uuid parentComponentId)
{
    Document::Component* parentComponent = (Document::Component*)findComponent(parentComponentId);
    if (!parentComponent->linkToPartId.isNull()) {
        parentComponentId = parentComponent->parentId;
        parentComponent = (Document::Component*)findComponent(parentComponentId);
    }

    Document::Component newComponent(dust3d::Uuid::createUuid());
    newComponent.name = tr("Group") + " " + QString::number(componentMap.size() - partMap.size() + 1);

    parentComponent->addChild(newComponent.id);
    newComponent.parentId = parentComponentId;

    auto newComponentId = newComponent.id;
    componentMap.emplace(newComponentId, std::move(newComponent));

    emit componentChildrenChanged(parentComponentId);
    emit componentAdded(newComponentId);
    emit optionsChanged();
}

void Document::removePart(dust3d::Uuid partId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }

    if (!part->second.componentId.isNull()) {
        removeComponent(part->second.componentId);
        return;
    }

    removePartDontCareComponent(partId);
}

void Document::removePartDontCareComponent(dust3d::Uuid partId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }

    std::vector<dust3d::Uuid> removedNodeIds;
    std::vector<dust3d::Uuid> removedEdgeIds;

    for (auto nodeIt = nodeMap.begin(); nodeIt != nodeMap.end();) {
        if (nodeIt->second.partId != partId) {
            nodeIt++;
            continue;
        }
        removedNodeIds.push_back(nodeIt->second.id);
        nodeIt = nodeMap.erase(nodeIt);
    }

    for (auto edgeIt = edgeMap.begin(); edgeIt != edgeMap.end();) {
        if (edgeIt->second.partId != partId) {
            edgeIt++;
            continue;
        }
        removedEdgeIds.push_back(edgeIt->second.id);
        edgeIt = edgeMap.erase(edgeIt);
    }

    partMap.erase(part);

    for (const auto& nodeId : removedNodeIds) {
        emit nodeRemoved(nodeId);
    }
    for (const auto& edgeId : removedEdgeIds) {
        emit edgeRemoved(edgeId);
    }
    emit partRemoved(partId);
}

void Document::addPartToComponent(dust3d::Uuid partId, dust3d::Uuid componentId)
{
    Document::Component child(dust3d::Uuid::createUuid());

    if (!componentId.isNull()) {
        auto parentComponent = componentMap.find(componentId);
        if (parentComponent == componentMap.end()) {
            componentId = dust3d::Uuid();
            rootComponent.addChild(child.id);
        } else {
            parentComponent->second.addChild(child.id);
        }
    } else {
        rootComponent.addChild(child.id);
    }

    partMap[partId].componentId = child.id;
    child.linkToPartId = partId;
    child.parentId = componentId;
    auto childId = child.id;
    componentMap.emplace(childId, std::move(child));

    emit componentChildrenChanged(componentId);
    emit componentAdded(childId);
}

void Document::removeComponent(dust3d::Uuid componentId)
{
    removeComponentRecursively(componentId);
    emit skeletonChanged();
}

void Document::removeComponentRecursively(dust3d::Uuid componentId)
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return;
    }

    if (!component->second.linkToPartId.isNull()) {
        removePartDontCareComponent(component->second.linkToPartId);
    }

    auto childrenIds = component->second.childrenIds;
    for (const auto& childId : childrenIds) {
        removeComponentRecursively(childId);
    }

    dust3d::Uuid parentId = component->second.parentId;
    if (!parentId.isNull()) {
        auto parentComponent = componentMap.find(parentId);
        if (parentComponent != componentMap.end()) {
            parentComponent->second.dirty = true;
            parentComponent->second.removeChild(componentId);
        }
    } else {
        rootComponent.removeChild(componentId);
    }

    componentMap.erase(component);
    emit componentRemoved(componentId);
    emit componentChildrenChanged(parentId);
}

void Document::setCurrentCanvasComponentId(dust3d::Uuid componentId)
{
    m_currentCanvasComponentId = componentId;
    const Document::Component* component = findComponent(m_currentCanvasComponentId);
    if (nullptr == component) {
        m_currentCanvasComponentId = dust3d::Uuid();
    } else {
        if (!component->linkToPartId.isNull()) {
            m_currentCanvasComponentId = component->parentId;
            component = findComponent(m_currentCanvasComponentId);
        }
    }
}

void Document::addComponent(dust3d::Uuid parentId)
{
    Document::Component component(dust3d::Uuid::createUuid());

    if (!parentId.isNull()) {
        auto parentComponent = componentMap.find(parentId);
        if (parentComponent == componentMap.end()) {
            return;
        }
        parentComponent->second.addChild(component.id);
    } else {
        rootComponent.addChild(component.id);
    }

    component.parentId = parentId;
    auto componentId = component.id;
    componentMap.emplace(componentId, std::move(component));

    emit componentChildrenChanged(parentId);
    emit componentAdded(componentId);
}

bool Document::isDescendantComponent(dust3d::Uuid componentId, dust3d::Uuid suspiciousId)
{
    const Document::Component* loopComponent = findComponentParent(suspiciousId);
    while (nullptr != loopComponent) {
        if (loopComponent->id == componentId)
            return true;
        loopComponent = findComponentParent(loopComponent->parentId);
    }
    return false;
}

void Document::moveComponent(dust3d::Uuid componentId, dust3d::Uuid toParentId)
{
    if (componentId == toParentId)
        return;

    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        return;
    }

    if (component->second.parentId == toParentId)
        return;

    if (isDescendantComponent(componentId, toParentId))
        return;

    if (component->second.parentId.isNull()) {
        rootComponent.removeChild(componentId);
        emit componentChildrenChanged(rootComponent.id);
    } else {
        auto oldParent = componentMap.find(component->second.parentId);
        if (oldParent != componentMap.end()) {
            oldParent->second.dirty = true;
            oldParent->second.removeChild(componentId);
            emit componentChildrenChanged(oldParent->second.id);
        }
    }

    component->second.parentId = toParentId;

    if (toParentId.isNull()) {
        rootComponent.addChild(componentId);
        emit componentChildrenChanged(rootComponent.id);
    } else {
        auto newParent = componentMap.find(toParentId);
        if (newParent != componentMap.end()) {
            newParent->second.dirty = true;
            newParent->second.addChild(componentId);
            emit componentChildrenChanged(newParent->second.id);
        }
    }

    emit skeletonChanged();
}

void Document::setPartLockState(dust3d::Uuid partId, bool locked)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }
    if (part->second.locked == locked)
        return;
    part->second.locked = locked;
    emit partLockStateChanged(partId);
    emit optionsChanged();
}

void Document::setPartVisibleState(dust3d::Uuid partId, bool visible)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }
    if (part->second.visible == visible)
        return;
    part->second.visible = visible;
    emit partVisibleStateChanged(partId);
    emit optionsChanged();
}

void Document::setPartDisableState(dust3d::Uuid partId, bool disabled)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }
    if (part->second.disabled == disabled)
        return;
    part->second.disabled = disabled;
    part->second.dirty = true;
    emit partDisableStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartColorImage(const dust3d::Uuid& partId, const dust3d::Uuid& imageId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        return;
    }
    if (part->second.colorImageId == imageId)
        return;
    part->second.colorImageId = imageId;
    part->second.dirty = true;
    auto component = componentMap.find(part->second.componentId);
    if (component != componentMap.end())
        component->second.isPreviewMeshObsolete = true;
    emit partColorImageChanged(partId);
    emit textureChanged();
}

void Document::collectComponentDescendantParts(dust3d::Uuid componentId, std::vector<dust3d::Uuid>& partIds) const
{
    const Document::Component* component = findComponent(componentId);
    if (nullptr == component)
        return;

    if (!component->linkToPartId.isNull()) {
        partIds.push_back(component->linkToPartId);
        return;
    }

    for (const auto& childId : component->childrenIds) {
        collectComponentDescendantParts(childId, partIds);
    }
}

void Document::collectComponentDescendantComponents(dust3d::Uuid componentId, std::vector<dust3d::Uuid>& componentIds) const
{
    const Document::Component* component = findComponent(componentId);
    if (nullptr == component)
        return;

    if (!component->linkToPartId.isNull()) {
        return;
    }

    for (const auto& childId : component->childrenIds) {
        componentIds.push_back(childId);
        collectComponentDescendantComponents(childId, componentIds);
    }
}

void Document::hideOtherComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    std::set<dust3d::Uuid> partIdSet;
    for (const auto& partId : partIds) {
        partIdSet.insert(partId);
    }
    for (const auto& part : partMap) {
        if (partIdSet.find(part.first) != partIdSet.end())
            continue;
        setPartVisibleState(part.first, false);
    }
}

void Document::lockOtherComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    std::set<dust3d::Uuid> partIdSet;
    for (const auto& partId : partIds) {
        partIdSet.insert(partId);
    }
    for (const auto& part : partMap) {
        if (partIdSet.find(part.first) != partIdSet.end())
            continue;
        setPartLockState(part.first, true);
    }
}

void Document::hideAllComponents()
{
    for (const auto& part : partMap) {
        setPartVisibleState(part.first, false);
    }
}

void Document::showAllComponents()
{
    for (const auto& part : partMap) {
        setPartVisibleState(part.first, true);
    }
}

void Document::showOrHideAllComponents()
{
    bool foundVisiblePart = false;
    for (const auto& part : partMap) {
        if (part.second.visible) {
            foundVisiblePart = true;
        }
    }
    if (foundVisiblePart)
        hideAllComponents();
    else
        showAllComponents();
}

void Document::collapseAllComponents()
{
    for (const auto& component : componentMap) {
        if (!component.second.linkToPartId.isNull())
            continue;
        setComponentExpandState(component.first, false);
    }
}

void Document::expandAllComponents()
{
    for (const auto& component : componentMap) {
        if (!component.second.linkToPartId.isNull())
            continue;
        setComponentExpandState(component.first, true);
    }
}

void Document::lockAllComponents()
{
    for (const auto& part : partMap) {
        setPartLockState(part.first, true);
    }
}

void Document::unlockAllComponents()
{
    for (const auto& part : partMap) {
        setPartLockState(part.first, false);
    }
}

void Document::hideDescendantComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    for (const auto& partId : partIds) {
        setPartVisibleState(partId, false);
    }
}

void Document::showDescendantComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    for (const auto& partId : partIds) {
        setPartVisibleState(partId, true);
    }
}

void Document::lockDescendantComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    for (const auto& partId : partIds) {
        setPartLockState(partId, true);
    }
}

void Document::unlockDescendantComponents(dust3d::Uuid componentId)
{
    std::vector<dust3d::Uuid> partIds;
    collectComponentDescendantParts(componentId, partIds);
    for (const auto& partId : partIds) {
        setPartLockState(partId, false);
    }
}

void Document::scaleNodeByAddRadius(dust3d::Uuid nodeId, float amount)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if (isPartReadonly(it->second.partId))
        return;
    if (radiusLocked)
        return;
    it->second.setRadius(it->second.radius + amount);
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeRadiusChanged(nodeId);
    emit skeletonChanged();
}

bool Document::isPartReadonly(dust3d::Uuid partId) const
{
    const Document::Part* part = findPart(partId);
    if (nullptr == part) {
        return true;
    }
    return part->locked || !part->visible;
}

void Document::moveNodeBy(dust3d::Uuid nodeId, float x, float y, float z)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if (m_allPositionRelatedLocksEnabled && isPartReadonly(it->second.partId))
        return;
    bool changed = false;
    if (!(m_allPositionRelatedLocksEnabled && xlocked)) {
        it->second.addX(x);
        changed = true;
    }
    if (!(m_allPositionRelatedLocksEnabled && ylocked)) {
        it->second.addY(y);
        changed = true;
    }
    if (!(m_allPositionRelatedLocksEnabled && zlocked)) {
        it->second.addZ(z);
        changed = true;
    }
    if (!changed)
        return;
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeOriginChanged(nodeId);
    emit skeletonChanged();
}

void Document::moveOriginBy(float x, float y, float z)
{
    if (!(m_allPositionRelatedLocksEnabled && xlocked))
        addOriginX(x);
    if (!(m_allPositionRelatedLocksEnabled && ylocked))
        addOriginY(y);
    if (!(m_allPositionRelatedLocksEnabled && zlocked))
        addOriginZ(z);
    markAllDirty();
    emit originChanged();
    emit skeletonChanged();
}

void Document::setNodeOrigin(dust3d::Uuid nodeId, float x, float y, float z)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if ((m_allPositionRelatedLocksEnabled && isPartReadonly(it->second.partId)))
        return;
    if (!(m_allPositionRelatedLocksEnabled && xlocked))
        it->second.setX(x);
    if (!(m_allPositionRelatedLocksEnabled && ylocked))
        it->second.setY(y);
    if (!(m_allPositionRelatedLocksEnabled && zlocked))
        it->second.setZ(z);
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeOriginChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeRadius(dust3d::Uuid nodeId, float radius)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if (isPartReadonly(it->second.partId))
        return;
    if (!radiusLocked)
        it->second.setRadius(radius);
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeRadiusChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeBoneJointState(const dust3d::Uuid& nodeId, bool boneJoint)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if (isPartReadonly(it->second.partId))
        return;
    if (it->second.boneJoint == boneJoint)
        return;
    it->second.boneJoint = boneJoint;
    emit nodeBoneJointStateChanged(nodeId);
    emit rigChanged();
}

void Document::switchNodeXZ(dust3d::Uuid nodeId)
{
    auto it = nodeMap.find(nodeId);
    if (it == nodeMap.end()) {
        return;
    }
    if (isPartReadonly(it->second.partId))
        return;
    {
        float oldX = it->second.getX();
        float oldZ = it->second.getZ();
        it->second.setX(oldZ);
        it->second.setZ(oldX);
    }
    auto part = partMap.find(it->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeOriginChanged(nodeId);
    emit skeletonChanged();
}

const Document::Component* Document::findComponent(dust3d::Uuid componentId) const
{
    if (componentId.isNull())
        return &rootComponent;
    auto it = componentMap.find(componentId);
    if (it == componentMap.end())
        return nullptr;
    return &it->second;
}

void Document::addEdge(dust3d::Uuid fromNodeId, dust3d::Uuid toNodeId)
{
    if (findEdgeByNodes(fromNodeId, toNodeId)) {
        return;
    }

    const Document::Node* fromNode = nullptr;
    const Document::Node* toNode = nullptr;
    bool toPartRemoved = false;

    fromNode = findNode(fromNodeId);
    if (nullptr == fromNode) {
        return;
    }

    if (isPartReadonly(fromNode->partId))
        return;

    toNode = findNode(toNodeId);
    if (nullptr == toNode) {
        return;
    }

    if (isPartReadonly(toNode->partId))
        return;

    dust3d::Uuid toPartId = toNode->partId;

    auto fromPart = partMap.find(fromNode->partId);
    if (fromPart == partMap.end()) {
        return;
    }

    fromPart->second.dirty = true;

    if (fromNode->partId != toNode->partId) {
        toPartRemoved = true;
        std::vector<dust3d::Uuid> toGroup;
        std::set<dust3d::Uuid> visitMap;
        joinNodeAndNeiborsToGroup(&toGroup, toNodeId, &visitMap);
        for (auto nodeIdIt = toGroup.begin(); nodeIdIt != toGroup.end(); nodeIdIt++) {
            auto nodeIt = nodeMap.find(*nodeIdIt);
            if (nodeIt == nodeMap.end()) {
                continue;
            }
            nodeIt->second.partId = fromNode->partId;
            fromPart->second.nodeIds.push_back(nodeIt->first);
            for (auto edgeIdIt = nodeIt->second.edgeIds.begin(); edgeIdIt != nodeIt->second.edgeIds.end(); edgeIdIt++) {
                auto edgeIt = edgeMap.find(*edgeIdIt);
                if (edgeIt == edgeMap.end()) {
                    continue;
                }
                edgeIt->second.partId = fromNode->partId;
            }
        }
    }

    Document::Edge edge;
    edge.partId = fromNode->partId;
    edge.nodeIds.push_back(fromNode->id);
    edge.nodeIds.push_back(toNodeId);
    edgeMap[edge.id] = edge;

    nodeMap[toNodeId].edgeIds.push_back(edge.id);
    nodeMap[fromNode->id].edgeIds.push_back(edge.id);

    emit edgeAdded(edge.id);

    if (toPartRemoved) {
        updateLinkedPart(toPartId, fromNode->partId);
        removePart(toPartId);
    }

    emit skeletonChanged();
}

void Document::updateLinkedPart(dust3d::Uuid oldPartId, dust3d::Uuid newPartId)
{
    for (auto& partIt : partMap) {
        if (partIt.second.cutFaceLinkedId == oldPartId) {
            partIt.second.dirty = true;
            partIt.second.setCutFaceLinkedId(newPartId);
        }
    }
    std::set<dust3d::Uuid> dirtyPartIds;
    for (auto& nodeIt : nodeMap) {
        if (nodeIt.second.cutFaceLinkedId == oldPartId) {
            dirtyPartIds.insert(nodeIt.second.partId);
            nodeIt.second.setCutFaceLinkedId(newPartId);
        }
    }
    for (const auto& partId : dirtyPartIds) {
        Document::Part* part = (Document::Part*)findPart(partId);
        if (nullptr == part)
            continue;
        part->dirty = true;
    }
}

void Document::enableAllPositionRelatedLocks()
{
    m_allPositionRelatedLocksEnabled = true;
}

void Document::disableAllPositionRelatedLocks()
{
    m_allPositionRelatedLocksEnabled = false;
}

void Document::resetDirtyFlags()
{
    for (auto& part : partMap) {
        part.second.dirty = false;
    }
    for (auto& component : componentMap) {
        component.second.dirty = false;
    }
}

void Document::markAllDirty()
{
    for (auto& part : partMap) {
        part.second.dirty = true;
    }
}

void Document::setXlockState(bool locked)
{
    if (xlocked == locked)
        return;
    xlocked = locked;
    emit xlockStateChanged();
}

void Document::setYlockState(bool locked)
{
    if (ylocked == locked)
        return;
    ylocked = locked;
    emit ylockStateChanged();
}

void Document::setZlockState(bool locked)
{
    if (zlocked == locked)
        return;
    zlocked = locked;
    emit zlockStateChanged();
}

void Document::setRadiusLockState(bool locked)
{
    if (radiusLocked == locked)
        return;
    radiusLocked = locked;
    emit radiusLockStateChanged();
}

void Document::setComponentPreviewImage(const dust3d::Uuid& componentId, std::unique_ptr<QImage> image)
{
    Document::Component* component = (Document::Component*)findComponent(componentId);
    if (nullptr == component)
        return;
    component->isPreviewImageDecorationObsolete = true;
    component->previewImage = std::move(image);
}

void Document::setComponentPreviewPixmap(const dust3d::Uuid& componentId, const QPixmap& pixmap)
{
    Document::Component* component = (Document::Component*)findComponent(componentId);
    if (nullptr == component)
        return;
    component->previewPixmap = pixmap;
    emit componentPreviewPixmapChanged(componentId);
}

void Document::setComponentPreviewMesh(const dust3d::Uuid& componentId, std::unique_ptr<ModelMesh> mesh)
{
    Document::Component* component = (Document::Component*)findComponent(componentId);
    if (nullptr == component)
        return;
    component->updatePreviewMesh(std::move(mesh));
    emit componentPreviewMeshChanged(componentId);
}

void Document::uiReady()
{
    qDebug() << "uiReady";
    emit editModeChanged();
}

bool Document::originSettled() const
{
    return !qFuzzyIsNull(getOriginX()) && !qFuzzyIsNull(getOriginY()) && !qFuzzyIsNull(getOriginZ());
}

void Document::setNodeCutRotation(dust3d::Uuid nodeId, float cutRotation)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (qFuzzyCompare(cutRotation, node->second.cutRotation))
        return;
    node->second.setCutRotation(cutRotation);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutRotationChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeCutFace(dust3d::Uuid nodeId, dust3d::CutFace cutFace)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (node->second.cutFace == cutFace)
        return;
    node->second.setCutFace(cutFace);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::setNodeCutFaceLinkedId(dust3d::Uuid nodeId, dust3d::Uuid linkedId)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (node->second.cutFace == dust3d::CutFace::UserDefined && node->second.cutFaceLinkedId == linkedId)
        return;
    node->second.setCutFaceLinkedId(linkedId);
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::clearNodeCutFaceSettings(dust3d::Uuid nodeId)
{
    auto node = nodeMap.find(nodeId);
    if (node == nodeMap.end()) {
        qDebug() << "Node not found:" << nodeId;
        return;
    }
    if (!node->second.hasCutFaceSettings)
        return;
    node->second.clearCutFaceSettings();
    auto part = partMap.find(node->second.partId);
    if (part != partMap.end())
        part->second.dirty = true;
    emit nodeCutFaceChanged(nodeId);
    emit skeletonChanged();
}

void Document::updateTurnaround(const QImage& image)
{
    turnaround = image;
    turnaroundPngByteArray.clear();
    QBuffer pngBuffer(&turnaroundPngByteArray);
    pngBuffer.open(QIODevice::WriteOnly);
    turnaround.save(&pngBuffer, "PNG");
    emit turnaroundChanged();
}

void Document::clearTurnaround()
{
    turnaround = QImage();
    turnaroundPngByteArray.clear();
    emit turnaroundChanged();
}

void Document::updateTextureImage(QImage* image)
{
    delete textureImageByteArray;
    textureImageByteArray = nullptr;

    delete textureImage;
    textureImage = image;
}

void Document::updateTextureNormalImage(QImage* image)
{
    delete textureNormalImageByteArray;
    textureNormalImageByteArray = nullptr;

    delete textureNormalImage;
    textureNormalImage = image;
}

void Document::updateTextureMetalnessImage(QImage* image)
{
    delete textureMetalnessImageByteArray;
    textureMetalnessImageByteArray = nullptr;

    delete textureMetalnessImage;
    textureMetalnessImage = image;
}

void Document::updateTextureRoughnessImage(QImage* image)
{
    delete textureRoughnessImageByteArray;
    textureRoughnessImageByteArray = nullptr;

    delete textureRoughnessImage;
    textureRoughnessImage = image;
}

void Document::updateTextureAmbientOcclusionImage(QImage* image)
{
    delete textureAmbientOcclusionImageByteArray;
    textureAmbientOcclusionImageByteArray = nullptr;

    delete textureAmbientOcclusionImage;
    textureAmbientOcclusionImage = image;
}

void Document::setEditMode(Document::EditMode mode)
{
    if (editMode == mode)
        return;

    if (EditMode::Pick == editMode)
        resetCurrentBone();

    editMode = mode;
    emit editModeChanged();
}

void Document::toSnapshot(dust3d::Snapshot* snapshot, const std::set<dust3d::Uuid>& limitNodeIds,
    Document::SnapshotFor forWhat) const
{
    if (static_cast<unsigned int>(Document::SnapshotFor::Nodes) & static_cast<unsigned int>(forWhat)) {
        std::set<dust3d::Uuid> limitPartIds;
        std::set<dust3d::Uuid> limitComponentIds;
        for (const auto& nodeId : limitNodeIds) {
            const Document::Node* node = findNode(nodeId);
            if (!node)
                continue;
            const Document::Part* part = findPart(node->partId);
            if (!part)
                continue;
            limitPartIds.insert(node->partId);
            const Document::Component* component = findComponent(part->componentId);
            while (component) {
                limitComponentIds.insert(component->id);
                if (component->id.isNull())
                    break;
                component = findComponent(component->parentId);
            }
        }
        for (const auto& partIt : partMap) {
            if (!limitPartIds.empty() && limitPartIds.find(partIt.first) == limitPartIds.end())
                continue;
            std::map<std::string, std::string> part;
            part["id"] = partIt.second.id.toString();
            part["visible"] = partIt.second.visible ? "true" : "false";
            part["locked"] = partIt.second.locked ? "true" : "false";
            part["subdived"] = partIt.second.subdived ? "true" : "false";
            part["disabled"] = partIt.second.disabled ? "true" : "false";
            part["xMirrored"] = partIt.second.xMirrored ? "true" : "false";
            part["rounded"] = partIt.second.rounded ? "true" : "false";
            part["chamfered"] = partIt.second.chamfered ? "true" : "false";
            if (dust3d::PartTarget::Model != partIt.second.target)
                part["target"] = PartTargetToString(partIt.second.target);
            if (partIt.second.cutRotationAdjusted())
                part["cutRotation"] = std::to_string(partIt.second.cutRotation);
            if (partIt.second.cutFaceAdjusted()) {
                if (dust3d::CutFace::UserDefined == partIt.second.cutFace) {
                    if (!partIt.second.cutFaceLinkedId.isNull()) {
                        part["cutFace"] = partIt.second.cutFaceLinkedId.toString();
                    }
                } else {
                    part["cutFace"] = CutFaceToString(partIt.second.cutFace);
                }
            }
            if (!partIt.second.colorImageId.isNull()) {
                part["colorImageId"] = partIt.second.colorImageId.toString();
            }
            part["__dirty"] = partIt.second.dirty ? "true" : "false";
            if (partIt.second.hasColor)
                part["color"] = partIt.second.color.name(QColor::HexArgb).toUtf8().constData();
            if (partIt.second.colorSolubilityAdjusted())
                part["colorSolubility"] = std::to_string(partIt.second.colorSolubility);
            if (partIt.second.metalnessAdjusted())
                part["metallic"] = std::to_string(partIt.second.metalness);
            if (partIt.second.roughnessAdjusted())
                part["roughness"] = std::to_string(partIt.second.roughness);
            if (partIt.second.deformThicknessAdjusted())
                part["deformThickness"] = std::to_string(partIt.second.deformThickness);
            if (partIt.second.deformWidthAdjusted())
                part["deformWidth"] = std::to_string(partIt.second.deformWidth);
            if (partIt.second.deformUnified)
                part["deformUnified"] = "true";
            if (partIt.second.hollowThicknessAdjusted())
                part["hollowThickness"] = std::to_string(partIt.second.hollowThickness);
            if (!partIt.second.name.isEmpty())
                part["name"] = partIt.second.name.toUtf8().constData();
            if (partIt.second.countershaded)
                part["countershaded"] = "true";
            if (partIt.second.smoothCutoffDegrees > 0)
                part["smoothCutoffDegrees"] = std::to_string(partIt.second.smoothCutoffDegrees);
            snapshot->parts[part["id"]] = part;
        }
        for (const auto& nodeIt : nodeMap) {
            if (!limitNodeIds.empty() && limitNodeIds.find(nodeIt.first) == limitNodeIds.end())
                continue;
            std::map<std::string, std::string> node;
            node["id"] = nodeIt.second.id.toString();
            node["radius"] = std::to_string(nodeIt.second.radius);
            node["x"] = std::to_string(nodeIt.second.getX());
            node["y"] = std::to_string(nodeIt.second.getY());
            node["z"] = std::to_string(nodeIt.second.getZ());
            node["partId"] = nodeIt.second.partId.toString();
            if (nodeIt.second.hasCutFaceSettings) {
                node["cutRotation"] = std::to_string(nodeIt.second.cutRotation);
                if (dust3d::CutFace::UserDefined == nodeIt.second.cutFace) {
                    if (!nodeIt.second.cutFaceLinkedId.isNull()) {
                        node["cutFace"] = nodeIt.second.cutFaceLinkedId.toString();
                    }
                } else {
                    node["cutFace"] = CutFaceToString(nodeIt.second.cutFace);
                }
            }
            if (!nodeIt.second.name.isEmpty())
                node["name"] = nodeIt.second.name.toUtf8().constData();
            std::vector<std::string> nodeBoneIdList;
            for (const auto& boneId : nodeIt.second.boneIds) {
                nodeBoneIdList.push_back(boneId.toString());
            }
            std::string boneIds = dust3d::String::join(nodeBoneIdList, ",");
            if (!boneIds.empty())
                node["boneIdList"] = boneIds;
            snapshot->nodes[node["id"]] = node;
        }
        for (const auto& edgeIt : edgeMap) {
            if (edgeIt.second.nodeIds.size() != 2)
                continue;
            if (!limitNodeIds.empty() && (limitNodeIds.find(edgeIt.second.nodeIds[0]) == limitNodeIds.end() || limitNodeIds.find(edgeIt.second.nodeIds[1]) == limitNodeIds.end()))
                continue;
            std::map<std::string, std::string> edge;
            edge["id"] = edgeIt.second.id.toString();
            edge["from"] = edgeIt.second.nodeIds[0].toString();
            edge["to"] = edgeIt.second.nodeIds[1].toString();
            edge["partId"] = edgeIt.second.partId.toString();
            if (!edgeIt.second.name.isEmpty())
                edge["name"] = edgeIt.second.name.toUtf8().constData();
            snapshot->edges[edge["id"]] = edge;
        }
        for (const auto& componentIt : componentMap) {
            if (!limitComponentIds.empty() && limitComponentIds.find(componentIt.first) == limitComponentIds.end())
                continue;
            std::map<std::string, std::string> component;
            component["id"] = componentIt.second.id.toString();
            if (!componentIt.second.name.isEmpty())
                component["name"] = componentIt.second.name.toUtf8().constData();
            component["expanded"] = componentIt.second.expanded ? "true" : "false";
            component["combineMode"] = CombineModeToString(componentIt.second.combineMode);
            component["__dirty"] = componentIt.second.dirty ? "true" : "false";
            std::vector<std::string> childIdList;
            for (const auto& childId : componentIt.second.childrenIds) {
                childIdList.push_back(childId.toString());
            }
            std::string children = dust3d::String::join(childIdList, ",");
            if (!children.empty())
                component["children"] = children;
            std::string linkData = componentIt.second.linkData().toUtf8().constData();
            if (!linkData.empty()) {
                component["linkData"] = linkData;
                component["linkDataType"] = componentIt.second.linkDataType().toUtf8().constData();
            }
            if (!componentIt.second.name.isEmpty())
                component["name"] = componentIt.second.name.toUtf8().constData();
            snapshot->components[component["id"]] = component;
        }
        if (limitComponentIds.empty() || limitComponentIds.find(dust3d::Uuid()) != limitComponentIds.end()) {
            std::vector<std::string> childIdList;
            for (const auto& childId : rootComponent.childrenIds) {
                childIdList.push_back(childId.toString());
            }
            std::string children = dust3d::String::join(childIdList, ",");
            if (!children.empty())
                snapshot->rootComponent["children"] = children;
        }
    }
    if (static_cast<unsigned int>(Document::SnapshotFor::Bones) & static_cast<unsigned int>(forWhat)) {
        if (!boneIdList.empty()) {
            for (const auto& boneId : boneIdList) {
                snapshot->boneIdList.emplace_back(boneId.toString());
            }
            for (const auto& boneIt : boneMap) {
                std::map<std::string, std::string> bone;
                bone["id"] = boneIt.second.id.toString();
                if (!boneIt.second.attachBoneId.isNull()) {
                    bone["attachBoneId"] = boneIt.second.attachBoneId.toString();
                    bone["attachBoneJointIndex"] = std::to_string(boneIt.second.attachBoneJointIndex);
                }
                if (!boneIt.second.name.isEmpty())
                    bone["name"] = boneIt.second.name.toUtf8().constData();
                std::vector<std::string> nodeIdList;
                for (const auto& nodeId : boneIt.second.joints) {
                    nodeIdList.push_back(nodeId.toString());
                }
                std::string nodeIds = dust3d::String::join(nodeIdList, ",");
                if (!nodeIds.empty())
                    bone["jointNodeIdList"] = nodeIds;
                snapshot->bones[bone["id"]] = bone;
            }
        }
    }
    if (Document::SnapshotFor::Document == forWhat) {
        std::map<std::string, std::string> canvas;
        canvas["originX"] = std::to_string(getOriginX());
        canvas["originY"] = std::to_string(getOriginY());
        canvas["originZ"] = std::to_string(getOriginZ());
        snapshot->canvas = canvas;
    }
}

void Document::addFromSnapshot(const dust3d::Snapshot& snapshot, enum SnapshotSource source)
{
    bool isOriginChanged = false;
    if (SnapshotSource::Paste != source && SnapshotSource::Import != source) {
        const auto& originXit = snapshot.canvas.find("originX");
        const auto& originYit = snapshot.canvas.find("originY");
        const auto& originZit = snapshot.canvas.find("originZ");
        if (originXit != snapshot.canvas.end() && originYit != snapshot.canvas.end() && originZit != snapshot.canvas.end()) {
            setOriginX(dust3d::String::toFloat(originXit->second));
            setOriginY(dust3d::String::toFloat(originYit->second));
            setOriginZ(dust3d::String::toFloat(originZit->second));
            isOriginChanged = true;
        }
    }

    std::set<dust3d::Uuid> newAddedNodeIds;
    std::set<dust3d::Uuid> newAddedEdgeIds;
    std::set<dust3d::Uuid> newAddedPartIds;
    std::set<dust3d::Uuid> newAddedComponentIds;
    std::set<dust3d::Uuid> newAddedBoneIds;

    std::set<dust3d::Uuid> inversePartIds;

    std::map<dust3d::Uuid, dust3d::Uuid> oldNewIdMap;
    std::map<dust3d::Uuid, dust3d::Uuid> cutFaceLinkedIdModifyMap;
    for (const auto& partKv : snapshot.parts) {
        const auto newUuid = dust3d::Uuid::createUuid();
        Document::Part& part = partMap[newUuid];
        part.id = newUuid;
        oldNewIdMap[dust3d::Uuid(partKv.first)] = part.id;
        part.name = dust3d::String::valueOrEmpty(partKv.second, "name").c_str();
        const auto& visibleIt = partKv.second.find("visible");
        if (visibleIt != partKv.second.end()) {
            part.visible = dust3d::String::isTrue(visibleIt->second);
        } else {
            part.visible = true;
        }
        part.locked = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "locked"));
        part.subdived = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "subdived"));
        part.disabled = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "disabled"));
        part.xMirrored = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "xMirrored"));
        part.rounded = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "rounded"));
        part.chamfered = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "chamfered"));
        part.target = dust3d::PartTargetFromString(dust3d::String::valueOrEmpty(partKv.second, "target").c_str());
        const auto& cutRotationIt = partKv.second.find("cutRotation");
        if (cutRotationIt != partKv.second.end())
            part.setCutRotation(dust3d::String::toFloat(cutRotationIt->second));
        const auto& cutFaceIt = partKv.second.find("cutFace");
        if (cutFaceIt != partKv.second.end()) {
            dust3d::Uuid cutFaceLinkedId = dust3d::Uuid(cutFaceIt->second);
            if (cutFaceLinkedId.isNull()) {
                part.setCutFace(dust3d::CutFaceFromString(cutFaceIt->second.c_str()));
            } else {
                part.setCutFaceLinkedId(cutFaceLinkedId);
                cutFaceLinkedIdModifyMap.insert({ part.id, cutFaceLinkedId });
            }
        }
        const auto& colorImageIt = partKv.second.find("colorImageId");
        if (colorImageIt != partKv.second.end()) {
            part.colorImageId = dust3d::Uuid(colorImageIt->second);
        }
        if (dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "inverse")))
            inversePartIds.insert(part.id);
        const auto& colorIt = partKv.second.find("color");
        if (colorIt != partKv.second.end()) {
            part.color = QColor(colorIt->second.c_str());
            part.hasColor = true;
        }
        const auto& colorSolubilityIt = partKv.second.find("colorSolubility");
        if (colorSolubilityIt != partKv.second.end())
            part.colorSolubility = dust3d::String::toFloat(colorSolubilityIt->second);
        const auto& metalnessIt = partKv.second.find("metallic");
        if (metalnessIt != partKv.second.end())
            part.metalness = dust3d::String::toFloat(metalnessIt->second);
        const auto& roughnessIt = partKv.second.find("roughness");
        if (roughnessIt != partKv.second.end())
            part.roughness = dust3d::String::toFloat(roughnessIt->second);
        const auto& deformThicknessIt = partKv.second.find("deformThickness");
        if (deformThicknessIt != partKv.second.end())
            part.setDeformThickness(dust3d::String::toFloat(deformThicknessIt->second));
        const auto& deformWidthIt = partKv.second.find("deformWidth");
        if (deformWidthIt != partKv.second.end())
            part.setDeformWidth(dust3d::String::toFloat(deformWidthIt->second));
        const auto& deformUnifiedIt = partKv.second.find("deformUnified");
        if (deformUnifiedIt != partKv.second.end())
            part.deformUnified = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "deformUnified"));
        const auto& hollowThicknessIt = partKv.second.find("hollowThickness");
        if (hollowThicknessIt != partKv.second.end())
            part.hollowThickness = dust3d::String::toFloat(hollowThicknessIt->second);
        part.countershaded = dust3d::String::isTrue(dust3d::String::valueOrEmpty(partKv.second, "countershaded"));
        const auto& smoothCutoffDegreesIt = partKv.second.find("smoothCutoffDegrees");
        if (smoothCutoffDegreesIt != partKv.second.end())
            part.smoothCutoffDegrees = dust3d::String::toFloat(smoothCutoffDegreesIt->second);
        newAddedPartIds.insert(part.id);
    }
    for (const auto& it : cutFaceLinkedIdModifyMap) {
        Document::Part& part = partMap[it.first];
        auto findNewLinkedId = oldNewIdMap.find(it.second);
        if (findNewLinkedId == oldNewIdMap.end()) {
            if (partMap.find(it.second) == partMap.end()) {
                part.setCutFaceLinkedId(dust3d::Uuid());
            }
        } else {
            part.setCutFaceLinkedId(findNewLinkedId->second);
        }
    }
    for (const auto& boneKv : snapshot.bones) {
        Document::Bone bone;
        auto boneId = bone.id;
        oldNewIdMap[dust3d::Uuid(boneKv.first)] = boneId;
        bone.name = dust3d::String::valueOrEmpty(boneKv.second, "name").c_str();
        bone.attachBoneJointIndex = dust3d::String::toInt(dust3d::String::valueOrEmpty(boneKv.second, "attachBoneJointIndex"));
        boneMap.emplace(boneId, std::move(bone));
        newAddedBoneIds.insert(boneId);
    }
    for (const auto& boneIdString : snapshot.boneIdList) {
        boneIdList.push_back(oldNewIdMap[dust3d::Uuid(boneIdString)]);
    }
    for (const auto& boneKv : snapshot.bones) {
        auto attachBoneId = dust3d::Uuid(dust3d::String::valueOrEmpty(boneKv.second, "attachBoneId"));
        if (!attachBoneId.isNull()) {
            boneMap[oldNewIdMap[dust3d::Uuid(boneKv.first)]].attachBoneId = oldNewIdMap[attachBoneId];
        }
    }
    for (const auto& nodeKv : snapshot.nodes) {
        if (nodeKv.second.find("radius") == nodeKv.second.end() || nodeKv.second.find("x") == nodeKv.second.end() || nodeKv.second.find("y") == nodeKv.second.end() || nodeKv.second.find("z") == nodeKv.second.end() || nodeKv.second.find("partId") == nodeKv.second.end())
            continue;
        dust3d::Uuid oldNodeId = dust3d::Uuid(nodeKv.first);
        Document::Node node(nodeMap.find(oldNodeId) == nodeMap.end() ? oldNodeId : dust3d::Uuid::createUuid());
        oldNewIdMap[oldNodeId] = node.id;
        node.name = dust3d::String::valueOrEmpty(nodeKv.second, "name").c_str();
        node.radius = dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeKv.second, "radius"));
        node.setX(dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeKv.second, "x")));
        node.setY(dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeKv.second, "y")));
        node.setZ(dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeKv.second, "z")));
        node.partId = oldNewIdMap[dust3d::Uuid(dust3d::String::valueOrEmpty(nodeKv.second, "partId"))];
        const auto& cutRotationIt = nodeKv.second.find("cutRotation");
        if (cutRotationIt != nodeKv.second.end())
            node.setCutRotation(dust3d::String::toFloat(cutRotationIt->second));
        const auto& cutFaceIt = nodeKv.second.find("cutFace");
        if (cutFaceIt != nodeKv.second.end()) {
            dust3d::Uuid cutFaceLinkedId = dust3d::Uuid(cutFaceIt->second);
            if (cutFaceLinkedId.isNull()) {
                node.setCutFace(dust3d::CutFaceFromString(cutFaceIt->second.c_str()));
            } else {
                node.setCutFaceLinkedId(cutFaceLinkedId);
                auto findNewLinkedId = oldNewIdMap.find(cutFaceLinkedId);
                if (findNewLinkedId == oldNewIdMap.end()) {
                    if (partMap.find(cutFaceLinkedId) == partMap.end()) {
                        node.setCutFaceLinkedId(dust3d::Uuid());
                    }
                } else {
                    node.setCutFaceLinkedId(findNewLinkedId->second);
                }
            }
        }
        for (const auto& boneIdString : dust3d::String::split(dust3d::String::valueOrEmpty(nodeKv.second, "boneIdList"), ',')) {
            if (boneIdString.empty())
                continue;
            node.boneIds.insert(oldNewIdMap[dust3d::Uuid(boneIdString)]);
        }
        nodeMap[node.id] = node;
        newAddedNodeIds.insert(node.id);
    }
    for (const auto& boneKv : snapshot.bones) {
        std::vector<dust3d::Uuid> joints;
        for (const auto& nodeIdString : dust3d::String::split(dust3d::String::valueOrEmpty(boneKv.second, "jointNodeIdList"), ',')) {
            if (nodeIdString.empty())
                continue;
            auto newNodeId = oldNewIdMap[dust3d::Uuid(nodeIdString)];
            nodeMap[newNodeId].boneJoint = true;
            joints.push_back(newNodeId);
        }
        boneMap[oldNewIdMap[dust3d::Uuid(boneKv.first)]].joints = joints;
    }
    for (const auto& edgeKv : snapshot.edges) {
        if (edgeKv.second.find("from") == edgeKv.second.end() || edgeKv.second.find("to") == edgeKv.second.end() || edgeKv.second.find("partId") == edgeKv.second.end())
            continue;
        dust3d::Uuid oldEdgeId = dust3d::Uuid(edgeKv.first);
        Document::Edge edge(edgeMap.find(oldEdgeId) == edgeMap.end() ? oldEdgeId : dust3d::Uuid::createUuid());
        oldNewIdMap[oldEdgeId] = edge.id;
        edge.name = dust3d::String::valueOrEmpty(edgeKv.second, "name").c_str();
        edge.partId = oldNewIdMap[dust3d::Uuid(dust3d::String::valueOrEmpty(edgeKv.second, "partId"))];
        std::string fromNodeId = dust3d::String::valueOrEmpty(edgeKv.second, "from");
        if (!fromNodeId.empty()) {
            dust3d::Uuid fromId = oldNewIdMap[dust3d::Uuid(fromNodeId)];
            edge.nodeIds.push_back(fromId);
            nodeMap[fromId].edgeIds.push_back(edge.id);
        }
        std::string toNodeId = dust3d::String::valueOrEmpty(edgeKv.second, "to");
        if (!toNodeId.empty()) {
            dust3d::Uuid toId = oldNewIdMap[dust3d::Uuid(toNodeId)];
            edge.nodeIds.push_back(toId);
            nodeMap[toId].edgeIds.push_back(edge.id);
        }
        edgeMap[edge.id] = edge;
        newAddedEdgeIds.insert(edge.id);
    }
    for (const auto& nodeIt : nodeMap) {
        if (newAddedNodeIds.find(nodeIt.first) == newAddedNodeIds.end())
            continue;
        partMap[nodeIt.second.partId].nodeIds.push_back(nodeIt.first);
    }
    for (const auto& componentKv : snapshot.components) {
        QString linkData = dust3d::String::valueOrEmpty(componentKv.second, "linkData").c_str();
        QString linkDataType = dust3d::String::valueOrEmpty(componentKv.second, "linkDataType").c_str();
        Document::Component component(dust3d::Uuid(), linkData, linkDataType);
        auto componentId = component.id;
        oldNewIdMap[dust3d::Uuid(componentKv.first)] = componentId;
        component.name = dust3d::String::valueOrEmpty(componentKv.second, "name").c_str();
        component.expanded = dust3d::String::isTrue(dust3d::String::valueOrEmpty(componentKv.second, "expanded"));
        component.combineMode = dust3d::CombineModeFromString(dust3d::String::valueOrEmpty(componentKv.second, "combineMode").c_str());
        if (component.combineMode == dust3d::CombineMode::Normal) {
            if (dust3d::String::isTrue(dust3d::String::valueOrEmpty(componentKv.second, "inverse")))
                component.combineMode = dust3d::CombineMode::Inversion;
        }
        //qDebug() << "Add component:" << component.id << " old:" << componentKv.first << "name:" << component.name;
        if ("partId" == linkDataType) {
            dust3d::Uuid partId = oldNewIdMap[dust3d::Uuid(linkData.toUtf8().constData())];
            component.linkToPartId = partId;
            //qDebug() << "Add part:" << partId << " from component:" << component.id;
            partMap[partId].componentId = componentId;
            if (inversePartIds.find(partId) != inversePartIds.end())
                component.combineMode = dust3d::CombineMode::Inversion;
        }
        componentMap.emplace(componentId, std::move(component));
        newAddedComponentIds.insert(componentId);
    }
    const auto& rootComponentChildren = snapshot.rootComponent.find("children");
    if (rootComponentChildren != snapshot.rootComponent.end()) {
        for (const auto& childId : dust3d::String::split(rootComponentChildren->second, ',')) {
            if (childId.empty())
                continue;
            dust3d::Uuid componentId = oldNewIdMap[dust3d::Uuid(childId)];
            if (componentMap.find(componentId) == componentMap.end())
                continue;
            //qDebug() << "Add root component:" << componentId;
            rootComponent.addChild(componentId);
        }
    }
    for (const auto& componentKv : snapshot.components) {
        dust3d::Uuid componentId = oldNewIdMap[dust3d::Uuid(componentKv.first)];
        if (componentMap.find(componentId) == componentMap.end())
            continue;
        for (const auto& childId : dust3d::String::split(dust3d::String::valueOrEmpty(componentKv.second, "children"), ',')) {
            if (childId.empty())
                continue;
            dust3d::Uuid childComponentId = oldNewIdMap[dust3d::Uuid(childId)];
            if (componentMap.find(childComponentId) == componentMap.end())
                continue;
            //qDebug() << "Add child component:" << childComponentId << "to" << componentId;
            componentMap[componentId].addChild(childComponentId);
            componentMap[childComponentId].parentId = componentId;
        }
    }

    for (const auto& nodeIt : newAddedNodeIds) {
        emit nodeAdded(nodeIt);
    }
    for (const auto& edgeIt : newAddedEdgeIds) {
        emit edgeAdded(edgeIt);
    }
    for (const auto& partIt : newAddedPartIds) {
        emit partAdded(partIt);
    }

    emit componentChildrenChanged(dust3d::Uuid());
    if (isOriginChanged)
        emit originChanged();

    for (const auto& boneIt : newAddedBoneIds) {
        emit boneAdded(boneIt);
    }
    emit boneIdListChanged();

    emit skeletonChanged();

    for (const auto& partIt : newAddedPartIds) {
        emit partVisibleStateChanged(partIt);
    }

    emit uncheckAll();
    for (const auto& nodeIt : newAddedNodeIds) {
        emit checkNode(nodeIt);
    }
    for (const auto& edgeIt : newAddedEdgeIds) {
        emit checkEdge(edgeIt);
    }
}

void Document::silentReset()
{
    setOriginX(0.0);
    setOriginY(0.0);
    setOriginZ(0.0);
    nodeMap.clear();
    edgeMap.clear();
    partMap.clear();
    componentMap.clear();
    rootComponent = Document::Component();
    boneMap.clear();
    boneIdList.clear();
}

void Document::reset()
{
    silentReset();
    emit cleanup();
    emit skeletonChanged();
}

void Document::fromSnapshot(const dust3d::Snapshot& snapshot)
{
    reset();
    addFromSnapshot(snapshot, SnapshotSource::Unknown);
    emit uncheckAll();
}

ModelMesh* Document::takeResultMesh()
{
    if (nullptr == m_resultMesh)
        return nullptr;
    ModelMesh* resultMesh = new ModelMesh(*m_resultMesh);
    return resultMesh;
}

quint64 Document::resultMeshId()
{
    if (nullptr == m_resultMesh)
        return 0;
    return m_resultMesh->meshId();
}

MonochromeMesh* Document::takeWireframeMesh()
{
    if (nullptr == m_wireframeMesh)
        return nullptr;
    return new MonochromeMesh(*m_wireframeMesh);
}

bool Document::isMeshGenerationSucceed()
{
    return m_isMeshGenerationSucceed;
}

ModelMesh* Document::takeResultTextureMesh()
{
    if (nullptr == m_resultTextureMesh)
        return nullptr;
    ModelMesh* resultTextureMesh = new ModelMesh(*m_resultTextureMesh);
    return resultTextureMesh;
}

quint64 Document::resultTextureMeshId()
{
    if (nullptr == m_resultTextureMesh)
        return 0;
    return m_resultTextureMesh->meshId();
}

ModelMesh* Document::takeResultBodyBonePreviewMesh()
{
    if (nullptr == m_resultBodyBonePreviewMesh)
        return nullptr;
    ModelMesh* resultBodyBonePreviewMesh = new ModelMesh(*m_resultBodyBonePreviewMesh);
    return resultBodyBonePreviewMesh;
}

quint64 Document::resultBodyBonePreviewMeshId()
{
    if (nullptr == m_resultBodyBonePreviewMesh)
        return 0;
    return m_resultBodyBonePreviewMesh->meshId();
}

void Document::meshReady()
{
    ModelMesh* resultMesh = m_meshGenerator->takeResultMesh();
    m_wireframeMesh.reset(m_meshGenerator->takeWireframeMesh());
    dust3d::Object* object = m_meshGenerator->takeObject();
    bool isSuccessful = m_meshGenerator->isSuccessful();

    std::unique_ptr<std::map<dust3d::Uuid, std::unique_ptr<ModelMesh>>> componentPreviewMeshes;
    componentPreviewMeshes.reset(m_meshGenerator->takeComponentPreviewMeshes());
    bool componentPreviewsChanged = componentPreviewMeshes && !componentPreviewMeshes->empty();
    if (componentPreviewsChanged) {
        for (auto& it : *componentPreviewMeshes) {
            setComponentPreviewMesh(it.first, std::move(it.second));
        }
        emit resultComponentPreviewMeshesChanged();
    }

    std::unique_ptr<std::map<dust3d::Uuid, std::unique_ptr<QImage>>> componentPreviewImages;
    componentPreviewImages.reset(m_meshGenerator->takeComponentPreviewImages());
    if (componentPreviewImages && !componentPreviewImages->empty()) {
        for (auto& it : *componentPreviewImages) {
            setComponentPreviewImage(it.first, std::move(it.second));
        }
    }

    delete m_resultMesh;
    m_resultMesh = resultMesh;

    m_isMeshGenerationSucceed = isSuccessful;

    delete m_currentObject;
    m_currentObject = object;

    if (nullptr == m_resultMesh) {
        qDebug() << "Result mesh is null";
    }

    delete m_meshGenerator;
    m_meshGenerator = nullptr;

    qDebug() << "Mesh generation done";

    emit resultMeshChanged();

    if (m_isResultMeshObsolete) {
        generateMesh();
    }
}

void Document::batchChangeBegin()
{
    m_batchChangeRefCount++;
}

void Document::batchChangeEnd()
{
    m_batchChangeRefCount--;
    if (0 == m_batchChangeRefCount) {
        if (m_isResultMeshObsolete) {
            generateMesh();
        }
    }
}

void Document::regenerateMesh()
{
    markAllDirty();
    generateMesh();
}

void Document::toggleSmoothNormal()
{
    m_smoothNormal = !m_smoothNormal;
    regenerateMesh();
}

void Document::enableWeld(bool enabled)
{
    weldEnabled = enabled;
    regenerateMesh();
}

void Document::generateMesh()
{
    if (nullptr != m_meshGenerator || m_batchChangeRefCount > 0) {
        m_isResultMeshObsolete = true;
        return;
    }

    emit meshGenerating();

    qDebug() << "Mesh generating..";

    settleOrigin();

    m_isResultMeshObsolete = false;

    QThread* thread = new QThread;

    dust3d::Snapshot* snapshot = new dust3d::Snapshot;
    toSnapshot(snapshot);
    resetDirtyFlags();
    m_meshGenerator = new MeshGenerator(snapshot);
    m_meshGenerator->setId(m_nextMeshGenerationId++);
    m_meshGenerator->setDefaultPartColor(dust3d::Color::createWhite());
    if (nullptr == m_generatedCacheContext)
        m_generatedCacheContext = new MeshGenerator::GeneratedCacheContext;
    m_meshGenerator->setGeneratedCacheContext((dust3d::MeshGenerator::GeneratedCacheContext*)m_generatedCacheContext);
    if (!m_smoothNormal) {
        m_meshGenerator->setSmoothShadingThresholdAngleDegrees(0);
    }
    m_meshGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_meshGenerator, &MeshGenerator::process);
    connect(m_meshGenerator, &MeshGenerator::finished, this, &Document::meshReady);
    connect(m_meshGenerator, &MeshGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::generateTexture()
{
    if (nullptr != m_textureGenerator) {
        m_isTextureObsolete = true;
        return;
    }

    m_isTextureObsolete = false;

    if (nullptr == m_currentObject)
        return;

    qDebug() << "UV mapping generating..";
    emit textureGenerating();

    auto object = std::make_unique<dust3d::Object>(*m_currentObject);

    auto snapshot = std::make_unique<dust3d::Snapshot>();
    toSnapshot(snapshot.get());

    QThread* thread = new QThread;
    m_textureGenerator = new UvMapGenerator(std::move(object), std::move(snapshot));
    m_textureGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_textureGenerator, &UvMapGenerator::process);
    connect(m_textureGenerator, &UvMapGenerator::finished, this, &Document::textureReady);
    connect(m_textureGenerator, &UvMapGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::textureReady()
{
    updateTextureImage(m_textureGenerator->takeResultTextureColorImage().release());
    updateTextureNormalImage(m_textureGenerator->takeResultTextureNormalImage().release());
    updateTextureMetalnessImage(m_textureGenerator->takeResultTextureMetalnessImage().release());
    updateTextureRoughnessImage(m_textureGenerator->takeResultTextureRoughnessImage().release());
    updateTextureAmbientOcclusionImage(m_textureGenerator->takeResultTextureAmbientOcclusionImage().release());

    delete m_resultTextureMesh;
    m_resultTextureMesh = m_textureGenerator->takeResultMesh().release();

    auto object = m_textureGenerator->takeObject();
    if (nullptr != object)
        m_uvMappedObject = std::move(object);
    //m_uvMappedObject->alphaEnabled = m_textureGenerator->hasTransparencySettings();

    m_textureImageUpdateVersion++;

    delete m_textureGenerator;
    m_textureGenerator = nullptr;

    qDebug() << "UV mapping generation done(meshId:" << (nullptr != m_resultTextureMesh ? m_resultTextureMesh->meshId() : 0) << ")";

    emit resultTextureChanged();

    if (m_isTextureObsolete) {
        generateTexture();
    } else {
        checkExportReadyState();
    }
}

quint64 Document::resultTextureImageUpdateVersion()
{
    return m_textureImageUpdateVersion;
}

const dust3d::Object& Document::currentUvMappedObject() const
{
    return *m_uvMappedObject;
}

void Document::setComponentCombineMode(dust3d::Uuid componentId, dust3d::CombineMode combineMode)
{
    auto component = componentMap.find(componentId);
    if (component == componentMap.end()) {
        qDebug() << "Document::Component not found:" << componentId;
        return;
    }
    if (component->second.combineMode == combineMode)
        return;
    component->second.combineMode = combineMode;
    component->second.dirty = true;
    emit componentCombineModeChanged(componentId);
    emit skeletonChanged();
}

void Document::setPartSubdivState(dust3d::Uuid partId, bool subdived)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.subdived == subdived)
        return;
    part->second.subdived = subdived;
    part->second.dirty = true;
    emit partSubdivStateChanged(partId);
    emit skeletonChanged();
}

void Document::resolveSnapshotBoundingBox(const dust3d::Snapshot& snapshot, QRectF* mainProfile, QRectF* sideProfile)
{
    float left = 0;
    bool leftFirstTime = true;
    float right = 0;
    bool rightFirstTime = true;
    float top = 0;
    bool topFirstTime = true;
    float bottom = 0;
    bool bottomFirstTime = true;
    float zLeft = 0;
    bool zLeftFirstTime = true;
    float zRight = 0;
    bool zRightFirstTime = true;
    for (const auto& nodeIt : snapshot.nodes) {
        float radius = dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeIt.second, "radius"));
        float x = dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeIt.second, "x"));
        float y = dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeIt.second, "y"));
        float z = dust3d::String::toFloat(dust3d::String::valueOrEmpty(nodeIt.second, "z"));
        if (leftFirstTime || x - radius < left) {
            left = x - radius;
            leftFirstTime = false;
        }
        if (topFirstTime || y - radius < top) {
            top = y - radius;
            topFirstTime = false;
        }
        if (rightFirstTime || x + radius > right) {
            right = x + radius;
            rightFirstTime = false;
        }
        if (bottomFirstTime || y + radius > bottom) {
            bottom = y + radius;
            bottomFirstTime = false;
        }
        if (zLeftFirstTime || z - radius < zLeft) {
            zLeft = z - radius;
            zLeftFirstTime = false;
        }
        if (zRightFirstTime || z + radius > zRight) {
            zRight = z + radius;
            zRightFirstTime = false;
        }
    }
    *mainProfile = QRectF(QPointF(left, top), QPointF(right, bottom));
    *sideProfile = QRectF(QPointF(zLeft, top), QPointF(zRight, bottom));
}

void Document::settleOrigin()
{
    if (originSettled())
        return;
    dust3d::Snapshot snapshot;
    toSnapshot(&snapshot);
    QRectF mainProfile;
    QRectF sideProfile;
    resolveSnapshotBoundingBox(snapshot, &mainProfile, &sideProfile);
    setOriginX(mainProfile.x() + mainProfile.width() / 2);
    setOriginY(mainProfile.y() + mainProfile.height() / 2);
    setOriginZ(sideProfile.x() + sideProfile.width() / 2);
    markAllDirty();
    emit originChanged();
}

void Document::setPartXmirrorState(dust3d::Uuid partId, bool mirrored)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.xMirrored == mirrored)
        return;
    part->second.xMirrored = mirrored;
    part->second.dirty = true;
    settleOrigin();
    emit partXmirrorStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformThickness(dust3d::Uuid partId, float thickness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    part->second.setDeformThickness(thickness);
    part->second.dirty = true;
    emit partDeformThicknessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformWidth(dust3d::Uuid partId, float width)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    part->second.setDeformWidth(width);
    part->second.dirty = true;
    emit partDeformWidthChanged(partId);
    emit skeletonChanged();
}

void Document::setPartDeformUnified(dust3d::Uuid partId, bool unified)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.deformUnified == unified)
        return;
    part->second.deformUnified = unified;
    part->second.dirty = true;
    emit partDeformUnifyStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartRoundState(dust3d::Uuid partId, bool rounded)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.rounded == rounded)
        return;
    part->second.rounded = rounded;
    part->second.dirty = true;
    emit partRoundStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartChamferState(dust3d::Uuid partId, bool chamfered)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.chamfered == chamfered)
        return;
    part->second.chamfered = chamfered;
    part->second.dirty = true;
    emit partChamferStateChanged(partId);
    emit skeletonChanged();
}

void Document::setPartTarget(dust3d::Uuid partId, dust3d::PartTarget target)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.target == target)
        return;
    part->second.target = target;
    part->second.dirty = true;
    emit partTargetChanged(partId);
    emit skeletonChanged();
}

void Document::setPartColorSolubility(dust3d::Uuid partId, float solubility)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.colorSolubility, solubility))
        return;
    part->second.colorSolubility = solubility;
    part->second.dirty = true;
    emit partColorSolubilityChanged(partId);
    emit skeletonChanged();
}

void Document::setPartMetalness(dust3d::Uuid partId, float metalness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.metalness, metalness))
        return;
    part->second.metalness = metalness;
    part->second.dirty = true;
    emit partMetalnessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartRoughness(dust3d::Uuid partId, float roughness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.roughness, roughness))
        return;
    part->second.roughness = roughness;
    part->second.dirty = true;
    emit partRoughnessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartHollowThickness(dust3d::Uuid partId, float hollowThickness)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(part->second.hollowThickness, hollowThickness))
        return;
    part->second.hollowThickness = hollowThickness;
    part->second.dirty = true;
    emit partHollowThicknessChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCountershaded(dust3d::Uuid partId, bool countershaded)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.countershaded == countershaded)
        return;
    part->second.countershaded = countershaded;
    part->second.dirty = true;
    emit partCountershadeStateChanged(partId);
    emit textureChanged();
}

void Document::setPartSmoothCutoffDegrees(dust3d::Uuid partId, float degrees)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (dust3d::Math::isEqual(part->second.smoothCutoffDegrees, degrees))
        return;
    part->second.smoothCutoffDegrees = degrees;
    part->second.dirty = true;
    emit partSmoothCutoffDegreesChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutRotation(dust3d::Uuid partId, float cutRotation)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (qFuzzyCompare(cutRotation, part->second.cutRotation))
        return;
    part->second.setCutRotation(cutRotation);
    part->second.dirty = true;
    emit partCutRotationChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutFace(dust3d::Uuid partId, dust3d::CutFace cutFace)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.cutFace == cutFace)
        return;
    part->second.setCutFace(cutFace);
    part->second.dirty = true;
    emit partCutFaceChanged(partId);
    emit skeletonChanged();
}

void Document::setPartCutFaceLinkedId(dust3d::Uuid partId, dust3d::Uuid linkedId)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.cutFace == dust3d::CutFace::UserDefined && part->second.cutFaceLinkedId == linkedId)
        return;
    part->second.setCutFaceLinkedId(linkedId);
    part->second.dirty = true;
    emit partCutFaceChanged(partId);
    emit skeletonChanged();
}

void Document::setPartColorState(dust3d::Uuid partId, bool hasColor, QColor color)
{
    auto part = partMap.find(partId);
    if (part == partMap.end()) {
        qDebug() << "Part not found:" << partId;
        return;
    }
    if (part->second.hasColor == hasColor && part->second.color == color)
        return;
    part->second.hasColor = hasColor;
    part->second.color = color;
    part->second.dirty = true;
    emit partColorStateChanged(partId);
    emit skeletonChanged();
}

void Document::saveSnapshot()
{
    Document::HistoryItem item;
    toSnapshot(&item.snapshot);
    if (m_undoItems.size() + 1 > m_maxSnapshot)
        m_undoItems.pop_front();
    m_undoItems.push_back(item);
}

void Document::undo()
{
    if (!undoable())
        return;
    m_redoItems.push_back(m_undoItems.back());
    m_undoItems.pop_back();
    const auto& item = m_undoItems.back();
    fromSnapshot(item.snapshot);
    qDebug() << "Undo/Redo items:" << m_undoItems.size() << m_redoItems.size();
}

void Document::redo()
{
    if (m_redoItems.empty())
        return;
    m_undoItems.push_back(m_redoItems.back());
    const auto& item = m_redoItems.back();
    fromSnapshot(item.snapshot);
    m_redoItems.pop_back();
    qDebug() << "Undo/Redo items:" << m_undoItems.size() << m_redoItems.size();
}

void Document::clearHistories()
{
    m_undoItems.clear();
    m_redoItems.clear();
}

void Document::paste()
{
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        dust3d::Snapshot snapshot;
        std::string text = mimeData->text().toUtf8().constData();
        loadSnapshotFromXmlString(&snapshot, (char*)text.c_str());
        addFromSnapshot(snapshot, SnapshotSource::Paste);
        saveSnapshot();
    }
}

bool Document::hasPastableNodesInClipboard() const
{
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();
    if (mimeData->hasText()) {
        if (-1 != mimeData->text().indexOf("<node "))
            return true;
    }
    return false;
}

bool Document::undoable() const
{
    return m_undoItems.size() >= 2;
}

bool Document::redoable() const
{
    return !m_redoItems.empty();
}

bool Document::isNodeEditable(dust3d::Uuid nodeId) const
{
    const Document::Node* node = findNode(nodeId);
    if (!node) {
        qDebug() << "Node id not found:" << nodeId;
        return false;
    }
    return !isPartReadonly(node->partId);
}

bool Document::isEdgeEditable(dust3d::Uuid edgeId) const
{
    const Document::Edge* edge = findEdge(edgeId);
    if (!edge) {
        qDebug() << "Edge id not found:" << edgeId;
        return false;
    }
    return !isPartReadonly(edge->partId);
}

bool Document::isExportReady() const
{
    if (m_meshGenerator || m_textureGenerator || m_boneGenerator)
        return false;

    if (m_isResultMeshObsolete || m_isTextureObsolete || m_isResultBoneObsolete)
        return false;

    return true;
}

void Document::checkExportReadyState()
{
    if (isExportReady())
        emit exportReady();
}

bool Document::isMeshGenerating() const
{
    return nullptr != m_meshGenerator;
}

bool Document::isTextureGenerating() const
{
    return nullptr != m_textureGenerator;
}

bool Document::isBoneGenerating() const
{
    return nullptr != m_boneGenerator;
}

void Document::copyNodes(std::set<dust3d::Uuid> nodeIdSet) const
{
    dust3d::Snapshot snapshot;
    toSnapshot(&snapshot, nodeIdSet, Document::SnapshotFor::Document);
    std::string snapshotXml;
    dust3d::saveSnapshotToXmlString(snapshot, snapshotXml);
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(snapshotXml.c_str());
}

void Document::collectCutFaceList(std::vector<QString>& cutFaces) const
{
    cutFaces.clear();

    std::vector<dust3d::Uuid> cutFacePartIdList;

    std::set<dust3d::Uuid> cutFacePartIds;
    for (const auto& it : partMap) {
        if (dust3d::PartTarget::CutFace == it.second.target) {
            if (cutFacePartIds.find(it.first) != cutFacePartIds.end())
                continue;
            cutFacePartIds.insert(it.first);
            cutFacePartIdList.push_back(it.first);
        }
        if (!it.second.cutFaceLinkedId.isNull()) {
            if (cutFacePartIds.find(it.second.cutFaceLinkedId) != cutFacePartIds.end())
                continue;
            cutFacePartIds.insert(it.second.cutFaceLinkedId);
            cutFacePartIdList.push_back(it.second.cutFaceLinkedId);
        }
    }

    // Sort cut face by center.x of front view
    std::map<dust3d::Uuid, float> centerOffsetMap;
    for (const auto& partId : cutFacePartIdList) {
        const Document::Part* part = findPart(partId);
        if (nullptr == part)
            continue;
        float offsetSum = 0;
        for (const auto& nodeId : part->nodeIds) {
            const Document::Node* node = findNode(nodeId);
            if (nullptr == node)
                continue;
            offsetSum += node->getX();
        }
        if (qFuzzyIsNull(offsetSum))
            continue;
        centerOffsetMap[partId] = offsetSum / part->nodeIds.size();
    }
    std::sort(cutFacePartIdList.begin(), cutFacePartIdList.end(),
        [&](const dust3d::Uuid& firstPartId, const dust3d::Uuid& secondPartId) {
            return centerOffsetMap[firstPartId] < centerOffsetMap[secondPartId];
        });

    size_t cutFaceTypeCount = (size_t)dust3d::CutFace::UserDefined;
    for (size_t i = 0; i < (size_t)cutFaceTypeCount; ++i) {
        dust3d::CutFace cutFace = (dust3d::CutFace)i;
        cutFaces.push_back(QString(dust3d::CutFaceToString(cutFace).c_str()));
    }

    for (const auto& it : cutFacePartIdList)
        cutFaces.push_back(QString(it.toString().c_str()));
}

void Document::addBone(const dust3d::Uuid& boneId)
{
    if (boneMap.end() != boneMap.find(boneId))
        return;

    Bone bone(boneId);
    bone.name = "Bone" + QString::number(boneMap.size() + 1);
    boneMap.emplace(boneId, std::move(bone));
    boneIdList.push_back(boneId);
    emit boneAdded(boneId);
    emit boneIdListChanged();
    emit rigChanged();
}

void Document::addNodesToBone(const dust3d::Uuid& boneId, const std::vector<dust3d::Uuid>& nodeIds)
{
    for (const auto& nodeId : nodeIds) {
        auto nodeIt = nodeMap.find(nodeId);
        if (nodeIt == nodeMap.end())
            continue;
        nodeIt->second.boneIds.insert(boneId);
    }
    emit boneNodesChanged(boneId);
    emit rigChanged();
}

void Document::removeNodesFromBone(const dust3d::Uuid& boneId, const std::vector<dust3d::Uuid>& nodeIds)
{
    for (const auto& nodeId : nodeIds) {
        auto nodeIt = nodeMap.find(nodeId);
        if (nodeIt == nodeMap.end())
            continue;
        nodeIt->second.boneIds.erase(boneId);
    }
    emit boneNodesChanged(boneId);
    emit rigChanged();
}

void Document::applyBoneJoints(const dust3d::Uuid& boneId, const std::vector<dust3d::Uuid>& nodeIds)
{
    auto boneIt = boneMap.find(boneId);
    if (boneIt == boneMap.end())
        return;

    std::set<dust3d::Uuid> changeNodeIds;
    for (const auto& nodeId : boneIt->second.joints) {
        auto nodeIt = nodeMap.find(nodeId);
        if (nodeIt == nodeMap.end())
            continue;
        nodeIt->second.asBontJoints.erase(boneId);
        nodeIt->second.boneJoint = !nodeIt->second.asBontJoints.empty();
        changeNodeIds.insert(nodeId);
    }
    boneIt->second.joints = nodeIds;
    for (const auto& nodeId : boneIt->second.joints) {
        auto nodeIt = nodeMap.find(nodeId);
        if (nodeIt == nodeMap.end())
            continue;
        nodeIt->second.asBontJoints.insert(boneId);
        nodeIt->second.boneJoint = true;
        changeNodeIds.insert(nodeId);
    }
    for (const auto& nodeId : changeNodeIds) {
        emit nodeBoneJointStateChanged(nodeId);
    }
    emit boneJointsChanged(boneId);
    emit rigChanged();
}

void Document::removeBone(const dust3d::Uuid& boneId)
{
    if (boneMap.end() == boneMap.find(boneId))
        return;

    for (auto& it : nodeMap)
        it.second.boneIds.erase(boneId);

    boneIdList.erase(std::remove(boneIdList.begin(), boneIdList.end(), boneId), boneIdList.end());
    boneMap.erase(boneId);
    emit boneRemoved(boneId);
    emit boneIdListChanged();
    emit rigChanged();
}

void Document::setBoneAttachment(const dust3d::Uuid& boneId, const dust3d::Uuid& toBoneId, int toBoneJointIndex)
{
    auto boneIt = boneMap.find(boneId);
    if (boneIt == boneMap.end())
        return;
    if (boneIt->second.attachBoneId == toBoneId && boneIt->second.attachBoneJointIndex == toBoneJointIndex)
        return;
    boneIt->second.attachBoneId = toBoneId;
    boneIt->second.attachBoneJointIndex = toBoneJointIndex;
    emit boneAttachmentChanged(boneId);
    emit rigChanged();
}

void Document::renameBone(const dust3d::Uuid& boneId, const QString& name)
{
    auto boneIt = boneMap.find(boneId);
    if (boneIt == boneMap.end())
        return;
    if (boneIt->second.name == name)
        return;
    boneIt->second.name = name;
    emit boneNameChanged(boneId);
    emit rigChanged();
}

const Document::Bone* Document::findBone(const dust3d::Uuid& boneId) const
{
    auto boneIt = boneMap.find(boneId);
    if (boneIt == boneMap.end())
        return nullptr;
    return &boneIt->second;
}

void Document::stopBoneJointsPicking()
{
    if (EditMode::Pick != editMode)
        return;
    setEditMode(EditMode::Select);
}

void Document::startBoneJointsPicking(const dust3d::Uuid& boneId, size_t boneJoints)
{
    stopBoneJointsPicking();

    m_currentBondId = boneId;
    m_currentBoneJoints = boneJoints;

    setEditMode(EditMode::Pick);
}

void Document::resetCurrentBone()
{
    m_currentBondId = dust3d::Uuid();
    m_currentBoneJoints = 0;
    m_currentBoneJointNodes.clear();
}

void Document::pickBoneNode(const dust3d::Uuid& nodeId)
{
    if (m_currentBondId.isNull())
        return;

    for (const auto& it : m_currentBoneJointNodes) {
        if (it == nodeId)
            return;
    }

    m_currentBoneJointNodes.push_back(nodeId);

    applyBoneJoints(m_currentBondId, m_currentBoneJointNodes);

    if (m_currentBoneJointNodes.size() < m_currentBoneJoints)
        return;

    stopBoneJointsPicking();
}

void Document::generateBone()
{
    if (nullptr != m_boneGenerator) {
        m_isResultBoneObsolete = true;
        return;
    }

    m_isResultBoneObsolete = false;

    if (nullptr == m_currentObject)
        return;

    emit boneGenerating();

    auto object = std::make_unique<dust3d::Object>(*m_currentObject);

    auto snapshot = std::make_unique<dust3d::Snapshot>();
    toSnapshot(snapshot.get());

    QThread* thread = new QThread;
    m_boneGenerator = std::make_unique<BoneGenerator>(std::move(object), std::move(snapshot));
    m_boneGenerator->moveToThread(thread);
    connect(thread, &QThread::started, m_boneGenerator.get(), &BoneGenerator::process);
    connect(m_boneGenerator.get(), &BoneGenerator::finished, this, &Document::boneReady);
    connect(m_boneGenerator.get(), &BoneGenerator::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

void Document::boneReady()
{
    std::unique_ptr<std::map<dust3d::Uuid, std::unique_ptr<ModelMesh>>> bonePreviewMeshes;
    bonePreviewMeshes.reset(m_boneGenerator->takeBonePreviewMeshes());
    bool bonePreviewsChanged = bonePreviewMeshes && !bonePreviewMeshes->empty();
    if (bonePreviewsChanged) {
        for (auto& it : *bonePreviewMeshes) {
            setBonePreviewMesh(it.first, std::move(it.second));
        }
        emit resultBonePreviewMeshesChanged();
    }

    m_resultBodyBonePreviewMesh = m_boneGenerator->takeBodyPreviewMesh();
    emit resultBodyBonePreviewMeshChanged();

    // TODO:

    m_boneGenerator.reset();

    emit resultBoneChanged();

    if (m_isResultBoneObsolete)
        generateBone();
}

void Document::setBonePreviewMesh(const dust3d::Uuid& boneId, std::unique_ptr<ModelMesh> mesh)
{
    Document::Bone* bone = (Document::Bone*)findBone(boneId);
    if (nullptr == bone)
        return;
    bone->updatePreviewMesh(std::move(mesh));
    emit bonePreviewMeshChanged(boneId);
}

void Document::setBonePreviewPixmap(const dust3d::Uuid& boneId, const QPixmap& pixmap)
{
    Document::Bone* bone = (Document::Bone*)findBone(boneId);
    if (nullptr == bone)
        return;
    bone->previewPixmap = pixmap;
    emit bonePreviewPixmapChanged(boneId);
}
