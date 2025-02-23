#pragma once

#include <RayTracer/Shader.h>
#include <RayTracer/Mesh.h>

#include <string_view>
#include <vector>

class Model {
    public:
	Model(std::string_view path);
	~Model();

	void Draw(const Shader &shader) const;

    private:
	// Holds all the meshes that compose the model
	std::vector<Mesh> _meshes;
	// Holds OpenGL texture handles
	std::vector<uint32_t> _textures;
	// Holds all the local transforms for each mesh
	std::vector<glm::mat4> _transforms;
	// OpenGL buffers
	uint32_t _vao;
	uint32_t _vbo;
	uint32_t _ibo;
	// These are vectors because we'll be batching our draws
	std::vector<uint32_t> _cmds;
	std::vector<uint32_t> _objectData;
	uint32_t _transformData;
};