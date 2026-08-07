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
#include <ion/render/Camera.hpp>
#include <ion/render/Shader.hpp>
#include <ion/render/Buffer.hpp>
#include <ion/render/Vertex.hpp>
#include <ion/render/RenderCommand.hpp>

// Math
#include <ion/math/Vector2.hpp>
#include <ion/math/Vector3.hpp>
#include <ion/math/Vector4.hpp>
#include <ion/math/Matrix4.hpp>

// ECS
#include <ion/ecs/Entity.hpp>
#include <ion/ecs/Component.hpp>
#include <ion/ecs/System.hpp>