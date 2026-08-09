#pragma once

//
// Ion Engine
// Public API Entry Point
//
// Copyright (c) 2026
//

// Core
#include <ion/core/Application.hpp>
#include <ion/core/Config.hpp>
#include <ion/core/Engine.hpp>
#include <ion/core/Error.hpp>
#include <ion/core/Log.hpp>
#include <ion/core/Memory.hpp>
#include <ion/core/Timer.hpp>
#include <ion/core/Version.hpp>

// Platform
#include <ion/platform/Window.hpp>
#include <ion/platform/Input.hpp>
#include <ion/platform/Paths.hpp>

// Rendering
#include <ion/render/Renderer.hpp>
#include <ion/render/Color.hpp>
#include <ion/render/Texture.hpp>
#include <ion/render/Image.hpp>
#include <ion/render/Camera.hpp>
#include <ion/render/Shader.hpp>
#include <ion/render/Buffer.hpp>
#include <ion/render/Vertex.hpp>
#include <ion/render/Mesh.hpp>
#include <ion/render/Material.hpp>
#include <ion/render/Model.hpp>
#include <ion/render/RenderCommand.hpp>
#include <ion/render/RenderTarget.hpp>
#include <ion/render/Light.hpp>
#include <ion/render/Skybox.hpp>

// 2D Rendering
#include <ion/render/Camera2D.hpp>
#include <ion/render/ParticleSystem.hpp>
#include <ion/render/PerformanceOverlay.hpp>
#include <ion/render/SpriteAnimation.hpp>
#include <ion/render/SpriteBatch.hpp>
#include <ion/render/SpriteRegion.hpp>
#include <ion/render/Svg.hpp>
#include <ion/render/Text.hpp>
#include <ion/render/TextureAtlas.hpp>
#include <ion/render/Tilemap.hpp>

// Math
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>
#include <ion/math/Matrix4.hpp>

// ECS
#include <ion/ecs/Entity.hpp>
#include <ion/ecs/Component.hpp>
#include <ion/ecs/System.hpp>