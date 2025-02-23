#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <RayTracer/App.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <vector>
#include <queue>
#include <set>

void App::AfterCreatedUiContext()
{
}

void App::BeforeDestroyUiContext()
{
}

bool App::Load()
{
	if (!BaseApp::Load()) {
		spdlog::error("App: Unable to load");
		return false;
	}

	return true;
}

void App::Update(float deltaTime)
{
	if (IsKeyPressed(GLFW_KEY_ESCAPE)) {
		Close();
	}

	_elapsedTime += deltaTime;
}

void App::RenderScene([[maybe_unused]] float deltaTime)
{
	const auto projection = glm::perspective(
		glm::radians(80.0f), 1920.0f / 1080.0f, 0.1f, 256.0f);
	const auto view =
		glm::lookAt(glm::vec3(3 * std::cos(glfwGetTime() / 4), 2,
				      -3 * std::sin(glfwGetTime() / 4)),
			    glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUniformMatrix4fv(0, 1, false, glm::value_ptr(projection));
	glUniformMatrix4fv(1, 1, false, glm::value_ptr(view));
}

void App::RenderUI(float deltaTime)
{
	ImGui::Begin("Window");
	{
		ImGui::TextUnformatted("Hello World!");
		ImGui::Text("Time in seconds since startup: %f", _elapsedTime);
		ImGui::Text("The delta time between frames: %f", deltaTime);
		ImGui::End();
	}
}
