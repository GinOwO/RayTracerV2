#pragma once

#include "Walnut/Image.h"

#include "Camera.h"
#include "Ray.h"
#include "Scene.h"

#include <memory>
#include <glm/glm.hpp>

class Renderer
{
private:
	std::shared_ptr<Walnut::Image> m_FinalImage;

	std::vector<uint32_t> m_ImageVerticalIter;

	Scene* m_ActiveScene = nullptr;
	const Camera* m_ActiveCamera = nullptr;

	uint32_t* m_ImageData = nullptr;
	glm::vec4* m_AccumulationData = nullptr;

	uint32_t m_FrameIndex = 1;
private:
	struct Settings
	{
		int MaxSamples = 4;
		int MaxBounces = 2;
		bool Accumulate = true;
		bool Denoise = false;
	} m_Settings;

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

	glm::vec4 PerPixel(uint32_t x, uint32_t y); // RayGen

	HitPayload TraceRay(const Ray& ray);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Sphere* sphere);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Plane* plane);
	HitPayload ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Cuboid* cuboid);
	HitPayload Miss(const Ray& ray);
public:
	Renderer() = default;

	void OnResize(uint32_t width, uint32_t height);
	void Render(Scene& scene, const Camera& camera);

	std::shared_ptr<Walnut::Image> GetFinalImage() const { return m_FinalImage; }

	void ResetFrameIndex() { m_FrameIndex = 1; }
	Settings& GetSettings() { return m_Settings; }
};