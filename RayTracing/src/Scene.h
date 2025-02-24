#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

struct Material
{
	glm::vec3 Albedo{ 1.0f };
	float Roughness = 1.0f;
	float Metallic = 0.0f;
	glm::vec3 EmissionColor{ 0.0f };
	float EmissionPower = 0.0f;

	glm::vec3 GetEmission() const { return EmissionColor * EmissionPower; }
};

struct Sphere
{
	glm::vec3 Position{ 0.0f };
	float Radius = 0.5f;
	int MaterialIndex = 0;
};

struct Plane
{
	glm::vec3 Position{ 0.0f };
	glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
	int MaterialIndex = 0;
};

struct Cuboid
{
	glm::vec3 Position{ 0.0f };
	glm::vec3 Dimensions{ 1.0f, 1.0f, 1.0f };
	glm::quat Rotation{ 1, 0, 0, 0 };
	int MaterialIndex = 0;
};;

struct Scene
{
	std::vector<Sphere> Spheres;
	std::vector<Plane> Planes;
	std::vector<Cuboid> Cuboids;
	std::vector<Material> Materials;
};
