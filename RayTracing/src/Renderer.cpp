#include "Renderer.h"

#include "Walnut/Random.h"

#include <glm/gtx/component_wise.hpp>

#include <numeric>
#include <algorithm>
#include <execution>
#include <sstream>

namespace Utils 
{
	static inline uint32_t ConvertToRGBA(const glm::vec4& color)
	{
		const uint8_t r = (uint8_t)(color.r * 255.0f);
		const uint8_t g = (uint8_t)(color.g * 255.0f);
		const uint8_t b = (uint8_t)(color.b * 255.0f);
		const uint8_t a = (uint8_t)(color.a * 255.0f);

		const uint32_t result = (a << 24) | (b << 16) | (g << 8) | r;
		return result;
	}

	static inline glm::vec3 HexToRGB(const std::string& hex)
	{
		unsigned int r, g, b;
		std::stringstream ss;
		ss << std::hex << hex;
		ss >> r >> g >> b;
		return { r / 255.0f, g / 255.0f, b / 255.0f };
	}
}

void Renderer::OnResize(uint32_t width, uint32_t height)
{
	if (m_FinalImage)
	{
		// No resize necessary
		if (m_FinalImage->GetWidth() == width && m_FinalImage->GetHeight() == height)
			return;

		m_FinalImage->Resize(width, height);
	}
	else
	{
		m_FinalImage = std::make_shared<Walnut::Image>(width, height, Walnut::ImageFormat::RGBA);
	}

	delete[] m_ImageData;
	m_ImageData = new uint32_t[width * height];

	delete[] m_AccumulationData;
	m_AccumulationData = new glm::vec4[width * height];

	m_ImageVerticalIter.resize(height);
	m_FrameIndex = 1;
	std::iota(m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(), 0);
}

void Renderer::Render(Scene& scene, const Camera& camera)
{
	m_ActiveScene = &scene;
	m_ActiveCamera = &camera;
	
	if (m_FrameIndex == 1)
		memset(m_AccumulationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));

	std::for_each(std::execution::par, m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(),
		[this](uint32_t y)
		{
			for (uint32_t x = 0; x < m_FinalImage->GetWidth(); x++)
				{
					glm::vec4 color = PerPixel(x, y);
					m_AccumulationData[x + y * m_FinalImage->GetWidth()] += color;

					glm::vec4 accumulatedColor = m_AccumulationData[x + y * m_FinalImage->GetWidth()];
					accumulatedColor /= (float)m_FrameIndex;

					accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f));
					m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(accumulatedColor);
				};
		});


	m_FinalImage->SetData(m_ImageData);

	if (m_Settings.Accumulate)
		m_FrameIndex++;
	else
		m_FrameIndex = 1;
}

glm::vec4 Renderer::PerPixel(uint32_t x, uint32_t y)
{
	Ray ray;
	ray.Origin = m_ActiveCamera->GetPosition();
	ray.Direction = m_ActiveCamera->GetRayDirections()[x + y * m_FinalImage->GetWidth()];

	glm::vec3 light(0.0f);
	glm::vec3 contribution(1.0f);
	int bounces = 2;
	for (int i = 0; i < bounces; i++)
	{
		Renderer::HitPayload payload = TraceRay(ray);
		if (payload.HitDistance < 0.0f)
		{
			glm::vec3 skyColor = glm::vec3(0.6f, 0.7f, 0.9f);
			light += contribution * skyColor;
			break;
		}

		Material* material = nullptr;
		if (payload.ObjectType == HitPayload::ObjectType::Sphere)
		{
			const Sphere& sphere = m_ActiveScene->Spheres[payload.ObjectIndex];
			material = &m_ActiveScene->Materials[sphere.MaterialIndex];
		}
		else if (payload.ObjectType == HitPayload::ObjectType::Plane)
		{
			const Plane& plane = m_ActiveScene->Planes[payload.ObjectIndex];
			material = &m_ActiveScene->Materials[plane.MaterialIndex];
		}
		else if (payload.ObjectType == HitPayload::ObjectType::Cuboid)
		{
			const Cuboid& cuboid = m_ActiveScene->Cuboids[payload.ObjectIndex];
			material = &m_ActiveScene->Materials[cuboid.MaterialIndex];
		}

		if (m_FrameIndex == -1)
		{
			memset(m_AccumulationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));
			m_FrameIndex = 1;
		}

		if (material->Metallic > 0.0f)
		{
			ray.Origin = payload.WorldPosition + payload.WorldNormal * 0.0001f;
			ray.Direction = glm::reflect(ray.Direction, payload.WorldNormal);
		}

		contribution *= material->Albedo;
		light += material->GetEmission();
		ray.Origin = payload.WorldPosition + payload.WorldNormal * 0.0001f;
		ray.Direction = glm::normalize(payload.WorldNormal + Walnut::Random::InUnitSphere());
	}
	return { light, 1.0f };
}

