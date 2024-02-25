#ifndef ZERO_RENDER_SHADER_H_
#define ZERO_RENDER_SHADER_H_

#include <glad/glad.h>

namespace zero {

#ifdef __ANDROID__
#define NULL_SHADER_VERSION "#version 300 es"
#else
#define NULL_SHADER_VERSION "#version 150"
#endif

struct ShaderProgram {
  bool Initialize(const char* vertex_code, const char* fragment_code);
  void Use();

  void Cleanup();

  GLuint program = -1;
};

}  // namespace zero

#endif
