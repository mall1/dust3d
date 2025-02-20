/*
 *  Copyright (c) 2016-2022 Jeremy HU <jeremy-at-dust3d dot org>. All rights reserved. 
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:

 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.

 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <dust3d/rig/bone_generator.h>

namespace dust3d {

BoneGenerator::BoneGenerator()
{
}

void BoneGenerator::setVertices(const std::vector<Vector3>& vertices)
{
    m_vertices = vertices;
}

void BoneGenerator::setTriangles(const std::vector<std::vector<size_t>>& triangles)
{
    m_triangles = triangles;
}

void BoneGenerator::setPositionToNodeMap(const std::map<PositionKey, Uuid>& positionToNodeMap)
{
    m_positionToNodeMap = positionToNodeMap;
}

void BoneGenerator::addBone(const Uuid& boneId, const Bone& bone)
{
    Bone newBone = bone;
    newBone.index = m_boneMap.size();
    m_boneMap.emplace(std::make_pair(boneId, std::move(newBone)));
}

void BoneGenerator::addNode(const Uuid& nodeId, const Node& node)
{
    m_nodeMap.emplace(std::make_pair(nodeId, node));
}

void BoneGenerator::addNodeBinding(const Uuid& nodeId, const NodeBinding& nodeBinding)
{
    m_nodeBindingMap.emplace(std::make_pair(nodeId, nodeBinding));
}

void BoneGenerator::buildEdges()
{
    for (const auto& triangle : m_triangles) {
        for (size_t i = 0; i < 3; ++i) {
            size_t j = (i + 1) % 3;
            m_edges[triangle[i]].insert(triangle[j]);
            m_edges[triangle[j]].insert(triangle[i]);
        }
    }
}

Uuid BoneGenerator::resolveVertexSourceByBreadthFirstSearch(size_t vertexIndex, std::unordered_set<size_t>& visited)
{
    visited.insert(vertexIndex);
    auto findNeighbors = m_edges.find(vertexIndex);
    if (findNeighbors == m_edges.end())
        return Uuid();
    for (const auto& it : findNeighbors->second) {
        if (!m_vertexSourceNodes[it].isNull())
            return m_vertexSourceNodes[it];
    }
    for (const auto& it : findNeighbors->second) {
        if (visited.end() != visited.find(it))
            continue;
        Uuid foundId = resolveVertexSourceByBreadthFirstSearch(it, visited);
        if (!foundId.isNull())
            return foundId;
    }
    return Uuid();
}

void BoneGenerator::resolveVertexSources()
{
    m_vertexSourceNodes.resize(m_vertices.size());
    for (size_t i = 0; i < m_vertices.size(); ++i) {
        auto findNode = m_positionToNodeMap.find(m_vertices[i]);
        if (findNode == m_positionToNodeMap.end())
            continue;
        m_vertexSourceNodes[i] = findNode->second;
    }

    for (size_t i = 0; i < m_vertexSourceNodes.size(); ++i) {
        if (!m_vertexSourceNodes[i].isNull())
            continue;
        std::unordered_set<size_t> visited;
        m_vertexSourceNodes[i] = resolveVertexSourceByBreadthFirstSearch(i, visited);
    }
}

void BoneGenerator::buildBoneJoints()
{
    for (auto& boneIt : m_boneMap) {
        boneIt.second.startPositions.resize(boneIt.second.joints.size());
        for (size_t i = 0; i < boneIt.second.joints.size(); ++i) {
            const auto& nodeId = boneIt.second.joints[i];
            auto nodeIt = m_nodeMap.find(nodeId);
            if (nodeIt == m_nodeMap.end())
                continue;
            boneIt.second.startPositions[i] = nodeIt->second.position;
        }
        boneIt.second.forwardVectors.resize(boneIt.second.startPositions.size());
        for (size_t i = 0; i + 1 < boneIt.second.startPositions.size(); ++i) {
            boneIt.second.forwardVectors[i] = boneIt.second.startPositions[i + 1] - boneIt.second.startPositions[i];
        }
    }
}

void BoneGenerator::groupBoneVertices()
{
    for (size_t i = 0; i < m_vertexSourceNodes.size(); ++i) {
        const Uuid& sourceNodeId = m_vertexSourceNodes[i];
        if (sourceNodeId.isNull())
            continue;
        auto findBinding = m_nodeBindingMap.find(sourceNodeId);
        if (findBinding == m_nodeBindingMap.end())
            continue;
        for (const auto& boneId : findBinding->second.boneIds) {
            m_boneVertices[boneId].insert(i);
        }
    }
}

void BoneGenerator::calculateBoneVertexWeights(const Uuid& boneId, Bone& bone)
{
    auto findBoneVertices = m_boneVertices.find(boneId);
    if (findBoneVertices == m_boneVertices.end())
        return;

    if (bone.startPositions.size() < 2)
        return;

    std::vector<std::unordered_set<size_t>> segments(bone.startPositions.size());

    std::unordered_set<size_t> usedVertices;

    // Split bone vertices by joint position and forward direction
    for (size_t jointIndex = 0; jointIndex + 1 < bone.startPositions.size(); ++jointIndex) {
        const auto& nextJointPosition = bone.startPositions[jointIndex + 1];
        auto forwardDirection = bone.forwardVectors[jointIndex].normalized();
        for (const auto& vertex : findBoneVertices->second) {
            if (usedVertices.end() != usedVertices.find(vertex))
                continue;
            const auto& vertexPosition = m_vertices[vertex];
            if (Vector3::dotProduct(forwardDirection, (vertexPosition - nextJointPosition).normalized()) <= 0.0) {
                segments[jointIndex].insert(vertex);
                usedVertices.insert(vertex);
            }
        }
    }
    for (const auto& vertex : findBoneVertices->second) {
        if (usedVertices.end() != usedVertices.find(vertex))
            continue;
        segments[segments.size() - 2].insert(vertex);
    }

    bone.vertexWeights.resize(bone.startPositions.size());
    for (size_t jointIndex = 0; jointIndex < bone.startPositions.size(); ++jointIndex) {
        for (const auto& it : segments[jointIndex])
            bone.vertexWeights[jointIndex].push_back(VertexWeight { it, 1.0 });
    }

    // TODO:
}

void BoneGenerator::generate()
{
    buildEdges();
    resolveVertexSources();
    groupBoneVertices();
    buildBoneJoints();
    for (auto& boneIt : m_boneMap)
        calculateBoneVertexWeights(boneIt.first, boneIt.second);
    generateBonePreviews();
}

std::map<Uuid, BoneGenerator::BonePreview>& BoneGenerator::bonePreviews()
{
    return m_bonePreviews;
}

BoneGenerator::BonePreview& BoneGenerator::bodyPreview()
{
    return m_bodyPreview;
}

void BoneGenerator::addBonePreviewTriangle(BonePreview& bonePreview,
    std::unordered_map<size_t, size_t>& oldToNewVertexMap,
    const std::vector<size_t>& triangle,
    const Color& color)
{
    std::vector<size_t> newTriangle(3);
    for (size_t i = 0; i < 3; ++i) {
        auto findVertex = oldToNewVertexMap.find(triangle[i]);
        if (findVertex == oldToNewVertexMap.end()) {
            oldToNewVertexMap.insert(std::make_pair(triangle[i], bonePreview.vertices.size()));
            newTriangle[i] = bonePreview.vertices.size();
            bonePreview.vertices.push_back(m_vertices[triangle[i]]);
            bonePreview.vertexColors.push_back(color);
        } else {
            newTriangle[i] = findVertex->second;
        }
    }
    bonePreview.triangles.emplace_back(newTriangle);
}

void BoneGenerator::generateBonePreviews()
{
    const static std::array<Color, 7> s_colors = {
        Color(255.0 / 255.0, 0.0 / 255.0, 0.0 / 255.0),
        Color(255.0 / 255.0, 128.0 / 255.0, 216.0 / 64.0),
        Color(255.0 / 255.0, 255.0 / 255.0, 0.0 / 255.0),
        Color(0.0 / 255.0, 255.0 / 255.0, 0.0 / 255.0),
        Color(0.0 / 255.0, 128.0 / 255.0, 255.0 / 255.0),
        Color(0.0 / 255.0, 0.0 / 255.0, 44.0 / 128.0),
        Color(128.0 / 255.0, 0.0 / 255.0, 128.0 / 255.0),
    };

    for (const auto& it : m_boneVertices) {
        auto findBone = m_boneMap.find(it.first);
        if (findBone == m_boneMap.end())
            continue;

        BonePreview bonePreview;
        std::unordered_map<size_t, size_t> oldToNewVertexMap;

        const auto& color = s_colors[findBone->second.index % s_colors.size()];

        for (const auto& triangle : m_triangles) {
            size_t countedPoints = 0;
            for (size_t i = 0; i < 3; ++i) {
                if (it.second.end() != it.second.find(triangle[i]))
                    ++countedPoints;
            }
            if (0 == countedPoints)
                continue;
            addBonePreviewTriangle(bonePreview, oldToNewVertexMap, triangle, color);
        }

        m_bonePreviews.emplace(std::make_pair(it.first, std::move(bonePreview)));
    }

    std::unordered_map<size_t, std::vector<Color>> vertexSkinColors;
    for (const auto& it : m_boneVertices) {
        auto findBone = m_boneMap.find(it.first);
        if (findBone == m_boneMap.end())
            continue;
        const auto& color = s_colors[(findBone->second.index) % s_colors.size()];
        const auto& alternativeColor = s_colors[(findBone->second.index + s_colors.size() / 2) % s_colors.size()];
        for (size_t jointIndex = 0; jointIndex < findBone->second.vertexWeights.size(); ++jointIndex) {
            const auto& useColor = 0 == (jointIndex % 2) ? color : alternativeColor;
            for (const auto& vertexWeight : findBone->second.vertexWeights[jointIndex]) {
                vertexSkinColors[vertexWeight.vertex].push_back(useColor);
            }
        }
    }
    std::vector<Color> bodyVertexColors(m_vertices.size(), Color(0.0, 0.0, 0.0, 0.0));
    for (const auto& it : vertexSkinColors) {
        Color color;
        for (const auto& colorIt : it.second)
            color = color + colorIt;
        color = color * (1.0 / it.second.size());
        bodyVertexColors[it.first] = color;
    }
    m_bodyPreview.vertices = m_vertices;
    m_bodyPreview.triangles = m_triangles;
    m_bodyPreview.vertexColors = std::move(bodyVertexColors);
}

}