Renderer::HitPayload Renderer::TraceRay(const Ray& ray)
{
	// (bx^2 + by^2)t^2 + (2(axbx + ayby))t + (ax^2 + ay^2 - r^2) = 0
	// where
	// a = ray origin
	// b = ray direction
	// r = radius
	// t = hit distance
	
	int objectIndex = -1;
	const Sphere* closestSphere = nullptr;
	const Plane* closestPlane = nullptr;
	const Cuboid* closestCuboid = nullptr;
	float hitDistance = std::numeric_limits<float>::max();
	for (size_t i = 0; i < m_ActiveScene->Spheres.size(); i++)
	{
		const Sphere& sphere = m_ActiveScene->Spheres[i];
		glm::vec3 origin = ray.Origin - sphere.Position;

		float a = glm::dot(ray.Direction, ray.Direction);
		float b = 2.0f * glm::dot(origin, ray.Direction);
		float c = glm::dot(origin, origin) - sphere.Radius * sphere.Radius;

		// Quadratic forumula discriminant:
		// b^2 - 4ac

		float discriminant = b * b - 4.0f * a * c;
		if (discriminant < 0.0f)
			continue;

		// Quadratic formula:
		// (-b +- sqrt(discriminant)) / 2a

		// float t0 = (-b + glm::sqrt(discriminant)) / (2.0f * a); // Second hit distance (currently unused)
		float closestT = (-b - glm::sqrt(discriminant)) / (2.0f * a);
		if (closestT > 0.0f && closestT < hitDistance)
		{
			objectIndex = static_cast<int>(i);
			hitDistance = closestT;
			closestSphere = &sphere;
		}
	}

	for (size_t i = 0; i < m_ActiveScene->Planes.size(); i++)
	{
		const Plane& plane = m_ActiveScene->Planes[i];
		float denom = glm::dot(plane.Normal, ray.Direction);
		if (denom > 1e-6)
		{
			glm::vec3 p0l0 = plane.Position - ray.Origin;
			float t = glm::dot(p0l0, plane.Normal) / denom;
			if (t >= 0.0f && t < hitDistance)
			{
				objectIndex = static_cast<int>(i);
				hitDistance = t;
				closestPlane = &plane;
				closestSphere = nullptr;
			}
		}
	}

	for (size_t i = 0; i < m_ActiveScene->Cuboids.size(); i++)
	{
		const Cuboid& cuboid = m_ActiveScene->Cuboids[i];
		glm::vec3 p = cuboid.Position;
		glm::vec3 d = cuboid.Dimensions;
		glm::vec3 t0 = (p - ray.Origin) / ray.Direction;
		glm::vec3 t1 = (p + d - ray.Origin) / ray.Direction;
		glm::vec3 tmin = glm::min(t0, t1);
		glm::vec3 tmax = glm::max(t0, t1);
		float tminmax = glm::compMax(tmin);
		float tmaxmin = glm::compMin(tmax);
		if (tminmax <= tmaxmin && tmaxmin > 0.0f && tmaxmin < hitDistance)
		{
			objectIndex = static_cast<int>(i);
			closestCuboid = &cuboid;
			hitDistance = tmaxmin;
			closestSphere = nullptr;
			closestPlane = nullptr;
		}
	}

	if (closestSphere != nullptr)
		return ClosestHit(ray, hitDistance, objectIndex, closestSphere);
	else if (closestPlane != nullptr)
		return ClosestHit(ray, hitDistance, objectIndex, closestPlane);
	else if (closestCuboid != nullptr)
		return ClosestHit(ray, hitDistance, objectIndex, closestCuboid);
	
	return Miss(ray);
}

