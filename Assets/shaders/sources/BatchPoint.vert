#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 position;
layout (location = 1) in vec4 color;
layout (location = 2) in vec2 size;
layout (location = 3) in vec2 uv;

layout(set = 0,binding = 0) uniform UniformBufferObject
{
	mat4 projView;
} ubo;

layout (location = 0) out Data
{
	vec3 position;
	vec4 color;
	vec2 size;
	vec2 uv;
} vs_out;

void main()
{
	gl_Position = ubo.projView * vec4(position,1.0);
	vs_out.position = position;
	vs_out.color = color;
	vs_out.size = size;
	vs_out.uv = uv;
}