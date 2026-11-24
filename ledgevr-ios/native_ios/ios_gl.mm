// LEDGE iOS — GL helpers shared by renderer.cpp (declared in touch_backend.h
// and ios_backend.h; Android defines them in touch_backend.cpp/xr_backend.cpp).
#include "../shim/GLES3/gl32.h"
#include <cstdio>

namespace lv {

GLuint lvCompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; GLsizei n = 0;
        glGetShaderInfoLog(sh, 512, &n, log);
        fprintf(stderr, "[LEDGE] shader compile failed: %.*s\n", (int)n, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint lvLinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; GLsizei n = 0;
        glGetProgramInfoLog(p, 512, &n, log);
        fprintf(stderr, "[LEDGE] program link failed: %.*s\n", (int)n, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

GLuint lvMakeFbo(GLuint tex, int w, int h) {
    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glBindTexture(GL_TEXTURE_2D, tex);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[LEDGE] ui fbo incomplete %dx%d\n", w, h);
        glDeleteFramebuffers(1, &fb);
        return 0;
    }
    return fb;
}

} // namespace lv
