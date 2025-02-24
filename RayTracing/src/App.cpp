#include "Walnut/Application.h"
#include "Walnut/EntryPoint.h"

#include "Walnut/Image.h"
#include "Walnut/Timer.h"

#include "Renderer.h"
#include "Camera.h"

#include <glm/gtc/type_ptr.hpp>

using namespace Walnut;

static bool hasChanged = false;

static void CheckChange(bool state)
{
	if (state) {
		hasChanged = true;
	}
}

class MainLayer : public Walnut::Layer
{
public:
	MainLayer()
		: m_Camera(45.0f, 0.1f, 100.0f)
	{
		Material& whiteSphere = m_Scene.Materials.emplace_back();
		whiteSphere.Albedo = { 1.0f, 0.0f, 1.0f };
		whiteSphere.Roughness = 0.0f;

		Material& blueSphere = m_Scene.Materials.emplace_back();
		blueSphere.Albedo = { 0.2f, 0.3f, 1.0f };
		blueSphere.Roughness = 0.1f;

		Material& OrangeBacklight = m_Scene.Materials.emplace_back();
		OrangeBacklight.Albedo = { 0.8f, 0.5f, 0.2f };
		OrangeBacklight.Roughness = 0.1f;
		OrangeBacklight.EmissionColor = OrangeBacklight.Albedo;
		OrangeBacklight.EmissionPower = 0.0f;

		Material& BlueBacklight = m_Scene.Materials.emplace_back();
		BlueBacklight.Albedo = { 0x87 / 255.0f, 0xce / 255.0f, 0xeb / 255.0f };
		BlueBacklight.Roughness = 0.1f;
		BlueBacklight.EmissionColor = BlueBacklight.Albedo;
		BlueBacklight.EmissionPower = 0.0f;

		{
			Sphere sphere;
			sphere.Position = { 0.0f, 0.0f, 0.0f };
			sphere.Radius = 1.0f;
			sphere.MaterialIndex = 0;
			m_Scene.Spheres.push_back(sphere);
		}

		{
			Sphere sphere;
			sphere.Position = { 0.0f, -101.0f, 0.0f };
			sphere.Radius = 100.0f;
			sphere.MaterialIndex = 1;
			m_Scene.Spheres.push_back(sphere);
		}

		{
			Sphere sphere;
			sphere.Position = { 90.0f, 50.0f, 20.0f };
			sphere.Radius = 15.0f;
			sphere.MaterialIndex = 2;
			m_Scene.Spheres.push_back(sphere);
		}

		{
			Sphere sphere;
			sphere.Position = { -50.0f, 150.0f, -18.0f };
			sphere.Radius = 250.0f;
			sphere.MaterialIndex = 3;
			m_Scene.Spheres.push_back(sphere);
		}
		m_Renderer.GetSettings().Sky = true;
		m_Renderer.GetSettings().SkyColor = { 0.8f, 0.8f, 0.8f };
		m_Renderer.GetSettings().MaxBounces = 5;
		m_Renderer.GetSettings().MaxSamples = 2;
	}

	virtual void OnUpdate(float ts) override
	{
		if (m_Camera.OnUpdate(ts))
			m_Renderer.ResetFrameIndex();
	}

