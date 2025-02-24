#pragma once

#include "Walnut/Image.h"

#include "Camera.h"
#include "Ray.h"
#include "Scene.h"

#include <memory>
#include <glm/glm.hpp>

class Renderer
{
public:
	struct FilterSettings
	{
		float GaussianSigma = 1.5f;
		float BilateralSpatialSigma = 2.5f;
		float BilateralGuideSigma = 0.1f;
		float UnsharpAmount = 0.4f;
		float UnsharpBlurSigma = 0.8f;
		int WaveletPasses = 3;
		bool GaussianFilter = false;
		bool JointBilateralFilter = false;
		bool UnsharpMask = false;
		bool WaveletFilter = false;
		bool DNNFilter = false;
		bool OIDN = false;
	};
private:
	std::shared_ptr<Walnut::Image> m_FinalImage;

	Scene* m_ActiveScene = nullptr;
	const Camera* m_ActiveCamera = nullptr;

	uint32_t* m_ImageData = nullptr;
	glm::vec4* m_HistoryData = nullptr;
	glm::vec4* m_AccumulationData = nullptr;
	glm::vec3* m_NormalData = nullptr;
	glm::vec3* m_AlbedoData = nullptr;

private:
	struct Settings
	{
		int MaxSamples = 4;
		int MaxBounces = 2;
		bool Accumulate = true;
		bool Denoise = false;
		bool Sky = false;
		glm::vec3 SkyColor{ 0x87 / 255.0f, 0xce / 255.0f, 0xeb / 255.0f };
		glm::vec3 AmbientLight{ 0.1f };
	} m_Settings;

	struct Statistics
	{
		float AvgFps = 0.0f;
		float AvgRenderTime = 0.0f;
		float LastRenderTime = 0.0f;
		float DenoisingTime = 0.0f;
		float CumulativeTime = 0.0f;
		int FrameIndex = 0;
	} m_Statistics;

	struct HitPayload
	{
		float HitDistance;
		glm::vec3 WorldPosition;
		glm::vec3 WorldNormal;

		int ObjectIndex;
		enum class ObjectType
		{
			None,
			Sphere,
			Plane,
			Cuboid
		} ObjectType;
	};

	glm::vec4 RayGen(uint32_t x, uint32_t y);

	HitPayload TraceRay(const Ray& ray);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Sphere* sphere);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Plane* plane);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Cuboid* cuboid);
	HitPayload Miss(const Ray& ray);

	void denoise();
	void OIDNDenoise();
	FilterSettings m_FilterSettings;
public:
	Renderer();

	void OnResize(uint32_t width, uint32_t height);
	void Render(Scene& scene, const Camera& camera);

	std::shared_ptr<Walnut::Image> GetFinalImage() const { return m_FinalImage; }

	void ResetFrameIndex() { m_Statistics.FrameIndex = 0; }
	Settings& GetSettings() { return m_Settings; }
	FilterSettings& GetFilterSettings() { return m_FilterSettings; }
	const Statistics& GetStatistics() const { return m_Statistics; }
};