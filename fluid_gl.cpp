#include "shader.h"

int w = 1920;
int h = 1080;

GLuint textures[2];
GLuint fbos[2];

Shader *density_shader;
Shader *advect_shader;
Shader *project_shader;
Shader *subtract_shader;
Shader *vorticity_shader;

float mouse_x;
float mouse_y;

struct Vertex {
    float x, y, z, w;
};

void bind_texture(Shader *shader, GLuint texture){
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    shader->use();
    glUniform1i(shader->uniforms[1], 0);
}

void set_attributes(Shader *shader){
    if (shader->attributes[0] != (GLuint)-1){
        glEnableVertexAttribArray(shader->attributes[0]);
        glVertexAttribPointer(shader->attributes[0], 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    }
}

void prepare_fbo(Shader *shader, int width, int height, GLuint fbo){
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    float m[16] = {
        2.0f/width, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f/height, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
    };

    shader->use();
    glUniformMatrix4fv(shader->uniforms[0], 1, GL_FALSE, m);
    glUniform2f(shader->uniforms[2], float(w), float(h));
    if (shader->uniforms[3] != (GLuint)-1){
        float time = glutGet(GLUT_ELAPSED_TIME)*0.001;
        glUniform1f(shader->uniforms[3], time);
    }
}

void reshape(int new_width, int new_height){
    w = new_width;
    h = new_height;

    CHECK_GL

    glDeleteFramebuffers(2, fbos);
    glGenFramebuffers(2, fbos);

    glDeleteTextures(2, textures);
    glGenTextures(2, textures);


    CHECK_GL

    for (int i = 0; i < 2; i++){
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    }

    CHECK_GL

    for (int i = 0; i < 2; i++){
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[i], 0);
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    CHECK_GL
}

void screenshot(const char *path){
    std::vector<uint32_t> rgba(w*h);
    std::vector<uint8_t> rgb(w*h*3);

    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    int i = 0;
    for (int y = h - 1; y >= 0; y--) for (int x = 0; x < w; x++){
        uint32_t c = rgba[x + y*w];
        rgb[i++] = (c >> 0*8) & 255;
        rgb[i++] = (c >> 1*8) & 255;
        rgb[i++] = (c >> 2*8) & 255;
    }

    FILE *fp = fopen(path, "wb");
    assert(fp);
    fprintf(fp, "P6\n%i %i\n255\n", w, h);
    fwrite(rgb.data(), 1, rgb.size(), fp);
    fclose(fp);
}

typedef std::vector<Vertex> Vertices;

void make_rect(
    Vertices &vertices,
    float x0, float y0,
    float x1, float y1,
    float u0 = 0.0f, float v0 = 0.0f,
    float u1 = 1.0f, float v1 = 1.0f
){
    Vertex v[4] = {
        Vertex{x0, y0, u0, v0},
        Vertex{x1, y0, u1, v0},
        Vertex{x1, y1, u1, v1},
        Vertex{x0, y1, u0, v1},
    };
    vertices.push_back(v[0]);
    vertices.push_back(v[1]);
    vertices.push_back(v[2]);

    vertices.push_back(v[0]);
    vertices.push_back(v[2]);
    vertices.push_back(v[3]);
}

int src = 0;
int dst = 1;

void fill_screen(Shader *shader){
    Vertices vertices;
    make_rect(vertices, 0.0f, 0.0f, w, h);
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    set_attributes(shader);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex)*vertices.size(), vertices.data(), GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, vertices.size());
    glDeleteBuffers(1, &vbo);
}

void draw(Shader *shader){
    bind_texture(shader, textures[src]);
    prepare_fbo(shader, w, h, fbos[dst]);
    fill_screen(shader);
    std::swap(src, dst);
}

