#version 330

in vec3 in_coords;
in vec3 in_normals;

out vec3 vertex_normal;

uniform mat4 mvp;     // Modelview-projection matrix

void main(void) {
  vertex_normal = in_normals;
  gl_Position = mvp * vec4(in_coords, 1.0);
}
