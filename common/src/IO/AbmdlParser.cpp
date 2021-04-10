/*
 Copyright (C) 2010-2017 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "AbmdlParser.h"

#include "Exceptions.h"
#include "Assets/EntityModel.h"
#include "Assets/Texture.h"
#include "Assets/TextureBuffer.h"
#include "Assets/Palette.h"
#include "IO/Reader.h"
#include "IO/FileSystem.h"
#include "IO/SkinLoader.h"
#include "Renderer/IndexRangeMapBuilder.h"
#include "Renderer/PrimType.h"

#include <kdl/string_utils.h>

#include <string>
#include <vector>

namespace TrenchBroom {
    namespace IO {
        namespace MdlLayout {
            static constexpr int Ident = (('T'<<24) + ('S'<<16) + ('D'<<8) + 'I');
            static constexpr int Version10 = 10;

            static constexpr unsigned int TexFlagMasked = 0x40;
            static constexpr unsigned int TexFlagUVCoOrds = 0x80000000;
            static constexpr unsigned int MdlFlagNoEmbeddedTextures = 0x800;

            static constexpr unsigned int Offset_HeaderFlags = 136;

            static constexpr unsigned int Offset_HeaderBoneCount = 140;
            static constexpr unsigned int Offset_HeaderBones = Offset_HeaderBoneCount + sizeof(int32_t);

            static constexpr unsigned int Offset_HeaderTexureCount = 180;
            static constexpr unsigned int Offset_HeaderTexureInfo = Offset_HeaderTexureCount + sizeof(int32_t);

            static constexpr unsigned int Offset_HeaderSkinCount = 192;
            static constexpr unsigned int Offset_HeaderSkinData = Offset_HeaderSkinCount + (2 * sizeof(int32_t));

            static constexpr unsigned int Offset_HeaderBodyPartCount = 204;
            static constexpr unsigned int Offset_HeaderBodyPartData = Offset_HeaderBodyPartCount + sizeof(int32_t);

            static constexpr size_t Size_TextureInfo = 64 + (4 * sizeof(int32_t));
            static constexpr size_t Size_Skin = sizeof(int16_t);
            static constexpr size_t Size_TexturePalette = 768;
            static constexpr size_t Size_BodyPart = 64 + (3 * sizeof(int32_t));
            static constexpr size_t Size_SubModel = 64 + (11 * sizeof(int32_t)) + sizeof(float);
            static constexpr size_t Size_Mesh = 5 * sizeof(int32_t);
            static constexpr size_t Size_Bone = 32 + (8 * sizeof(int32_t)) + (12 * sizeof(float));
        }

        AbmdlParser::TextureInfo::TextureInfo(BufferedReader& reader)
        {
            name = reader.readString(64);
            flags = reader.readUnsignedInt<uint32_t>();
            width = reader.readInt<int32_t>();
            height = reader.readInt<int32_t>();
            index = reader.readInt<int32_t>();
        }

        AbmdlParser::BodyPart::BodyPart(BufferedReader& reader)
        {
            name = reader.readString(64);
            numModels = reader.readSize<int32_t>();
            base = reader.readInt<int32_t>();
            modelIndex = reader.readSize<int32_t>();
        }

        AbmdlParser::SubModel::SubModel(BufferedReader& reader)
        {
            name = reader.readString(64);
            unused = reader.readInt<int32_t>();
            unused2 = reader.readFloat<float>();
            numMeshes = reader.readSize<int32_t>();
            meshIndex = reader.readSize<int32_t>();
            numVertices = reader.readSize<int32_t>();
            vertexInfoIndex = reader.readSize<int32_t>();
            vertexIndex = reader.readSize<int32_t>();
            numNormals = reader.readSize<int32_t>();
            normalInfoIndex = reader.readSize<int32_t>();
            normalIndex = reader.readSize<int32_t>();
            blendVertexInfoIndex = reader.readSize<int32_t>();
            blendNormalInfoIndex = reader.readSize<int32_t>();
        }

        AbmdlParser::Mesh::Mesh(BufferedReader& reader)
        {
            numTriangles = reader.readSize<int32_t>();
            triangleIndex = reader.readSize<int32_t>();
            skinRef = reader.readSize<int32_t>();
            numNormals = reader.readSize<int32_t>();
            unused = reader.readInt<int32_t>();
        }

        AbmdlParser::Bone::Bone(BufferedReader& reader)
        {
            name = reader.readString(32);
            parent = reader.readInt<int32_t>();
            unused = reader.readInt<int32_t>();

            for ( size_t index = 0; index < 6; ++index )
            {
                controller[index] = reader.readInt<int32_t>();
            }

            for ( size_t index = 0; index < 6; ++index )
            {
                value[index] = reader.readFloat<float>();
            }

            for ( size_t index = 0; index < 6; ++index )
            {
                scale[index] = reader.readFloat<float>();
            }
        }

        AbmdlParser::TriangleVertex::TriangleVertex(BufferedReader& reader)
        {
            readFrom(reader);
        }

        void AbmdlParser::TriangleVertex::readFrom(BufferedReader& reader)
        {
            vertexIndex = reader.readSize<int16_t>();
            normalIndex = reader.readSize<int16_t>();
            s = reader.readInt<int16_t>();
            t = reader.readInt<int16_t>();
        }

        std::string AbmdlParser::MdlIterator::GenerateSurfaceName() const
        {
            return bodyPart.name + "_" + firstSubModel.name + "_" + std::to_string(meshIndex);
        }

        AbmdlParser::AbmdlParser(const std::string& name, const char* begin, const char* end, const FileSystem& fs) :
        m_name(name),
        m_begin(begin),
        m_end(end),
        m_fs(fs) {
            assert(m_begin < m_end);
            unused(m_end);
        }

        std::unique_ptr<Assets::EntityModel> AbmdlParser::doInitializeModel(Logger& logger) {
            BufferedReader reader = Reader::from(m_begin, m_end).buffer();

            const int32_t ident = reader.readInt<int32_t>();
            const int32_t version = reader.readInt<int32_t>();

            if (ident != MdlLayout::Ident) {
                throw AssetException("Unknown MDL model ident: " + std::to_string(ident));
            }
            if (version != MdlLayout::Version10) {
                throw AssetException("Unknown MDL model version: " + std::to_string(version));
            }

            std::unique_ptr<Assets::EntityModel> model = std::make_unique<Assets::EntityModel>(m_name, Assets::PitchType::MdlInverted);
            createSurfacesAndLoadTextures(logger, reader, *model);
            return model;
        }

        void AbmdlParser::doLoadFrame(const size_t frameIndex, Assets::EntityModel& model, Logger&) {
            BufferedReader reader = Reader::from(m_begin, m_end).buffer();

            const int32_t ident = reader.readInt<int32_t>();
            const int32_t version = reader.readInt<int32_t>();

            if (ident != MdlLayout::Ident) {
                throw AssetException("Unknown MDL model ident: " + std::to_string(ident));
            }
            if (version != MdlLayout::Version10) {
                throw AssetException("Unknown MDL model version: " + std::to_string(version));
            }

            if ( frameIndex != 0 )
            {
                throw AssetException("v" + std::to_string(MdlLayout::Version10) + " MDLs currently only support one frame.");
            }

            readBonesAndTransforms(reader);
            loadVerticesForSurfaces(reader, model);
        }

        void AbmdlParser::createSurfacesAndLoadTextures(Logger& logger, BufferedReader& reader, Assets::EntityModel& model)
        {
            reader.seekFromBegin(MdlLayout::Offset_HeaderSkinCount);
            const size_t numSkinReferences = reader.readSize<int32_t>();
            const size_t numSkinFamilies = reader.readSize<int32_t>();
            const size_t skinDataOffset = reader.readSize<int32_t>();

            if ( numSkinReferences < 1 || numSkinFamilies < 1 || skinDataOffset < 1 )
            {
                throw AssetException("MDLs with textures in a \"t\" file are not currently supported.");
            }

            reader.seekFromBegin(MdlLayout::Offset_HeaderFlags);
            const uint32_t mdlFlags = reader.readUnsignedInt<uint32_t>();
            const bool noEmbeddedTextures = mdlFlags & MdlLayout::MdlFlagNoEmbeddedTextures;

            iterateMdlComponents(reader, [this, &logger, &reader, &model, skinDataOffset, noEmbeddedTextures](const MdlIterator& it)
            {
                Assets::EntityModelSurface& surface = model.addSurface(it.GenerateSurfaceName());
                std::vector<Assets::Texture> textures;

                const size_t meshSkinOffset = skinDataOffset + (it.mesh.skinRef * MdlLayout::Size_Skin);
                reader.seekFromBegin(meshSkinOffset);
                const size_t textureIndex = reader.readSize<int16_t>();

                // This does potentially read textures multiple times, if different meshes
                // reference the same texture. Does this matter? Are textures cached behind the scenes?
                // Otherwise, we'd have to build up a table of which textures are used by which meshes,
                // and then only load those textures once each.
                if ( noEmbeddedTextures )
                {
                    readTextureFromDisk(logger, reader, textures, textureIndex);
                }
                else
                {
                    readEmbeddedTexture(reader, textures, textureIndex);
                }

                surface.setSkins(std::move(textures));
            });
        }

        void AbmdlParser::loadVerticesForSurfaces(BufferedReader& reader, Assets::EntityModel& model)
        {
            m_bodyPartIndexForCachedModelData = ~0UL;

            iterateMdlComponents(reader, [this, &reader, &model](const MdlIterator& it)
            {
                loadVerticesForSurface(reader, model, model.surface(it.iteratorIndex), it);
            });
        }

        void AbmdlParser::loadVerticesForSurface(BufferedReader& reader, Assets::EntityModel& model, Assets::EntityModelSurface& surface, const MdlIterator& it)
        {
            if ( m_bodyPartIndexForCachedModelData != it.bodyPartIndex )
            {
                cacheModelComponents(reader, it.firstSubModel);
                m_bodyPartIndexForCachedModelData = it.bodyPartIndex;
            }

            std::vector<Assets::EntityModelVertex> meshFinalisedVertices;
            vm::bbox3f::builder bounds;
            getUnindexedVerticesFromMesh(reader, meshFinalisedVertices, bounds, it);

            Renderer::IndexRangeMap::Size size;
            size.inc(Renderer::PrimType::Triangles, meshFinalisedVertices.size());

            Renderer::IndexRangeMapBuilder<Assets::EntityModelVertex::Type> builder(meshFinalisedVertices.size(), size);
            builder.addTriangles(meshFinalisedVertices);

            Assets::EntityModelLoadedFrame& frame = model.loadFrame(0, "frame_0", bounds.bounds());
            surface.addIndexedMesh(frame, builder.vertices(), builder.indices());
        }

        void AbmdlParser::iterateMdlComponents(BufferedReader& reader, const std::function<void(const MdlIterator&)>& callback)
        {
            if ( !callback )
            {
                return;
            }

            size_t iteratorIndex = 0;

            reader.seekFromBegin(MdlLayout::Offset_HeaderBodyPartCount);
            const size_t bodyPartCount = reader.readSize<int32_t>();
            const size_t bodyPartsOffset = reader.readSize<int32_t>();

            for ( size_t bodyPartIndex = 0; bodyPartIndex < bodyPartCount; ++bodyPartIndex )
            {
                const size_t bodyPartOffset = bodyPartsOffset + (bodyPartIndex * MdlLayout::Size_BodyPart);
                reader.seekFromBegin(bodyPartOffset);
                const BodyPart bodyPart(reader);

                // Only deal with model 0 right now. Other models are used for showing different bodies,
                // so we could deal with them properly in future.
                reader.seekFromBegin(bodyPart.modelIndex);
                const SubModel subModel(reader);

                // If model is "blank", don't bother.
                if ( subModel.name == "blank" )
                {
                    continue;
                }

                const size_t meshesOffset = subModel.meshIndex;

                for ( size_t meshIndex = 0; meshIndex < subModel.numMeshes; ++meshIndex )
                {
                    const size_t meshOffset = meshesOffset + (meshIndex * MdlLayout::Size_Mesh);
                    reader.seekFromBegin(meshOffset);
                    const Mesh mesh(reader);

                    const MdlIterator iterator = { iteratorIndex++, bodyPartIndex, bodyPart, subModel, meshIndex, mesh };
                    callback(iterator);
                }
            }
        }

        void AbmdlParser::readBonesAndTransforms(BufferedReader& reader)
        {
            m_bones.clear();
            m_boneTransforms.clear();

            reader.seekFromBegin(MdlLayout::Offset_HeaderBoneCount);
            const size_t boneCount = reader.readSize<int32_t>();
            const size_t bonesOffset = reader.readSize<int32_t>();

            m_bones.reserve(boneCount);
            m_boneTransforms.reserve(boneCount);
            reader.seekFromBegin(bonesOffset);

            for ( size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                // Reader is wound on by constructor of bone.
                m_bones.emplace_back(reader);

                const Bone& bone = m_bones[boneIndex];

                if ( boneIndex <= static_cast<size_t>(bone.parent) )
                {
                    // We assume bones are in order, so that the transform for the parent bone
                    // has been generated before the child is encountered.
                    throw AssetException("Bone " + std::to_string(boneIndex) + " was encountered before parent " + std::to_string(bone.parent));
                }

                createBoneTransform(m_bones[boneIndex]);
            }
        }

        void AbmdlParser::createBoneTransform(const Bone& bone)
        {
            const vm::quatf boneQuat = anglesToQuaternion(bone.value[3], bone.value[4], bone.value[5]);
            const vm::vec3f boneOrigin(bone.value[0], bone.value[1], bone.value[2]);

            if ( bone.parent < 0 )
            {
                // No parent matrix to combine, so just use the bone's matrix.
                m_boneTransforms.emplace_back(quatAndOriginToMat(boneQuat, boneOrigin));
                return;
            }

            // Otherwise, combine parent transform.
            m_boneTransforms.emplace_back();
            concat3x4Matrices(m_boneTransforms[static_cast<size_t>(bone.parent)], quatAndOriginToMat(boneQuat, boneOrigin), m_boneTransforms.back());
        }

        void AbmdlParser::readEmbeddedTexture(BufferedReader& reader, std::vector<Assets::Texture>& textureList, size_t textureIndex)
        {
            // Get the offset to the texture info objects.
            reader.seekFromBegin(MdlLayout::Offset_HeaderTexureInfo);
            const size_t textureInfoOffset = reader.readSize<int32_t>() + (textureIndex * MdlLayout::Size_TextureInfo);

            // Only do anything if we do have textures embedded - some Half Life models might not, according to the Xash code.
            if ( textureInfoOffset <= 0 )
            {
                return;
            }

            // Load a texture info object from this offset.
            reader.seekFromBegin(textureInfoOffset);
            const TextureInfo texInfo(reader);

            // Work out some properties based on the flags.
            const bool textureIsMasked = texInfo.flags & MdlLayout::TexFlagMasked;
            const auto transparency = textureIsMasked
                ? Assets::PaletteTransparency::Index255Transparent
                : Assets::PaletteTransparency::Opaque;
            const auto texType = transparency == Assets::PaletteTransparency::Index255Transparent
                ? Assets::TextureType::Masked
                : Assets::TextureType::Opaque;

            // Calculate how large the image is, and where the palette data is.
            const size_t imageDataSize = static_cast<size_t>(texInfo.width) * static_cast<size_t>(texInfo.height);
            const size_t paletteDataOffset = static_cast<size_t>(texInfo.index) + imageDataSize;

            // Create a vector to hold the palette data.
            std::vector<unsigned char> paletteData;
            paletteData.resize(MdlLayout::Size_TexturePalette);

            // Read the palette data.
            reader.seekFromBegin(paletteDataOffset);
            reader.read(reinterpret_cast<char*>(paletteData.data()), MdlLayout::Size_TexturePalette);

            // Set up a palette and image.
            Assets::Palette palette(paletteData);
            Assets::TextureBuffer rgbaImage(imageDataSize * 4);

            // Fill the image based on the pixel data.
            reader.seekFromBegin(static_cast<size_t>(texInfo.index));
            Color avgColor;
            palette.indexedToRgba(reader, imageDataSize, rgbaImage, transparency, avgColor);

            // Add the texture to the vector.
            textureList.emplace_back(texInfo.name, texInfo.width, texInfo.height, avgColor, std::move(rgbaImage), GL_RGBA, texType);
            m_textureInfos.emplace_back(texInfo);
        }

        void AbmdlParser::readTextureFromDisk(Logger& logger, BufferedReader& reader, std::vector<Assets::Texture>& textureList, size_t textureIndex)
        {
            // Get the offset to the texture info objects.
            reader.seekFromBegin(MdlLayout::Offset_HeaderTexureInfo);
            const size_t textureInfoOffset = reader.readSize<int32_t>() + (textureIndex * MdlLayout::Size_TextureInfo);

            // Only do anything if we do have textures embedded - some Half Life models might not, according to the Xash code.
            if ( textureInfoOffset <= 0 )
            {
                return;
            }

            // Load a texture info object from this offset.
            reader.seekFromBegin(textureInfoOffset);
            const TextureInfo texInfo(reader);

            // Let the skin loader attempt to load the referenced texture.
            textureList.push_back(loadSkin(Path("textures") + Path(texInfo.name), m_fs, logger));
            m_textureInfos.emplace_back(texInfo);
        }

        void AbmdlParser::cacheModelComponents(BufferedReader& reader, const SubModel& subModel)
        {
            m_modelBoneIndices.clear();
            m_modelBoneIndices.reserve(subModel.numVertices);
            reader.seekFromBegin(subModel.vertexInfoIndex);

            for ( size_t index = 0; index < subModel.numVertices; ++index )
            {
                m_modelBoneIndices.emplace_back(reader.readSize<uint8_t>());
            }

            m_modelVertPositions.clear();
            m_modelVertPositions.reserve(subModel.numVertices);
            reader.seekFromBegin(subModel.vertexIndex);

            for ( size_t index = 0; index < subModel.numVertices; ++index )
            {
                m_modelVertPositions.emplace_back(reader.readVec<float, 3>());
            }
        }

        void AbmdlParser::getUnindexedVerticesFromMesh(BufferedReader& reader, std::vector<Assets::EntityModelVertex>& outVertices, vm::bbox3f::builder& bounds, const MdlIterator& it)
        {
            const size_t triCmdsOffset = it.mesh.triangleIndex;
            std::vector<TriangleVertex> triangleVerts;
            triangleVerts.resize(3);
            reader.seekFromBegin(triCmdsOffset);

            for ( int32_t numTriCmds = reader.readInt<int16_t>(); numTriCmds != 0; numTriCmds = reader.readInt<int16_t>() )
            {
                if ( numTriCmds > 0 )
                {
                    // This is a triangle strip.
                    for ( size_t vertexIndex = 0; numTriCmds > 0; --numTriCmds, ++vertexIndex )
                    {
                        if ( vertexIndex == 0 )
                        {
                            // Fill in the first vertex.
                            triangleVerts[0].readFrom(reader);

                            // Not enough vertices to create a triangle yet.
                            continue;
                        }
                        else if ( vertexIndex == 1 )
                        {
                            // Fill in the third vertex (so that we get the ordering correct).
                            triangleVerts[2].readFrom(reader);

                            // Not enough vertices to create a triangle yet.
                            continue;
                        }
                        else if ( vertexIndex == 2 )
                        {
                            // Fill in the second vertex.
                            triangleVerts[1].readFrom(reader);
                        }
                        else if ( (vertexIndex % 2) != 0 )
                        {
                            // Vertex index is odd. Shift third vertex to be first, then add new third vertex.
                            triangleVerts[0] = triangleVerts[2];
                            triangleVerts[2].readFrom(reader);
                        }
                        else
                        {
                            // Vertex index is even. Shift second vertex to be first, then add new second vertex.
                            triangleVerts[0] = triangleVerts[1];
                            triangleVerts[1].readFrom(reader);
                        }

                        // Create a triangle out of these vertices.
                        appendModelTriangle(outVertices, bounds, it, { &triangleVerts[0], &triangleVerts[1], &triangleVerts[2] });
                    }
                }
                else
                {
                    // This is a triangle fan.
                    numTriCmds *= -1;

                    for ( size_t vertexIndex = 0; numTriCmds > 0; --numTriCmds, ++vertexIndex )
                    {
                        if ( vertexIndex == 0 )
                        {
                            // Fill in the first vertex.
                            triangleVerts[0].readFrom(reader);

                            // Not enough vertices to create a triangle yet.
                            continue;
                        }
                        else if ( vertexIndex == 1 )
                        {
                            // Fill in the third vertex (so that we get the ordering correct).
                            triangleVerts[2].readFrom(reader);

                            // Not enough vertices to create a triangle yet.
                            continue;
                        }
                        else if ( vertexIndex == 2 )
                        {
                            // Fill in the second vertex.
                            triangleVerts[1].readFrom(reader);
                        }
                        else
                        {
                            // Shift the third vertex to be second, and read a new third vertex.
                            triangleVerts[2] = triangleVerts[1];
                            triangleVerts[1].readFrom(reader);
                        }

                        // Create a triangle out of these vertices.
                        appendModelTriangle(outVertices, bounds, it, { &triangleVerts[0], &triangleVerts[1], &triangleVerts[2] });
                    }
                }
            }
        }

        void AbmdlParser::appendModelTriangle(std::vector<Assets::EntityModelVertex>& outVertices, vm::bbox3f::builder& bounds, const MdlIterator& it, const TriangleVertexTrio& verts)
        {
            const TextureInfo& texInfo = m_textureInfos[it.iteratorIndex];

            // We need a vec3f for the position, and a vec2f for the texture co-ordinates, for each vertex.
            for ( size_t index = 0; index < 3; ++index )
            {
                const TriangleVertex& triVert = *(verts.v[index]);
                const size_t vertexIndex = triVert.vertexIndex;
                const size_t boneIndex = m_modelBoneIndices[vertexIndex];

                const mat3x4f& transformForBone = m_boneTransforms[boneIndex];
                const vm::vec3f& position = m_modelVertPositions[vertexIndex];
                const vm::vec3f transformedPosition = transformVector(position, transformForBone);

                const float u = (triVert.s + 1.0f) * (1.0f / static_cast<float>(texInfo.width));
                const float v = 1.0f - triVert.t * (1.0f / static_cast<float>(texInfo.height));
                const vm::vec2f texCoOrds(u, v);

                outVertices.emplace_back(transformedPosition, texCoOrds);
                bounds.add(transformedPosition);
            }
        }

        // I couldn't find these functions anywhere in the codebase, so they were ported from Xash3D.
        vm::quatf AbmdlParser::anglesToQuaternion(float pitchRad, float yawRad, float rollRad)
        {
            const float sinPitch = std::sin(pitchRad * 0.5f);
            const float cosPitch = std::cos(pitchRad * 0.5f);
            const float sinYaw = std::sin(yawRad * 0.5f);
            const float cosYaw = std::cos(yawRad * 0.5f);
            const float sinRoll = std::sin(rollRad * 0.5f);
            const float cosRoll = std::cos(rollRad * 0.5f);

            // Half Life quaternions are [x y z w], with w being the real part.
            const float qx = (sinPitch * cosYaw * cosRoll) - (cosPitch * sinYaw * sinRoll);
            const float qy = (cosPitch * sinYaw * cosRoll) + (sinPitch * cosYaw * sinRoll);
            const float qz = (cosPitch * cosYaw * sinRoll) - (sinPitch * sinYaw * cosRoll);
            const float qw = (cosPitch * cosYaw * cosRoll) - (sinPitch * sinYaw * sinRoll);

            return vm::quatf(qw, { qx, qy, qz });
        }

        AbmdlParser::mat3x4f AbmdlParser::quatAndOriginToMat(const vm::quatf& quat, const vm::vec3f& origin)
        {
            const float mat00 = 1.0f - 2.0f * quat.v[1] * quat.v[1] - 2.0f * quat.v[2] * quat.v[2];
            const float mat10 = 2.0f * quat.v[0] * quat.v[1] + 2.0f * quat.r * quat.v[2];
            const float mat20 = 2.0f * quat.v[0] * quat.v[2] - 2.0f * quat.r * quat.v[1];

            const float mat01 = 2.0f * quat.v[0] * quat.v[1] - 2.0f * quat.r * quat.v[2];
            const float mat11 = 1.0f - 2.0f * quat.v[0] * quat.v[0] - 2.0f * quat.v[2] * quat.v[2];
            const float mat21 = 2.0f * quat.v[1] * quat.v[2] + 2.0f * quat.r * quat.v[0];

            const float mat02 = 2.0f * quat.v[0] * quat.v[2] + 2.0f * quat.r * quat.v[1];
            const float mat12 = 2.0f * quat.v[1] * quat.v[2] - 2.0f * quat.r * quat.v[0];
            const float mat22 = 1.0f - 2.0f * quat.v[0] * quat.v[0] - 2.0f * quat.v[1] * quat.v[1];

            const float mat03 = origin[0];
            const float mat13 = origin[1];
            const float mat23 = origin[2];

            return mat3x4f(mat00, mat01, mat02, mat03,
                           mat10, mat11, mat12, mat13,
                           mat20, mat21, mat22, mat23);
        }

        void AbmdlParser::concat3x4Matrices(const mat3x4f& a, const mat3x4f b, mat3x4f& out)
        {
            const vm::vec3f& aC0 = a[0];
            const vm::vec3f& aC1 = a[1];
            const vm::vec3f& aC2 = a[2];
            const vm::vec3f& aC3 = a[3];

            for ( size_t outColIndex = 0; outColIndex < 4; ++outColIndex )
            {
                vm::vec3f& outCol = out[outColIndex];
                const vm::vec3f& bCol = b[outColIndex];

                outCol[0] = aC0[0] * bCol[0] + aC1[0] * bCol[1] + aC2[0] * bCol[2];
                outCol[1] = aC0[1] * bCol[0] + aC1[1] * bCol[1] + aC2[1] * bCol[2];
                outCol[2] = aC0[2] * bCol[0] + aC1[2] * bCol[1] + aC2[2] * bCol[2];

                if ( outColIndex == 3 )
                {
                    outCol[0] += aC3[0];
                    outCol[1] += aC3[1];
                    outCol[2] += aC3[2];
                }
            }
        }

        vm::vec3f AbmdlParser::transformVector(const vm::vec3f vec, const mat3x4f mat)
        {
           const float x = (vec[0] * mat[0][0]) + (vec[1] * mat[1][0]) + (vec[2] * mat[2][0]) + mat[3][0];
           const float y = (vec[0] * mat[0][1]) + (vec[1] * mat[1][1]) + (vec[2] * mat[2][1]) + mat[3][1];
           const float z = (vec[0] * mat[0][2]) + (vec[1] * mat[1][2]) + (vec[2] * mat[2][2]) + mat[3][2];

           return vm::vec3f(x, y, z);
        }
    }
}
