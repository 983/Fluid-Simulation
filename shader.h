#include <GL/glew.h>
#include <GL/glut.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <vector>

bool check_shader_compile_status(GLuint obj) {
    GLint status;
    glGetShaderiv(obj, GL_COMPILE_STATUS, &status);
    if(status == GL_FALSE) {
        GLint length;
        glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length);
        glGetShaderInfoLog(obj, length, &length, &log[0]);
        puts(log.data());
        return false;
    }
    return true;
}

bool check_program_link_status(GLuint obj) {
    GLint status;
    glGetProgramiv(obj, GL_LINK_STATUS, &status);
    if(status == GL_FALSE) {
        GLint length;
        glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length);
        glGetProgramInfoLog(obj, length, &length, &log[0]);
        puts(log.data());
        return false;
    }
    return true;
}

GLuint make_shader(const char *source, GLenum shader_type){
    GLuint shader = glCreateShader(shader_type);
    int length = strlen(source);
    glShaderSource(shader, 1, &source, &length);
    glCompileShader(shader);
    check_shader_compile_status(shader);
    return shader;
}

GLuint make_shader_program(const char *vert_src, const char *frag_src){
    GLuint program = glCreateProgram();

    GLuint vert_shader = make_shader(vert_src, GL_VERTEX_SHADER);
    GLuint frag_shader = make_shader(frag_src, GL_FRAGMENT_SHADER);

    glAttachShader(program, vert_shader);
    glAttachShader(program, frag_shader);

    glLinkProgram(program);

    check_program_link_status(program);

    return program;
}

#define MAX_ATTRIBUTES 5
#define MAX_UNIFORMS   5

struct Shader {
    GLuint program;
    GLuint attributes[MAX_ATTRIBUTES];
    GLuint uniforms[MAX_UNIFORMS];

    Shader(const char *vert_src, const char *frag_src){
        program = make_shader_program(vert_src, frag_src);

        use();

        for (int i = 0; i < MAX_UNIFORMS; i++){
            char name[4] = "u_0";
            name[2] = '0' + i;
            uniforms[i] = glGetUniformLocation(program, name);
            //printf("uniform location %i: %i\n", i, uniforms[i]);
        }
        for (int i = 0; i < MAX_ATTRIBUTES; i++){
            char name[8] = "a_data0";
            name[6] = '0' + i;
            attributes[i] = glGetAttribLocation(program, name);
            //printf("attribute location %i: %i\n", i, attributes[i]);
        }
    }

    void use(){
        glUseProgram(program);
    }
};

void check_gl(int line){
    int error = glGetError();
    if (error != GL_NO_ERROR){
        printf("OpenGL ERROR line %i: %s\n", line, gluErrorString(error));
    }
}

#define CHECK_GL check_gl(__LINE__);