	virtual void OnUIRender() override
	{
		hasChanged = false;
		static const auto& Statistics = m_Renderer.GetStatistics();
		ImGui::Begin("Statistics");
		ImGui::Text("Frame Num: %d", Statistics.FrameIndex);
		ImGui::Text("Avg. FPS: %.3f", Statistics.AvgFps);
		ImGui::Text("Avg. Frame Time: %.3fms", Statistics.AvgRenderTime);
		ImGui::Text("Last Frame Time: %.3fms", Statistics.LastRenderTime);
		ImGui::Text("Denoising Time: %.3fms", Statistics.DenoisingTime);
		ImGui::Text("Cumulative Time: %.5fs", Statistics.CumulativeTime / 1000.0f);
		ImGui::End();

		ImGui::Begin("Settings");
		if (ImGui::Button("Render"))
		{
			Render();
		}
		CheckChange(ImGui::Checkbox("Accumulate", &m_Renderer.GetSettings().Accumulate));
		CheckChange(ImGui::Checkbox("Denoise", &m_Renderer.GetSettings().Denoise));
		CheckChange(ImGui::Checkbox("Sky", &m_Renderer.GetSettings().Sky));
		CheckChange(ImGui::ColorEdit3("Sky Color", glm::value_ptr(m_Renderer.GetSettings().SkyColor)));
		CheckChange(ImGui::DragInt("Max Samples", &m_Renderer.GetSettings().MaxSamples, 1.0f, 1, 128));
		CheckChange(ImGui::DragInt("Max Bounces", &m_Renderer.GetSettings().MaxBounces, 1.0f, 1, 10));

		if (ImGui::Button("Reset"))
			m_Renderer.ResetFrameIndex();
		ImGui::End();

		ImGui::Begin("Filter Settings");
		static auto& filterSettings = m_Renderer.GetFilterSettings();
		CheckChange(ImGui::Checkbox("OIDN", &filterSettings.OIDN));
		CheckChange(ImGui::Checkbox("Gaussian Filter", &filterSettings.GaussianFilter));
		CheckChange(ImGui::Checkbox("WaveletFilter", &filterSettings.WaveletFilter));
		CheckChange(ImGui::Checkbox("Bilateral Filter", &filterSettings.JointBilateralFilter));
		CheckChange(ImGui::Checkbox("Unsharp Mask", &filterSettings.UnsharpMask));
		//CheckChange(ImGui::Checkbox("DNN Filter", &filterSettings.DNNFilter));
		CheckChange(ImGui::DragFloat("Gaussian Sigma", &filterSettings.GaussianSigma, 0.01f, 0.0f, 10.0f));
		CheckChange(ImGui::DragFloat("Bilateral Spatial Sigma", &filterSettings.BilateralSpatialSigma, 0.01f, 0.0f, 10.0f));
		CheckChange(ImGui::DragFloat("Bilateral Guide Sigma", &filterSettings.BilateralGuideSigma, 0.01f, 0.0f, 10.0f));
		CheckChange(ImGui::DragInt("Wavelet Passes", &filterSettings.WaveletPasses, 1, 1, 10));
		CheckChange(ImGui::DragFloat("Unsharp Amount", &filterSettings.UnsharpAmount, 0.01f, 0.0f, 10.0f));
		CheckChange(ImGui::DragFloat("Unsharp Blur Sigma", &filterSettings.UnsharpBlurSigma, 0.01f, 0.0f, 10.0f));
		ImGui::End();

		ImGui::Begin("Scene");

		if (ImGui::CollapsingHeader("Spheres"))
		{
			for (size_t i = 0; i < m_Scene.Spheres.size(); i++)
			{
				ImGui::PushID(i);
				Sphere& sphere = m_Scene.Spheres[i];
				CheckChange(ImGui::DragFloat3("Position", glm::value_ptr(sphere.Position), 0.1f));
				CheckChange(ImGui::DragFloat("Radius", &sphere.Radius, 0.1f));
				CheckChange(ImGui::DragInt("Material", &sphere.MaterialIndex, 1.0f, 0, (int)m_Scene.Materials.size() - 1));
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
				CheckChange(ImGui::DragFloat3("Position", glm::value_ptr(plane.Position), 0.1f));
				CheckChange(ImGui::DragFloat3("Normal", glm::value_ptr(plane.Normal), 0.1f));
				CheckChange(ImGui::DragInt("Material", &plane.MaterialIndex, 1.0f, 0, (int)m_Scene.Materials.size() - 1));
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
				CheckChange(ImGui::DragFloat3("Position", glm::value_ptr(cuboid.Position), 0.1f));
				CheckChange(ImGui::DragFloat3("Dimensions", glm::value_ptr(cuboid.Dimensions), 0.1f));

				glm::vec3 eulerRadians = glm::eulerAngles(cuboid.Rotation);
				glm::vec3 eulerDegrees = glm::degrees(eulerRadians);
				float euler[3] = { eulerDegrees.x, eulerDegrees.y, eulerDegrees.z };

				if (ImGui::DragFloat3("Rotation (Pitch, Yaw, Roll)", euler, 0.1f))
				{
					glm::vec3 newEulerRadians = glm::radians(glm::vec3(euler[0], euler[1], euler[2]));
					cuboid.Rotation = glm::quat(newEulerRadians);
					CheckChange(true);
				}

				CheckChange(ImGui::DragInt("Material", &cuboid.MaterialIndex, 1, 0, (int)m_Scene.Materials.size() - 1));
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
				CheckChange(ImGui::ColorEdit3("Albedo", glm::value_ptr(material.Albedo)));
				CheckChange(ImGui::DragFloat("Roughness", &material.Roughness, 0.05f, 0.0f, 1.0f));
				CheckChange(ImGui::DragFloat("Metallic", &material.Metallic, 0.05f, 0.0f, 1.0f));
				CheckChange(ImGui::ColorEdit3("Emission Color", glm::value_ptr(material.EmissionColor)));
				CheckChange(ImGui::DragFloat("Emission Power", &material.EmissionPower, 0.05f, 0.0f, FLT_MAX));
				ImGui::Separator();
				ImGui::PopID();
			}
		}
		ImGui::End();

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

		if (hasChanged)
		{
			m_Renderer.ResetFrameIndex();
		}

		Render();
	}


	void Render()
	{
		m_Renderer.OnResize(m_ViewportWidth, m_ViewportHeight);
		m_Camera.OnResize(m_ViewportWidth, m_ViewportHeight);
		m_Renderer.Render(m_Scene, m_Camera);
	}
private:
	Renderer m_Renderer;
	Camera m_Camera;
	Scene m_Scene;
	uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
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