Renderer::HitPayload Renderer::ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Sphere* sphere)
{
	Renderer::HitPayload payload;
	payload.HitDistance = hitDistance;
	payload.ObjectIndex = objectIndex;
	payload.ObjectType = HitPayload::ObjectType::Sphere;

	const Sphere& closestSphere = m_ActiveScene->Spheres[objectIndex];

	glm::vec3 origin = ray.Origin - closestSphere.Position;
	payload.WorldPosition = origin + ray.Direction * hitDistance;
	payload.WorldNormal = glm::normalize(payload.WorldPosition);

	payload.WorldPosition += closestSphere.Position;

	return payload;
}

Renderer::HitPayload Renderer::ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Plane* plane)
{
	Renderer::HitPayload payload;
	payload.HitDistance = hitDistance;
	payload.ObjectIndex = objectIndex;
	payload.ObjectType = HitPayload::ObjectType::Plane;

	const Plane& closestPlane = m_ActiveScene->Planes[objectIndex];

	payload.WorldPosition = ray.Origin + ray.Direction * hitDistance;
	payload.WorldNormal = closestPlane.Normal;
	return payload;
}

Renderer::HitPayload Renderer::ClosestHit(const Ray& ray, float hitDistance, int objectIndex, const Cuboid* cuboid)
{
	Renderer::HitPayload payload;
	payload.HitDistance = hitDistance;
	payload.ObjectIndex = objectIndex;
	payload.ObjectType = HitPayload::ObjectType::Cuboid; // Make sure your enum includes Cuboid

	const Cuboid& hitCuboid = m_ActiveScene->Cuboids[objectIndex];

	// Compute the hit point in world space
	glm::vec3 hitPoint = ray.Origin + ray.Direction * hitDistance;
	payload.WorldPosition = hitPoint;

	// Transform the hit point into the cuboid's local space
	glm::quat invRot = glm::inverse(hitCuboid.Rotation);
	glm::vec3 localPoint = invRot * (hitPoint - hitCuboid.Position);

	// Cuboid defined as centered at origin with half dimensions
	glm::vec3 halfDims = hitCuboid.Dimensions * 0.5f;

	// Determine which face is hit by computing the difference from the face boundary
	float diffX = fabs(fabs(localPoint.x) - halfDims.x);
	float diffY = fabs(fabs(localPoint.y) - halfDims.y);
	float diffZ = fabs(fabs(localPoint.z) - halfDims.z);

	glm::vec3 localNormal(0.0f);
	if (diffX < diffY && diffX < diffZ)
	{
		// Hit on the X face
		localNormal = glm::vec3((localPoint.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
	}
	else if (diffY < diffZ)
	{
		// Hit on the Y face
		localNormal = glm::vec3(0.0f, (localPoint.y > 0.0f) ? 1.0f : -1.0f, 0.0f);
	}
	else
	{
		// Hit on the Z face
		localNormal = glm::vec3(0.0f, 0.0f, (localPoint.z > 0.0f) ? 1.0f : -1.0f);
	}

	// Transform the normal back to world space using the cuboid's rotation
	payload.WorldNormal = glm::normalize(hitCuboid.Rotation * localNormal);

	return payload;
}


Renderer::HitPayload Renderer::Miss(const Ray& ray)
{
	Renderer::HitPayload payload;
	payload.HitDistance = -1.0f;
	return payload;
}