void on_frame(){
    CHECK_GL

    GLuint timeElapsedQuery;
    glGenQueries(1, &timeElapsedQuery);
    glBeginQuery(GL_TIME_ELAPSED, timeElapsedQuery);

    int iterations = 20;

    draw(advect_shader);
    for (int i = 0; i < iterations; i++){
        draw(project_shader);
    }
    draw(subtract_shader);
    draw(vorticity_shader);

    // dump src to screen
    bind_texture(density_shader, textures[src]);
    prepare_fbo(density_shader, w, h, 0);
    fill_screen(density_shader);

    glEndQuery(GL_TIME_ELAPSED);

#if 0
    // Warning: produces almost 6 GB of images
    static int frame = 0;
    char path[256];
    snprintf(path, sizeof(path), "frames_gl/frame_%d.ppm", frame);
    screenshot(path);
    puts(path);
    frame++;
    if (frame >= 1024){
        exit(0);
    }
#endif
    glFlush();
    glFinish();

    glutSwapBuffers();
    CHECK_GL

    GLint available = 0;
    while (!available){
        glGetQueryObjectiv(timeElapsedQuery, GL_QUERY_RESULT_AVAILABLE, &available);
    }

    uint64_t t;
    glGetQueryObjectui64v(timeElapsedQuery, GL_QUERY_RESULT, &t);
    printf("%f\n", t*1e-6);
    glDeleteQueries(1, &timeElapsedQuery);
}

void work(int frame){
    glutPostRedisplay();
    glutTimerFunc(20, work, frame + 1);
}

void on_move(int x, int y){
    y = h - 1 - y;
    mouse_x = x;
    mouse_y = y;
}

void on_mouse_button(int button, int action, int x, int y){
    on_move(x, y);

    int down = action == GLUT_DOWN;

    if (button == GLUT_LEFT_BUTTON){
        if (down){
        }
    }
}

