#include <RayTracer/Shader.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <string>

// Helper function to read the whole file
static std::string Slurp(std::string_view path)
{
	std::ifstream file(path.data(), std::ios::ate);
	std::string result(file.tellg(), '\0');
	file.seekg(0);
	file.read((char *)result.data(), result.size());
	return result;
}

Shader::Shader(std::string_view vertex, std::string_view fragment)
{
	// Used to check the compilation status of the shader.
	int success = false;
	// Holds the compiler error messages, in case of an error.
	char log[1024] = {};

	// Reads the vertex shader
	const auto vertexShaderSource = Slurp(vertex);
	const char *vertexShaderSourcePtr = vertexShaderSource.c_str();
	// Calls OpenGL to make a new vertex shader handle
	const auto vertexShader = glCreateShader(GL_VERTEX_SHADER);
	// Associate the shader source to the vertex shader handle
	glShaderSource(vertexShader, 1, &vertexShaderSourcePtr, nullptr);
	// Compile the shader
	glCompileShader(vertexShader);
	// Get compilation status
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
	// If there was an error, print it
	if (!success) {
		glGetShaderInfoLog(vertexShader, 1024, NULL, log);
		std::printf("%s\n", log);
	}

	// Reads the fragment shader
	const auto fragmentShaderSource = Slurp(fragment);
	const char *fragmentShaderSourcePtr = fragmentShaderSource.c_str();
	// Calls OpenGL to make a new fragment shader handle
	const auto fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	// Associate the shader source to the vertex shader handle
	glShaderSource(fragmentShader, 1, &fragmentShaderSourcePtr, nullptr);
	// Compile the shader
	glCompileShader(fragmentShader);
	// Get compilation status
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(fragmentShader, 1024, NULL, log);
		std::printf("%s\b", log);
	}

	// Create the shader program, also called "pipeline" in other APIs
	_program = glCreateProgram();
	// Attach both the vertex and the fragment shader to the program
	glAttachShader(_program, vertexShader);
	glAttachShader(_program, fragmentShader);
	glLinkProgram(_program);
	// Get link status
	glGetProgramiv(_program, GL_LINK_STATUS, &success);
	// If linking failed, display the error message
	if (!success) {
		glGetProgramInfoLog(_program, 1024, NULL, log);
		std::printf("%s\b", log);
	}

	// Delete the shader handles, since we have our program they are unnecessary
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
}

Shader::~Shader() = default;

void Shader::Bind() const
{
	glUseProgram(_program);
}

void Shader::Set(uint32_t location, const glm::mat4 &matrix) const
{
	glUniformMatrix4fv(location, 1, false, glm::value_ptr(matrix));
}

void Shader::Set(uint32_t location, int32_t value) const
{
	glUniform1i(location, value);
}