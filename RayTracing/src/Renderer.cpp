#include "Renderer.h"

#include "Walnut/Random.h"
#include "Walnut/Timer.h"

#include <OpenImageDenoise/oidn.hpp>

#include <glm/gtx/component_wise.hpp>

#include <iostream>
#include <numeric>
#include <algorithm>
#include <execution>
#include <sstream>
#include <glm/gtx/norm.hpp>

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

static std::vector<int> pixels;;

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

	delete[] m_HistoryData;
	m_HistoryData = new glm::vec4[width * height];

	delete[] m_NormalData;
	m_NormalData = new glm::vec3[width * height];

	delete[] m_AlbedoData;
	m_AlbedoData = new glm::vec3[width * height];

	m_ImageVerticalIter.resize(height);
	m_Statistics.FrameIndex = 1;
	std::iota(m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(), 0);

	pixels.resize(width * height);
	std::iota(pixels.begin(), pixels.end(), 0);
}

void Renderer::Render(Scene& scene, const Camera& camera)
{
	m_ActiveScene = &scene;
	m_ActiveCamera = &camera;
	static bool denoised = false;
	if (m_Statistics.FrameIndex >= m_Settings.MaxSamples && m_Settings.MaxSamples > 0) {
		if (m_Settings.Denoise && !denoised) {
			Walnut::Timer timer;
			denoise();
			denoised = true;
			m_Statistics.DenoisingTime = timer.ElapsedMillis();
		}
		m_FinalImage->SetData(m_ImageData);
		return;
	}
	denoised = false;

	Walnut::Timer timer;
	if (m_Settings.Accumulate)
		m_Statistics.FrameIndex++;
	else
		m_Statistics.FrameIndex = 1;
	
	if (m_Statistics.FrameIndex == 1) {
		memset(m_AccumulationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));
		memset(m_HistoryData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));
		memset(m_AlbedoData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec3));
		memset(m_NormalData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec3));
	}

	//for (uint32_t cnt = 0; cnt < m_Settings.MaxSamples; cnt++)
	std::for_each(std::execution::par, m_ImageVerticalIter.begin(), m_ImageVerticalIter.end(),
		[this](uint32_t y)
		{
			for (uint32_t x = 0; x < m_FinalImage->GetWidth(); x++)
				{
					glm::vec4 color = RayGen(x, y);
					m_AccumulationData[x + y * m_FinalImage->GetWidth()] += color;

					glm::vec4 accumulatedColor = m_AccumulationData[x + y * m_FinalImage->GetWidth()];
					accumulatedColor /= (float)m_Statistics.FrameIndex;

					accumulatedColor = glm::clamp(accumulatedColor, glm::vec4(0.0f), glm::vec4(1.0f));
					m_ImageData[x + y * m_FinalImage->GetWidth()] = Utils::ConvertToRGBA(accumulatedColor);
				};
		});

	m_FinalImage->SetData(m_ImageData);
	m_Statistics.LastRenderTime = timer.ElapsedMillis();
	if (m_Statistics.FrameIndex == 1) {
		m_Statistics.CumulativeTime = 0;
		m_Statistics.AvgRenderTime = 0;
		m_Statistics.AvgFps = 0;
	}
	m_Statistics.CumulativeTime += m_Statistics.LastRenderTime;
	m_Statistics.AvgRenderTime = ((m_Statistics.AvgRenderTime * (m_Statistics.FrameIndex - 1)) + m_Statistics.LastRenderTime) / m_Statistics.FrameIndex;
	m_Statistics.AvgFps = 1000.0f / m_Statistics.AvgRenderTime;
}

