#include "Walnut/Application.h"
#include "Walnut/EntryPoint.h"

#include "Walnut/Image.h"
#include "Walnut/Timer.h"

#include "Renderer.h"
#include "Camera.h"

#include <glm/gtc/type_ptr.hpp>

using namespace Walnut;

class MainLayer : public Walnut::Layer
{
public:
	MainLayer()
		: m_Camera(45.0f, 0.1f, 100.0f)
	{
		
	}



	virtual void OnUpdate(float ts) override
	{
		if (m_Camera.OnUpdate(ts))
			m_Renderer.ResetFrameIndex();
	}

	virtual void OnUIRender() override
	{
		// Statistics window
		ImGui::Begin("Statistics");
		ImGui::Text("Avg. FPS: %.3f", m_AvgFps);
		ImGui::Text("Avg. Frame Time: %.3fms", m_avgRenderTime);
		ImGui::Text("Last Frame Time: %.3fms", m_LastRenderTime);
		ImGui::End();

		// Settings window
		ImGui::Begin("Settings");
		if (ImGui::Button("Render"))
		{
			Render();
		}
		ImGui::Checkbox("Accumulate", &m_Renderer.GetSettings().Accumulate);
		ImGui::Checkbox("Denoise", &m_Renderer.GetSettings().Denoise);
		ImGui::DragInt("Max Samples", &m_Renderer.GetSettings().MaxSamples, 1.0f, 1, 128);
		ImGui::DragInt("Max Bounces", &m_Renderer.GetSettings().MaxBounces, 1.0f, 1, 10);
		if (ImGui::Button("Reset"))
			m_Renderer.ResetFrameIndex();
		ImGui::End();

		// Scene window with collapsible sections
		ImGui::Begin("Scene");

		if (ImGui::CollapsingHeader("Spheres"))
		{
			for (size_t i = 0; i < m_Scene.Spheres.size(); i++)
			{
				ImGui::PushID(i);
				Sphere& sphere = m_Scene.Spheres[i];
				ImGui::DragFloat3("Position", glm::value_ptr(sphere.Position), 0.1f);
				ImGui::DragFloat("Radius", &sphere.Radius, 0.1f);
				ImGui::DragInt("Material", &sphere.MaterialIndex, 1.0f, 0, (int)m_Scene.Materials.size() - 1);
				ImGui::Separator();
				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Planes"))
		{
			for (size_t i = 0; i < m_Scene.Planes.size(); i++)
			{
				ImGui::PushID(i);
				Plane& plane = m_Scene.Planes[i];
				ImGui::DragFloat3("Position", glm::value_ptr(plane.Position), 0.1f);
				ImGui::DragFloat3("Normal", glm::value_ptr(plane.Normal), 0.1f);
				ImGui::DragInt("Material", &plane.MaterialIndex, 1.0f, 0, (int)m_Scene.Materials.size() - 1);
				ImGui::Separator();
				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Cuboids"))
		{
			for (size_t i = 0; i < m_Scene.Cuboids.size(); i++)
			{
				ImGui::PushID(i);
				Cuboid& cuboid = m_Scene.Cuboids[i];
				ImGui::DragFloat3("Position", glm::value_ptr(cuboid.Position), 0.1f);
				ImGui::DragFloat3("Dimensions", glm::value_ptr(cuboid.Dimensions), 0.1f);

				// Convert quaternion to Euler angles (in radians) then to degrees for UI
				glm::vec3 eulerRadians = glm::eulerAngles(cuboid.Rotation);
				glm::vec3 eulerDegrees = glm::degrees(eulerRadians);
				float euler[3] = { eulerDegrees.x, eulerDegrees.y, eulerDegrees.z };

				if (ImGui::DragFloat3("Rotation (Pitch, Yaw, Roll)", euler, 0.1f))
				{
					// Convert back to radians and update the quaternion
					glm::vec3 newEulerRadians = glm::radians(glm::vec3(euler[0], euler[1], euler[2]));
					cuboid.Rotation = glm::quat(newEulerRadians);
				}

				ImGui::DragInt("Material", &cuboid.MaterialIndex, 1, 0, (int)m_Scene.Materials.size() - 1);
				ImGui::Separator();
				ImGui::PopID();
			}
		}

		if (ImGui::CollapsingHeader("Materials"))
		{
			for (size_t i = 0; i < m_Scene.Materials.size(); i++)
			{
				ImGui::PushID(i);
				Material& material = m_Scene.Materials[i];
				ImGui::ColorEdit3("Albedo", glm::value_ptr(material.Albedo));
				ImGui::DragFloat("Roughness", &material.Roughness, 0.05f, 0.0f, 1.0f);
				ImGui::DragFloat("Metallic", &material.Metallic, 0.05f, 0.0f, 1.0f);
				ImGui::ColorEdit3("Emission Color", glm::value_ptr(material.EmissionColor));
				ImGui::DragFloat("Emission Power", &material.EmissionPower, 0.05f, 0.0f, FLT_MAX);
				ImGui::Separator();
				ImGui::PopID();
			}
		}
		ImGui::End();

		// Viewport window
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");
		m_ViewportWidth = ImGui::GetContentRegionAvail().x;
		m_ViewportHeight = ImGui::GetContentRegionAvail().y;
		auto image = m_Renderer.GetFinalImage();
		if (image)
			ImGui::Image(image->GetDescriptorSet(), { (float)image->GetWidth(), (float)image->GetHeight() },
				ImVec2(0, 1), ImVec2(1, 0));
		ImGui::End();
		ImGui::PopStyleVar();

		// Render the scene
		Render();
	}


	void Render()
	{
		static int frameCounter = 0;
		static double cumulativeTime = 0.0f;
		Timer timer;

		m_Renderer.OnResize(m_ViewportWidth, m_ViewportHeight);
		m_Camera.OnResize(m_ViewportWidth, m_ViewportHeight);
		m_Renderer.Render(m_Scene, m_Camera);

		m_LastRenderTime = timer.ElapsedMillis();
		if (cumulativeTime >= 1000.0f) {
			frameCounter = 0;
			cumulativeTime = 0.0f;
			m_avgRenderTime = 0.0f;
			m_AvgFps = 0.0f;
		}
		cumulativeTime += m_LastRenderTime;
		frameCounter++;

		m_avgRenderTime = cumulativeTime / frameCounter;
		m_AvgFps = 1000.0f / m_avgRenderTime;
	}
private:
	Renderer m_Renderer;
	Camera m_Camera;
	Scene m_Scene;
	uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

	float m_LastRenderTime = 0.0f;
	float m_avgRenderTime = 0.0f;
	float m_AvgFps = 0.0f;

};

Walnut::Application* Walnut::CreateApplication(int argc, char** argv)
{
	Walnut::ApplicationSpecification spec;
	spec.Name = "Ray Tracing";

	Walnut::Application* app = new Walnut::Application(spec);
	app->PushLayer<MainLayer>();
	app->SetMenubarCallback([app]()
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit"))
			{
				app->Close();
			}
			ImGui::EndMenu();
		}
	});
	return app;
}