int main(int argc, char **argv){
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(w, h);
    glutCreateWindow("");

    glewInit();

#define STR(x) #x

    const char *vert_src = STR(
        attribute vec4 a_data0;

        varying vec4 v_data0;

        uniform mat4 u_0;

        void main(){
            v_data0 = a_data0;

            gl_Position = u_0*vec4(a_data0.xy, 0.0, 1.0);
        }
    );

    const char *density_frag_src = STR(
        varying vec4 v_data0;

        uniform sampler2D u_1;
        uniform vec2 u_2;

        void main(){
            vec2 pos = gl_FragCoord.xy;

            vec4 src = texture2D(u_1, pos/u_2);

            float f = src.z;
            //f = log(f*0.2 + 1.0);
            f *= 0.05;
            float f3 = f*f*f;
            vec3 rgb = vec3(1.5*f, 1.5*f3, 1.5*f3*f3);

            rgb.rg = rgb.gr;

            gl_FragColor = vec4(rgb, 1.0);
        }
    );

    const char *advect_frag_src = STR(
        varying vec4 v_data0;

        uniform sampler2D u_1;
        uniform vec2 u_2;
        uniform float u_3;

        vec4 noise(vec4 v){
            // ensure reasonable range
            v = fract(v) + fract(v*1e4) + fract(v*1e-4);
            // seed
            v += vec4(0.12345, 0.6789, 0.314159, 0.271828);
            // more iterations => more random
            v = fract(v*dot(v, v)*123.456);
            v = fract(v*dot(v, v)*123.456);
            return v;
        }

        void main(){
            float time = u_3;
            vec2 s = 1.0/u_2;
            vec2 pos = gl_FragCoord.xy;
            vec4 src = texture2D(u_1, pos*s);

            float dt = 0.01f;

            vec2 old_pos = pos - dt*src.xy;

            vec4 dst = texture2D(u_1, old_pos*s);

            // add random velocity
            if (0.333 < pos.x*s.x && pos.x*s.x < 0.666){
                vec2 random_velocity = noise(vec4(pos*s, fract(time*13.37), 0.0)).xy*2.0 - 1.0;
                dst.xy += random_velocity*20.0;
            }

            float dx = 64.0;
            pos.x = fract(pos.x*(1.0/dx))*dx;

            vec2 d = pos - dx*0.5;
            dst.z += smoothstep(25.0, 0.0, length(d));

            // dense regions rise up
            dst.y += dst.z*1.0;
            // dampen velocity
            dst.xy *= 0.995;
            // dissipation
            dst.z *= 0.995;
            // clear pressure for next shader
            dst.w = 0.0;

            // clear bottom
            if (pos.y < 10.0) dst.xyz = 0.0;

            gl_FragColor = dst;
        }
    );

    const char *project_frag_src = STR(
        varying vec4 v_data0;

        uniform sampler2D u_1;
        uniform vec2 u_2;

        void main(){
            vec2 s = 1.0/u_2;
            vec2 pos = gl_FragCoord.xy*s;

            vec4 dst    = texture2D(u_1, pos);
            vec4 left   = texture2D(u_1, vec2(pos.x - s.x, pos.y));
            vec4 right  = texture2D(u_1, vec2(pos.x + s.x, pos.y));
            vec4 bottom = texture2D(u_1, vec2(pos.x, pos.y - s.y));
            vec4 top    = texture2D(u_1, vec2(pos.x, pos.y + s.y));

            float divergence = (right.x - left.x) + (top.y - bottom.y);
            float sum = left.w + right.w + bottom.w + top.w - divergence;

            dst.w = 0.25*sum;

            gl_FragColor = dst;
        }
    );

    const char *subtract_frag_src = STR(
        varying vec4 v_data0;

        uniform sampler2D u_1;
        uniform vec2 u_2;

        void main(){
            vec2 s = 1.0/u_2;
            vec2 pos = gl_FragCoord.xy*s;

            vec4 dst    = texture2D(u_1, pos);
            vec4 left   = texture2D(u_1, vec2(pos.x - s.x, pos.y));
            vec4 right  = texture2D(u_1, vec2(pos.x + s.x, pos.y));
            vec4 bottom = texture2D(u_1, vec2(pos.x, pos.y - s.y));
            vec4 top    = texture2D(u_1, vec2(pos.x, pos.y + s.y));

            dst.x -= 0.5*(right.w - left.w);
            dst.y -= 0.5*(top.w - bottom.w);

            gl_FragColor = dst;
        }
    );

    const char *vorticity_frag_src = STR(
        varying vec4 v_data0;

        uniform sampler2D u_1;
        uniform vec2 u_2;

        float curl(float x, float y){
            vec2 s = 1.0/u_2;

            vec4 left   = texture2D(u_1, vec2(x - s.x, y));
            vec4 right  = texture2D(u_1, vec2(x + s.x, y));
            vec4 bottom = texture2D(u_1, vec2(x, y - s.y));
            vec4 top    = texture2D(u_1, vec2(x, y + s.y));

            return top.x - bottom.x + left.y - right.y;
        }

        void main(){
            vec2 s = 1.0/u_2;
            vec2 pos = gl_FragCoord.xy*s;

            vec2 direction;
            direction.x = abs(curl(pos.x, pos.y - s.y)) - abs(curl(pos.x, pos.y + s.y));
            direction.y = abs(curl(pos.x + s.x, pos.y)) - abs(curl(pos.x - s.x, pos.y));

            direction = 10.0/(length(direction) + 1e-5) * direction;

            vec4 dst = texture2D(u_1, pos);

            float dt = 0.01;

            if (pos.x > 0.66){
                dst.xy += dt*curl(pos.x, pos.y)*direction;
            }

            gl_FragColor = dst;
        }
    );

    density_shader   = new Shader(vert_src, density_frag_src);
    advect_shader    = new Shader(vert_src, advect_frag_src);
    project_shader   = new Shader(vert_src, project_frag_src);
    subtract_shader  = new Shader(vert_src, subtract_frag_src);
    vorticity_shader = new Shader(vert_src, vorticity_frag_src);

    glutMouseFunc(on_mouse_button);
    glutMotionFunc(on_move);
    glutPassiveMotionFunc(on_move);
    reshape(w, h);

    glutDisplayFunc(on_frame);
    work(0);
    glutMainLoop();
    return 0;
}
