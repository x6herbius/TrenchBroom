/*
 Copyright (C) 2010-2014 Kristian Duske
 
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

#ifndef __TrenchBroom__ClipTool__
#define __TrenchBroom__ClipTool__

#include "TrenchBroom.h"
#include "VecMath.h"
#include "Model/Hit.h"
#include "Model/ModelTypes.h"
#include "View/Tool.h"
#include "View/ViewTypes.h"

namespace TrenchBroom {
    namespace Renderer {
        class BrushRenderer;
        class Camera;
        class RenderBatch;
        class RenderContext;
    }
    
    namespace Model {
        class ModelFactory;
        class PickResult;
    }
    
    namespace View {
        class Selection;
        
        class ClipTool : public Tool {
        public:
            static const Model::Hit::HitType PointHit;

            class PointSnapper {
            public:
                virtual ~PointSnapper();
                bool snap(const Vec3& point, Vec3& result) const;
            private:
                virtual bool doSnap(const Vec3& point, Vec3& result) const = 0;
            };
        private:
            enum ClipSide {
                ClipSide_Front,
                ClipSide_Both,
                ClipSide_Back
            };

            class ClipStrategy {
            public:
                virtual ~ClipStrategy();
                void pick(const Ray3& pickRay, const Renderer::Camera& camera, Model::PickResult& pickResult) const;
                void render(Renderer::RenderContext& renderContext, Renderer::RenderBatch& renderBatch, const Model::PickResult& pickResult);
                
                Vec3 helpVector() const;
                bool computeThirdPoint(Vec3& point) const;

                bool canClip() const;
                bool canAddPoint(const Vec3& point, const PointSnapper& snapper) const;
                void addPoint(const Vec3& point, const PointSnapper& snapper, const Vec3& helpVector);
                void removeLastPoint();
                bool beginDragPoint(const Model::PickResult& pickResult, Vec3& initialPosition);
                bool dragPoint(const Vec3& newPosition, const PointSnapper& snapper, const Vec3& helpVector, Vec3& snappedPosition);
                bool setFace(const Model::BrushFace* face);
                void reset();
                size_t getPoints(Vec3& point1, Vec3& point2, Vec3& point3) const;
            private:
                virtual void doPick(const Ray3& pickRay, const Renderer::Camera& camera, Model::PickResult& pickResult) const = 0;
                virtual void doRender(Renderer::RenderContext& renderContext, Renderer::RenderBatch& renderBatch, const Model::PickResult& pickResult) = 0;

                virtual Vec3 doGetHelpVector() const = 0;
                virtual bool doComputeThirdPoint(Vec3& point) const = 0;

                virtual bool doCanClip() const = 0;
                virtual bool doCanAddPoint(const Vec3& point, const PointSnapper& snapper) const = 0;
                virtual void doAddPoint(const Vec3& point, const PointSnapper& snapper, const Vec3& helpVector) = 0;
                virtual void doRemoveLastPoint() = 0;
                virtual bool doBeginDragPoint(const Model::PickResult& pickResult, Vec3& initialPosition) = 0;
                virtual bool doDragPoint(const Vec3& newPosition, const PointSnapper& snapper, const Vec3& helpVector, Vec3& snappedPosition) = 0;
                virtual bool doSetFace(const Model::BrushFace* face) = 0;
                virtual void doReset() = 0;
                virtual size_t doGetPoints(Vec3& point1, Vec3& point2, Vec3& point3) const = 0;
            };
            
            class PointClipStrategy;
            class FaceClipStrategy;
        private:
            MapDocumentWPtr m_document;
            
            ClipSide m_clipSide;
            ClipStrategy* m_strategy;

            Model::ParentChildrenMap m_frontBrushes;
            Model::ParentChildrenMap m_backBrushes;
            
            Renderer::BrushRenderer* m_remainingBrushRenderer;
            Renderer::BrushRenderer* m_clippedBrushRenderer;
            
            bool m_ignoreNotifications;
        public:
            ClipTool(MapDocumentWPtr document);
            ~ClipTool();
            
            void toggleSide();
            void resetSide();
            
            void pick(const Ray3& pickRay, const Renderer::Camera& camera, Model::PickResult& pickResult);
            
            void render(Renderer::RenderContext& renderContext, Renderer::RenderBatch& renderBatch, const Model::PickResult& pickResult);
        private:
            void renderBrushes(Renderer::RenderContext& renderContext, Renderer::RenderBatch& renderBatch);
            void renderStrategy(Renderer::RenderContext& renderContext, Renderer::RenderBatch& renderBatch, const Model::PickResult& pickResult);
        public:
            bool canClip() const;
            void performClip();
        private:
            Model::ParentChildrenMap clipBrushes();
        public:
            Vec3 defaultClipPointPos() const;
            Vec3 helpVector() const;

            bool canAddPoint(const Vec3& point, const PointSnapper& snapper) const;
            void addPoint(const Vec3& point, const PointSnapper& snapper, const Vec3& helpVector);
            void removeLastPoint();
            
            bool beginDragPoint(const Model::PickResult& pickResult, Vec3& initialPosition);
            bool dragPoint(const Vec3& newPosition, const PointSnapper& snapper, const Vec3& helpVector, Vec3& snappedPosition);
            
            void setFace(const Model::BrushFace* face);
            bool reset();
        private:
            void resetStrategy();
            void update();

            void clearBrushes();
            void updateBrushes();
            
            void setFaceAttributes(const Model::BrushFaceList& faces, Model::BrushFace* frontFace, Model::BrushFace* backFace) const;
            
            void clearRenderers();
            void updateRenderers();
            
            class AddBrushesToRendererVisitor;
            void addBrushesToRenderer(const Model::ParentChildrenMap& map, Renderer::BrushRenderer* renderer);
            
            bool keepFrontBrushes() const;
            bool keepBackBrushes() const;
        private:
            bool doActivate();
            bool doDeactivate();
            
            void bindObservers();
            void unbindObservers();
            void selectionDidChange(const Selection& selection);
            void nodesWillChange(const Model::NodeList& nodes);
            void nodesDidChange(const Model::NodeList& nodes);
        };
    }
}

#endif /* defined(__TrenchBroom__ClipTool__) */
