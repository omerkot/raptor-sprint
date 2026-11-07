// The few OpenGL calls gfx_full.cpp makes itself, for Windows, whose system headers stop at OpenGL
// 1.1. They're found at startup through the windowing library. (Not windows.h: its names clash with
// raylib's.)
#pragma once

typedef unsigned int GLuint, GLenum, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_WRAP_S = 0x2802,
                 GL_TEXTURE_WRAP_T = 0x2803, GL_LINEAR = 0x2601, GL_NEAREST = 0x2600, GL_CLAMP_TO_EDGE = 0x812F,
                 GL_TEXTURE_COMPARE_MODE = 0x884C, GL_TEXTURE_COMPARE_FUNC = 0x884D, GL_COMPARE_REF_TO_TEXTURE = 0x884E, GL_LEQUAL = 0x0203,
                 GL_DEPTH_COMPONENT = 0x1902, GL_DEPTH_COMPONENT16 = 0x81A5, GL_DEPTH_COMPONENT32F = 0x8CAC, GL_RGBA = 0x1908,
                 GL_RGBA16F = 0x881A, GL_FLOAT = 0x1406, GL_HALF_FLOAT = 0x140B, GL_UNSIGNED_SHORT = 0x1403, GL_NONE = 0,
                 GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9, GL_RENDERBUFFER = 0x8D41,
                 GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00, GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
                 GL_SCISSOR_TEST = 0x0C11, GL_POLYGON_OFFSET_FILL = 0x8037, GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100;

#define RR_GL_CALLS(X)                                                                                               \
    X(void, glGenTextures, (GLsizei, GLuint*))                                                                       \
    X(void, glDeleteTextures, (GLsizei, const GLuint*))                                                              \
    X(void, glBindTexture, (GLenum, GLuint))                                                                         \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))              \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                                \
    X(void, glGenFramebuffers, (GLsizei, GLuint*))                                                                   \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*))                                                          \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                                     \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                         \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                             \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                                                    \
    X(void, glGenRenderbuffers, (GLsizei, GLuint*))                                                                  \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint*))                                                         \
    X(void, glBindRenderbuffer, (GLenum, GLuint))                                                                    \
    X(void, glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                           \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))         \
    X(void, glDrawBuffer, (GLenum))                                                                                  \
    X(void, glReadBuffer, (GLenum))                                                                                  \
    X(void, glEnable, (GLenum))                                                                                      \
    X(void, glDisable, (GLenum))                                                                                     \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                                             \
    X(void, glClear, (GLbitfield))                                                                                   \
    X(void, glPolygonOffset, (GLfloat, GLfloat))

#define RR_GL_DECLARE(ret, name, args) static ret(*name) args = nullptr;
RR_GL_CALLS(RR_GL_DECLARE)
#undef RR_GL_DECLARE

extern "C" void (*glfwGetProcAddress(const char* name))(void);

inline void gl_load() {
#define RR_GL_LOAD(ret, name, args) name = (ret(*) args)glfwGetProcAddress(#name);
    RR_GL_CALLS(RR_GL_LOAD)
#undef RR_GL_LOAD
}
