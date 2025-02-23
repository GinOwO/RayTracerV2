#pragma once

#include <string_view>
#include <glm/mat4x4.hpp>
#include <cstdint>

class Shader {
    public:
	Shader(std::string_view vertex, std::string_view fragment);
	~Shader();

	void Bind() const;
	void Set(uint32_t location, const glm::mat4 &matrix) const;
	void Set(uint32_t location, int32_t value) const;

    private:
	uint32_t _program;
};