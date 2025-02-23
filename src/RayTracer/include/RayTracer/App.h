#pragma once

#include <RayTracerLib/BaseApp.hpp>

#include <RayTracer/Model.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

#include <string_view>
#include <vector>
#include <memory>

class App final : public BaseApp {
    protected:
	void AfterCreatedUiContext() override;
	void BeforeDestroyUiContext() override;
	bool Load() override;
	void RenderScene(float deltaTime) override;
	void RenderUI(float deltaTime) override;
	void Update(float deltaTime) override;

    private:
	float _elapsedTime = 0.0f;

    public:
	App() = default;
};