glm::vec4 Renderer::RayGen(uint32_t x, uint32_t y)
{
	Ray ray;
	ray.Origin = m_ActiveCamera->GetPosition();
	ray.Direction = m_ActiveCamera->GetRayDirections()[x + y * m_FinalImage->GetWidth()];

	glm::vec3 light(0.0f);
	glm::vec3 contribution(1.0f);
	for (int i = 0; i < m_Settings.MaxBounces; i++)
	{
		Renderer::HitPayload payload = TraceRay(ray);
		if (payload.HitDistance < 0.0f)
		{
			if (m_Settings.Sky)
				light += contribution * m_Settings.SkyColor;
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
		m_NormalData[x + y * m_FinalImage->GetWidth()] = glm::normalize(payload.WorldNormal);
		m_AlbedoData[x + y * m_FinalImage->GetWidth()] = material->Albedo;

		if (m_Statistics.FrameIndex == -1)
		{
			memset(m_AccumulationData, 0, m_FinalImage->GetWidth() * m_FinalImage->GetHeight() * sizeof(glm::vec4));
			m_Statistics.FrameIndex = 1;
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
	payload.ObjectType = HitPayload::ObjectType::Cuboid;

	const Cuboid& hitCuboid = m_ActiveScene->Cuboids[objectIndex];

	glm::vec3 hitPoint = ray.Origin + ray.Direction * hitDistance;
	payload.WorldPosition = hitPoint;

	glm::quat invRot = glm::inverse(hitCuboid.Rotation);
	glm::vec3 localPoint = invRot * (hitPoint - hitCuboid.Position);

	glm::vec3 halfDims = hitCuboid.Dimensions * 0.5f;

	float diffX = fabs(fabs(localPoint.x) - halfDims.x);
	float diffY = fabs(fabs(localPoint.y) - halfDims.y);
	float diffZ = fabs(fabs(localPoint.z) - halfDims.z);

	glm::vec3 localNormal(0.0f);
	if (diffX < diffY && diffX < diffZ)
	{
		localNormal = glm::vec3((localPoint.x > 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
	}
	else if (diffY < diffZ)
	{
		localNormal = glm::vec3(0.0f, (localPoint.y > 0.0f) ? 1.0f : -1.0f, 0.0f);
	}
	else
	{
		localNormal = glm::vec3(0.0f, 0.0f, (localPoint.z > 0.0f) ? 1.0f : -1.0f);
	}

	payload.WorldNormal = glm::normalize(hitCuboid.Rotation * localNormal);

	return payload;
}

Renderer::HitPayload Renderer::Miss(const Ray& ray)
{
	Renderer::HitPayload payload;
	payload.HitDistance = -1.0f;
	return payload;
}


static void BilateralFilter(uint32_t width, uint32_t height, glm::vec4* accumulationData)
{
	std::vector<glm::vec4> temp(width * height);

	const float spatialSigma = 3.0f;
	const float colorSigma = 0.1f;
	int radius = static_cast<int>(std::ceil(2 * spatialSigma));

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			int index = y * width + x;
			glm::vec4 centerColor = accumulationData[index];
			glm::vec4 sumColor(0.0f);
			float sumWeight = 0.0f;

			for (int j = -radius; j <= radius; ++j)
			{
				for (int i = -radius; i <= radius; ++i)
				{
					int sampleX = x + i;
					int sampleY = y + j;

					if (sampleX < 0 || sampleX >= static_cast<int>(width) ||
						sampleY < 0 || sampleY >= static_cast<int>(height))
						continue;

					int sampleIndex = sampleY * width + sampleX;
					glm::vec4 sampleColor = accumulationData[sampleIndex];

					float spatialDist2 = static_cast<float>(i * i + j * j);
					float spatialWeight = std::exp(-spatialDist2 / (2.0f * spatialSigma * spatialSigma));

					glm::vec4 diff = sampleColor - centerColor;
					float colorDist2 = glm::dot(diff, diff);
					float colorWeight = std::exp(-colorDist2 / (2.0f * colorSigma * colorSigma));

					float weight = spatialWeight * colorWeight;;
					sumColor += sampleColor * weight;
					sumWeight += weight;
				}
			}
			temp[index] = sumColor / sumWeight;
		}
	}
	std::memcpy(accumulationData, temp.data(), width * height * sizeof(glm::vec4));
}

static void GaussianBlurFilter(uint32_t width, uint32_t height, glm::vec4* accumulationData)
{
	std::vector<glm::vec4> horizontal(width * height);
	std::vector<glm::vec4> temp(width * height);

	const float sigma = 1.0f;
	int radius = static_cast<int>(std::ceil(3 * sigma));

	std::vector<float> kernel(2 * radius + 1);
	float kernelSum = 0.0f;
	for (int i = -radius; i <= radius; ++i)
	{
		float value = std::exp(-(i * i) / (2.0f * sigma * sigma));
		kernel[i + radius] = value;
		kernelSum += value;
	}

	for (float& v : kernel)
		v /= kernelSum;

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			glm::vec4 sum(0.0f);
			float weightSum = 0.0f;
			for (int i = -radius; i <= radius; ++i)
			{
				int sampleX = x + i;
				if (sampleX < 0 || sampleX >= static_cast<int>(width))
					continue;
				float weight = kernel[i + radius];
				sum += accumulationData[y * width + sampleX] * weight;
				weightSum += weight;
			}
			horizontal[y * width + x] = sum / weightSum;
		}
	}

	for (uint32_t y = 0; y < height; ++y)
	{
		for (uint32_t x = 0; x < width; ++x)
		{
			glm::vec4 sum(0.0f);
			float weightSum = 0.0f;
			for (int j = -radius; j <= radius; ++j)
			{
				int sampleY = y + j;
				if (sampleY < 0 || sampleY >= static_cast<int>(height))
					continue;
				float weight = kernel[j + radius];
				sum += horizontal[sampleY * width + x] * weight;
				weightSum += weight;
			}
			temp[y * width + x] = sum / weightSum;
		}
	}

	std::memcpy(accumulationData, temp.data(), width * height * sizeof(glm::vec4));
}

void Renderer::OIDNDenoise()
{
	uint32_t width = m_FinalImage->GetWidth();
	uint32_t height = m_FinalImage->GetHeight();

	oidn::DeviceRef device = oidn::newDevice();
	device.commit();

	// Prepare buffers for color
	size_t bufferSize = width * height * 3 * sizeof(float);
	oidn::BufferRef colorBuffer = device.newBuffer(bufferSize);
	oidn::BufferRef outputBuffer = device.newBuffer(bufferSize);
	float* colorData = static_cast<float*>(colorBuffer.getData());
	float* outputData = static_cast<float*>(outputBuffer.getData());

	// Prepare buffers for normal and albedo layers
	oidn::BufferRef normalBuffer = device.newBuffer(bufferSize);
	oidn::BufferRef albedoBuffer = device.newBuffer(bufferSize);
	float* normalData = static_cast<float*>(normalBuffer.getData());
	float* albedoData = static_cast<float*>(albedoBuffer.getData());

	// Copy color data (as before)
	std::for_each(pixels.begin(), pixels.end(), [&](int i)
		{
			colorData[i * 3 + 0] = m_AccumulationData[i].r;
			colorData[i * 3 + 1] = m_AccumulationData[i].g;
			colorData[i * 3 + 2] = m_AccumulationData[i].b;
		});

	// Copy normal data.
	// Note: OIDN expects normals to be normalized. Typically, you can use the raw normal,
	// or you may remap them from [-1,1] to [0,1] if needed by your pipeline.
	std::for_each(pixels.begin(), pixels.end(), [&](int i)
		{
			// Assuming m_NormalData is glm::vec3; if stored as glm::vec3, no alpha.
			normalData[i * 3 + 0] = m_NormalData[i].x;
			normalData[i * 3 + 1] = m_NormalData[i].y;
			normalData[i * 3 + 2] = m_NormalData[i].z;
		});

	// Copy albedo data.
	std::for_each(pixels.begin(), pixels.end(), [&](int i)
		{
			albedoData[i * 3 + 0] = m_AlbedoData[i].r;
			albedoData[i * 3 + 1] = m_AlbedoData[i].g;
			albedoData[i * 3 + 2] = m_AlbedoData[i].b;
		});

	// Create and configure the OIDN filter.
	oidn::FilterRef filter = device.newFilter("RT");
	filter.setImage("color", colorData, oidn::Format::Float3, width, height);
	filter.setImage("normal", normalData, oidn::Format::Float3, width, height);
	filter.setImage("albedo", albedoData, oidn::Format::Float3, width, height);
	filter.setImage("output", outputData, oidn::Format::Float3, width, height);

	// Set additional filter parameters if necessary (for example, HDR flag)
	filter.set("hdr", false);
	filter.commit();

	filter.execute();

	// Check for errors
	const char* errorMessage;
	if (device.getError(errorMessage) != oidn::Error::None)
	{
		std::cerr << "OIDN Error: " << errorMessage << std::endl;
		return;
	}

	// Update the accumulation data with the denoised output.
	std::for_each(pixels.begin(), pixels.end(), [&](int i)
		{
			m_AccumulationData[i].r = outputData[i * 3 + 0];
			m_AccumulationData[i].g = outputData[i * 3 + 1];
			m_AccumulationData[i].b = outputData[i * 3 + 2];

			glm::vec4 clampedColor = glm::clamp(m_AccumulationData[i], glm::vec4(0.0f), glm::vec4(1.0f));
			m_ImageData[i] = Utils::ConvertToRGBA(clampedColor);
		});
}

void Renderer::denoise() {
	//return;
	if (m_Settings.OIDN) {
		OIDNDenoise();
		return;
	}

	uint32_t width = m_FinalImage->GetWidth();
	uint32_t height = m_FinalImage->GetHeight();

	BilateralFilter(width, height, m_AccumulationData);
	GaussianBlurFilter(width, height, m_AccumulationData);

	std::for_each(std::execution::seq, pixels.begin(), pixels.end(),
		[this](int i) {
			glm::vec4 clampedColor = glm::clamp(m_AccumulationData[i], glm::vec4(0.0f), glm::vec4(1.0f));
			m_ImageData[i] = Utils::ConvertToRGBA(clampedColor);
		});
}
