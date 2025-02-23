#include <RayTracer/Model.h>

// #define STB_IMAGE_IMPLEMENTATION
// #define CGLTF_IMPLEMENTATION

#include <glad/glad.h>
#include <stb_image.h>
#include <cgltf.h>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <queue>
#include <set>

namespace fs = std::filesystem;

// Helper function to find the actual texture path given a CGLTF image.
static std::string FindTexturePath(const fs::path &basePath,
				   const cgltf_image *image)
{
	std::string texturePath;
	if (!image->uri) {
		auto newPath = basePath / image->name;
		if (!newPath.has_extension()) {
			if (std::strcmp(image->mime_type, "image/png") == 0) {
				newPath.replace_extension("png");
			} else if (std::strcmp(image->mime_type, "image/jpg") ==
				   0) {
				newPath.replace_extension("jpg");
			}
		}
		texturePath = newPath.generic_string();
	} else {
		texturePath = (basePath / image->uri).generic_string();
	}
	return texturePath;
}

Model::Model(std::string_view file)
{
	cgltf_options options = {};
	cgltf_data *model = nullptr;
	// Read GLTF, no additional options are required
	cgltf_parse_file(&options, file.data(), &model);
	// Load all GLTF buffers
	cgltf_load_buffers(&options, model, file.data());

	// Get the base path (useful when loading textures)
	fs::path path(file.data());
	const auto basePath = path.parent_path();
	// This is our texture cache, to make sure we don't load the same texture twice
	std::unordered_map<std::string, size_t> textureIds;
	// Reserves space for our texture vector
	_textures.reserve(model->materials_count);
	// Since we'll be batching our draws based on the textures it has, we need to calculate
	// how many batches this model needs, this is done by dividing by 16, which is the "batch size"
	// and rounding up, because we always need at least one batch.
	const uint32_t maxBatches = model->materials_count / 16 + 1;
	for (uint32_t i = 0; i < model->materials_count;
	     ++i) // For each material
	{
		const auto &material = model->materials[i];
		// Get the material's base color texture
		const auto *image = material.pbr_metallic_roughness
					    .base_color_texture.texture->image;
		// Find its texture path
		const auto texturePath = FindTexturePath(basePath, image);
		if (textureIds.contains(texturePath)) {
			// If we already loaded the texture, go onto the next material
			continue;
		}
		// Ask OpenGL to give us a new texture handle
		uint32_t texture;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);

		// Sets the texture's sampler's parameters
		// if you are not familiar with these, LearnOpenGL.com has a great tutorial
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER,
				    GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// Loads the texture data with STB_Image
		int32_t width = 0;
		int32_t height = 0;
		int32_t channels = STBI_rgb_alpha;
		const auto *textureData = stbi_load(texturePath.c_str(), &width,
						    &height, &channels,
						    STBI_rgb_alpha);
		// Calculate how many mip levels we need to generate for the texture.
		const auto levels = (uint32_t)std::floor(
			std::log2(std::max(width, height)));
		// Actually allocate the texture
		glTextureStorage2D(texture, levels, GL_RGBA8, width, height);
		// Copy our texture data to the GPU
		glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA,
				    GL_UNSIGNED_BYTE, textureData);
		// Generate mipmaps
		glGenerateTextureMipmap(texture);
		// Free texture memory on our end
		stbi_image_free((void *)textureData);
		// Add the new texture handle to the texture vector
		_textures.emplace_back(texture);
		// Register this texture index in our cache
		textureIds[texturePath] = _textures.size() - 1;
	}

	uint32_t transformIndex = 0;
	size_t vertexOffset = 0;
	size_t indexOffset = 0;
	std::vector<MeshCreateInfo> meshCreateInfos;
	meshCreateInfos.reserve(1024);
	// For each node in the scene
	for (uint32_t i = 0; i < model->scene->nodes_count; ++i) {
		std::queue<cgltf_node *> nodes;
		// Add the node to the queue (Exercise: can you use recursion instead of a queue?)
		nodes.push(model->scene->nodes[i]);
		while (!nodes.empty()) {
			// Get a node from the queue
			const auto *node = nodes.front();
			nodes.pop();
			// If there are no meshes
			if (!node->mesh) {
				// Then for each node (if any)
				for (uint32_t j = 0; j < node->children_count;
				     ++j) {
					// Add it to the queue again
					nodes.push(node->children[j]);
				}
				continue;
			}
			// For each primitive in the node
			for (uint32_t j = 0; j < node->mesh->primitives_count;
			     ++j) {
				const auto &primitive =
					node->mesh->primitives[j];
				const glm::vec3 *positionPtr = nullptr;
				const glm::vec3 *normalPtr = nullptr;
				const glm::vec2 *uvPtr = nullptr;
				const glm::vec4 *tangentPtr = nullptr;
				uint64_t vertexCount = 0;
				// Get its vertex data
				for (uint32_t k = 0;
				     k < primitive.attributes_count; ++k) {
					// Get the attribute information (position, normal, ...)
					const auto &attribute =
						primitive.attributes[k];
					const auto *accessor = attribute.data;
					// Get the buffer view associated with this attribute
					const auto *view =
						accessor->buffer_view;
					const auto *dataPtr =
						(const char *)view->buffer->data;
					// If this is confusing you can refer to the glTF main scheme by Khronos, it should clear up some things
					switch (attribute.type) {
					case cgltf_attribute_type_position:
						vertexCount = accessor->count;
						// Set the `positionPtr`
						positionPtr =
							(const glm::vec3
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						break;

					case cgltf_attribute_type_normal:
						// Set the `normalPtr`
						normalPtr =
							(const glm::vec3
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						break;

					case cgltf_attribute_type_texcoord:
						// Set the `uvPtr`
						uvPtr = (const glm::vec2
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						break;

					case cgltf_attribute_type_tangent:
						// Set the `tangentPtr`
						tangentPtr =
							(const glm::vec4
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						break;

					default:
						break;
					}
				}
				// Reserve space for the vertices in our own vertex format
				std::vector<Vertex> vertices;
				vertices.resize(vertexCount);
				{
					// Get the pointer to the base of the vector
					auto *ptr = vertices.data();
					// For each vertex
					for (uint32_t v = 0; v < vertexCount;
					     ++v, ++ptr) {
						// Copy the attribute (if available) to the current pointer (will increment every iteration)
						if (positionPtr) {
							std::memcpy(
								&ptr->position,
								positionPtr + v,
								sizeof(glm::vec3));
						}
						if (normalPtr) {
							std::memcpy(
								&ptr->normal,
								normalPtr + v,
								sizeof(glm::vec3));
						}
						if (uvPtr) {
							std::memcpy(
								&ptr->uv,
								uvPtr + v,
								sizeof(glm::vec2));
						}
						if (tangentPtr) {
							std::memcpy(
								&ptr->tangent,
								tangentPtr + v,
								sizeof(glm::vec4));
						}
					}
				}

				std::vector<uint32_t> indices;
				{
					// Get the indices information for the primitive
					const auto *accessor =
						primitive.indices;
					const auto *view =
						accessor->buffer_view;
					const char *dataPtr =
						(const char *)view->buffer->data;
					// Reserve space for our indices buffer
					indices.reserve(accessor->count);
					// Check the index type (uint8, uint16 or uin32)
					switch (accessor->component_type) {
					// Copy the whole index buffer to our vector
					case cgltf_component_type_r_8:
					case cgltf_component_type_r_8u: {
						const auto *ptr =
							(const uint8_t
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						std::copy(ptr,
							  ptr + accessor->count,
							  std::back_inserter(
								  indices));
					} break;

					case cgltf_component_type_r_16:
					case cgltf_component_type_r_16u: {
						const auto *ptr =
							(const uint16_t
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						std::copy(ptr,
							  ptr + accessor->count,
							  std::back_inserter(
								  indices));
					} break;

					case cgltf_component_type_r_32f:
					case cgltf_component_type_r_32u: {
						const auto *ptr =
							(const uint32_t
								 *)(dataPtr +
								    view->offset +
								    accessor->offset);
						std::copy(ptr,
							  ptr + accessor->count,
							  std::back_inserter(
								  indices));
					} break;

					default:
						break;
					}
				}
				// Get the primitive's material base color texture path
				const auto baseColorURI = FindTexturePath(
					basePath,
					primitive.material
						->pbr_metallic_roughness
						.base_color_texture.texture
						->image);
				const auto indexCount = indices.size();
				// Emplace a `MeshCreateInfo` (we will use this later)
				meshCreateInfos.emplace_back(MeshCreateInfo{
					std::move(vertices),
					std::move(indices),
					transformIndex++,
					// Exercise: this doesn't handle missing textures, it's possible that a mesh may not have any color
					// texture, can you change this behavior and display a default texture of your choice when this happens?
					(uint32_t)textureIds[baseColorURI],
					// Exercise: We don't load normal textures, can you load the normal textures (when available)
					// and apply some basic normal mapping?
					0,
					vertexOffset,
					indexOffset,
				});
				// Apply the node transformation and emplace it to the vector
				cgltf_node_transform_world(
					node,
					glm::value_ptr(
						_transforms.emplace_back()));
				// Increment the vertex and index byte offset
				vertexOffset += vertexCount * sizeof(Vertex);
				indexOffset += indexCount * sizeof(uint32_t);
			}
			// Push children nodes
			for (uint32_t j = 0; j < node->children_count; ++j) {
				nodes.push(node->children[j]);
			}
		}
	}
	// Resize the indirect commands vector and the object data vector.
	_cmds.resize(maxBatches);
	_objectData.resize(maxBatches);
	// Allocate GL buffers
	// This is the Vertex Array Object, it specifies how the vertex data should be read by the GPU
	glCreateVertexArrays(1, &_vao);
	// This is the Vertex Buffer Object, it holds on all the mesh vertex data in this model
	glCreateBuffers(1, &_vbo);
	// This is the Element Buffer Object (I call it the Index Buffer Object, same thing)
	// it holds all the indices for all the meshes
	glCreateBuffers(1, &_ibo);
	// This is the transform data buffer, it holds the local transform for each mesh
	glCreateBuffers(1, &_transformData);
	// Create the object data buffers, they are useful when drawing because they associate all the
	// necessary indices (transform index, base color index, ...) to a mesh, allowing the shader
	// to fetch the information from the other buffers, keep in mind this is per batch.
	glCreateBuffers(_objectData.size(), _objectData.data());
	// Finally this is the indirect data buffer, it holds all the information required for OpenGL to draw the mesh,
	// this is also per batch.
	glGenBuffers(_cmds.size(), _cmds.data());

	// First, we need to figure out how big our vertex and index buffer should be
	size_t vertexSize = 0;
	size_t indexSize = 0;
	for (const auto &info : meshCreateInfos) {
		vertexSize += info.vertices.size() * sizeof(Vertex);
		indexSize += info.indices.size() * sizeof(uint32_t);
	}

	// Allocate the storage
	glNamedBufferStorage(_vbo, vertexSize, nullptr, GL_DYNAMIC_STORAGE_BIT);
	glNamedBufferStorage(_ibo, indexSize, nullptr, GL_DYNAMIC_STORAGE_BIT);

	// Associate the vertex array object, with our vertex and index buffer
	glVertexArrayVertexBuffer(_vao, 0, _vbo, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(_vao, _ibo);

	// Tell OpenGL which vertex location index we want to use
	// it maps to the "layout (location = N) in vecN position/normal/..." in our vertex shader
	glEnableVertexArrayAttrib(_vao, 0);
	glEnableVertexArrayAttrib(_vao, 1);
	glEnableVertexArrayAttrib(_vao, 2);
	glEnableVertexArrayAttrib(_vao, 3);

	// Tell OpenGL how to interpret each vertex attribute
	//                              location          components  type      transpose  offset
	glVertexArrayAttribFormat(_vao, /*location = */ 0, /*vec*/ 3, GL_FLOAT,
				  GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribFormat(_vao, /*location = */ 1, /*vec*/ 3, GL_FLOAT,
				  GL_FALSE, offsetof(Vertex, normal));
	glVertexArrayAttribFormat(_vao, /*location = */ 2, /*vec*/ 2, GL_FLOAT,
				  GL_FALSE, offsetof(Vertex, uv));
	glVertexArrayAttribFormat(_vao, /*location = */ 3, /*vec*/ 4, GL_FLOAT,
				  GL_FALSE, offsetof(Vertex, tangent));

	// Finally, bind each vertex attribute to a vertex buffer,
	// the 0th buffer, which is the only one we bound
	glVertexArrayAttribBinding(_vao, 0, 0);
	glVertexArrayAttribBinding(_vao, 1, 0);
	glVertexArrayAttribBinding(_vao, 2, 0);
	glVertexArrayAttribBinding(_vao, 3, 0);

	// For each mesh
	for (auto &info : meshCreateInfos) {
		// Upload at a given vertex and index offset the data
		glNamedBufferSubData(_vbo, info.vertexOffset,
				     info.vertices.size() * sizeof(Vertex),
				     info.vertices.data());
		glNamedBufferSubData(_ibo, info.indexOffset,
				     info.indices.size() * sizeof(uint32_t),
				     info.indices.data());
		_meshes.emplace_back(info);
	}
}

Model::~Model() = default;

void Model::Draw(const Shader &shader) const
{
	// Define an object data structure (this should match with the one in the shader)
	struct ObjectData {
		uint32_t transformIndex;
		uint32_t baseColorIndex;
		uint32_t normalIndex;
	};
	struct BatchData {
		std::vector<ObjectData> objects;
		std::vector<MeshIndirectInfo> commands;
	};
	std::vector<BatchData> objectBatches(_cmds.size());
	std::vector<std::set<uint32_t> > textureHandles(_cmds.size());
	// For each mesh
	for (const auto &mesh : _meshes) {
		// Calculate the batch index this mesh belongs to (just divide by the "batch size")
		const auto index = mesh.BaseColorTexture() / 16;
		// Get the mesh indirect info structure
		objectBatches[index].commands.emplace_back(mesh.Info());
		// Get the mesh general information
		objectBatches[index].objects.emplace_back(ObjectData{
			// Restrict the texture range to [0, 15], because by batching texture
			// indices must not be "global", but local to the batch group
			mesh.TransformIndex(), mesh.BaseColorTexture() % 16,
			// Exercise: Can you do the same for normal textures?
			mesh.NormalTexture() });
		// Insert the texture index for this batch in a set, this is useful
		// when binding the textures because we will need unique handles
		textureHandles[index].insert(
			_textures[mesh.BaseColorTexture()]);
	}

	// Copy the transform data we just created to the GPU
	glNamedBufferData(_transformData,
			  _transforms.size() * sizeof(glm::mat4),
			  _transforms.data(), GL_DYNAMIC_DRAW);
	// Bind the buffer to the storage buffer, location = 1
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, _transformData);

	// Bind the shader because we will be setting uniforms now
	shader.Bind();
	// For each batch
	for (uint32_t index = 0; const auto &batch : objectBatches) {
		// Write the object data to the current object uniform buffer
		glNamedBufferData(_objectData[index],
				  batch.objects.size() * sizeof(ObjectData),
				  batch.objects.data(), GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,
				 _objectData[index]);

		// Write the indirect commands to the current indirect buffer
		glBindBuffer(GL_DRAW_INDIRECT_BUFFER, _cmds[index]);
		glNamedBufferData(_cmds[index],
				  batch.commands.size() *
					  sizeof(MeshIndirectInfo),
				  batch.commands.data(), GL_DYNAMIC_DRAW);

		// Set all the active textures for this batch
		for (uint32_t offset = 0;
		     const auto texture : textureHandles[index]) {
			shader.Set(2 + offset, offset);
			glActiveTexture(GL_TEXTURE0 + offset);
			glBindTexture(GL_TEXTURE_2D, texture);
			offset++;
		}

		// Finally, bind the VAO and issue the draw call
		glBindVertexArray(_vao);
		glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
					    nullptr, batch.commands.size(),
					    sizeof(MeshIndirectInfo));
		// Increment the index to go to the next batch
		index++;
	}
}