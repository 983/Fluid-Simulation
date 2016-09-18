#include <GL/glut.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include "vec2.h"

#ifdef _WIN32
#include <windows.h>

double sec(){
    LARGE_INTEGER frequency, t;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&frequency);
    return t.QuadPart / (double)frequency.QuadPart;
}
#else
#include <time.h>

double sec(){
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + 1e-9*t.tv_nsec;
}
#endif

int w = 512;
int h = 512;

const int nx = 256;
const int ny = 256;

float dt = 0.02f;
int iterations = 5;
float vorticity = 10.0f;

vec2f mouse;

void draw(const vec2f *data, int n, GLenum mode){
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, data);
    glDrawArrays(mode, 0, n);
}

void draw_circle(float x, float y, float r, int n = 100){
    vec2f pos[n];
    for (int i = 0; i < n; i++){
        float angle = 2.0f*3.14159f*i/n;
        pos[i] = vec2f{x, y} + r*polar(angle);
    }
    draw(pos, n, GL_LINE_LOOP);
}

template <typename T>
struct Grid {
    T *values;
    int nx, ny;

    Grid(int nx, int ny): nx(nx), ny(ny){
        values = new T[nx*ny];
    }

    Grid(const Grid&) = delete;
    Grid& operator = (const Grid&) = delete;

    ~Grid(){
        delete[] values;
    }

    void swap(Grid &other){
        std::swap(values, other.values);
        std::swap(nx, other.nx);
        std::swap(ny, other.ny);
    }

    const T* data() const {
        return values;
    }

    int idx(int x, int y) const {
        //x = clamp(x, 0, nx - 1);
        //y = clamp(y, 0, ny - 1);

        // wrap around
        x = (x + nx) % nx;
        y = (y + ny) % ny;

        return x + y*nx;
    }

    T& operator () (int x, int y){
        return values[idx(x, y)];
    }

    const T& operator () (int x, int y) const {
        return values[idx(x, y)];
    }
};

Grid<vec2f> old_velocity(nx, ny);
Grid<vec2f> new_velocity(nx, ny);

Grid<float> old_density(nx, ny);
Grid<float> new_density(nx, ny);

Grid<uint32_t> pixels(nx, ny);

GLuint texture;

#define FOR_EACH_CELL for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++)

void check_gl(int line){
    int error = glGetError();
    if (error != GL_NO_ERROR){
        printf("OpenGL ERROR line %i: %s\n", line, gluErrorString(error));
    }
}

#define CHECK_GL check_gl(__LINE__);

void init(){
    FOR_EACH_CELL {
        old_density(x, y) = 0.0f;
        old_velocity(x, y) = vec2f{0.0f, 0.0f};
    }

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nx, ny, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

template <typename T>
T interpolate(const Grid<T> &grid, vec2f p){
    int ix = floorf(p.x);
    int iy = floorf(p.y);
    float ux = p.x - ix;
    float uy = p.y - iy;
    return lerp(
        lerp(grid(ix + 0, iy + 0), grid(ix + 1, iy + 0), ux),
        lerp(grid(ix + 0, iy + 1), grid(ix + 1, iy + 1), ux),
        uy
    );
}

void advect_density(){
    FOR_EACH_CELL {
        vec2f pos = v2f(x, y) - dt*old_velocity(x, y);
        new_density(x, y) =  interpolate(old_density, pos);
    }
    old_density.swap(new_density);
}

void advect_velocity(){
    FOR_EACH_CELL {
        vec2f pos = v2f(x, y) - dt*old_velocity(x, y);
        new_velocity(x, y) =  interpolate(old_velocity, pos);
    }
    old_velocity.swap(new_velocity);
}

void diffuse_density(){
    float diffusion = dt*100.01f;
    FOR_EACH_CELL {
        float sum =
            diffusion*(
            + old_density(x - 1, y + 0)
            + old_density(x + 1, y + 0)
            + old_density(x + 0, y - 1)
            + old_density(x + 0, y + 1)
            )
            + old_density(x + 0, y + 0);
        new_density(x, y) = 1.0f/(1.0f + 4.0f*diffusion) * sum;
    }
    old_density.swap(new_density);
}

void diffuse_velocity(){
    float viscosity = dt*0.000001f;
    FOR_EACH_CELL {
        vec2f sum =
            viscosity*(
            + old_velocity(x - 1, y + 0)
            + old_velocity(x + 1, y + 0)
            + old_velocity(x + 0, y - 1)
            + old_velocity(x + 0, y + 1)
            )
            + old_velocity(x + 0, y + 0);
        new_velocity(x, y) = 1.0f/(1.0f + 4.0f*viscosity) * sum;
    }
    old_velocity.swap(new_velocity);
}

void project_velocity(){
    Grid<float> p(nx, ny);
    Grid<float> p2(nx, ny);
    Grid<float> div(nx, ny);

    FOR_EACH_CELL {
        float dx = old_velocity(x + 1, y + 0).x - old_velocity(x - 1, y + 0).x;
        float dy = old_velocity(x + 0, y + 1).y - old_velocity(x + 0, y - 1).y;
        div(x, y) = dx + dy;
        p(x, y) = 0.0f;
    }

    for (int k = 0; k < iterations; k++){
        FOR_EACH_CELL {
            float sum = -div(x, y)
                + p(x + 1, y + 0)
                + p(x - 1, y + 0)
                + p(x + 0, y + 1)
                + p(x + 0, y - 1);
            p2(x, y) = 0.25f*sum;
        }
        p.swap(p2);
    }

    FOR_EACH_CELL {
        old_velocity(x, y).x -= 0.5f*(p(x + 1, y + 0) - p(x - 1, y + 0));
        old_velocity(x, y).y -= 0.5f*(p(x + 0, y + 1) - p(x + 0, y - 1));
    }
}

float curl(int x, int y){
    return
        old_velocity(x, y + 1).x - old_velocity(x, y - 1).x +
        old_velocity(x - 1, y).y - old_velocity(x + 1, y).y;
}

void vorticity_confinement(){
    Grid<float> abs_curl(nx, ny);

    FOR_EACH_CELL {
        abs_curl(x, y) = fabsf(curl(x, y));
    }

    FOR_EACH_CELL {
        vec2f direction;
        direction.x = abs_curl(x + 0, y - 1) - abs_curl(x + 0, y + 1);
        direction.y = abs_curl(x + 1, y + 0) - abs_curl(x - 1, y + 0);

        direction = vorticity/(length(direction) + 1e-5f) * direction;

        if (x < nx/2) direction *= 0.0f;

        new_velocity(x, y) = old_velocity(x, y) + dt*curl(x, y)*direction;
    }

    old_velocity.swap(new_velocity);
}

void add_density(int px, int py, int r = 10, float value = 0.5f){
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++){
        float d = sqrtf(x*x + y*y);
        float u = smoothstep(float(r), 0.0f, d);
        old_density(px + x, py + y) += u*value;
    }
}

float randf(float a, float b){
    float u = rand()*(1.0f/RAND_MAX);
    return lerp(a, b, u);
}

float sign(float x){
    return
        x > 0.0f ? +1.0f :
        x < 0.0f ? -1.0f :
        0.0f;
}

void fluid_simulation_step(){
    FOR_EACH_CELL {
        if (x > nx*0.5f) continue;

        float r = 10.0f;
        old_velocity(x, y).x += randf(-r, +r);
        old_velocity(x, y).y += randf(-r, +r);
    }

    // dense regions rise up
    FOR_EACH_CELL {
        old_velocity(x, y).y += (old_density(x, y)*20.0f - 5.0f)*dt;
    }

    add_density(mouse.x, mouse.y, 10, 0.5f);

    // fast movement is dampened
    FOR_EACH_CELL {
        old_velocity(x, y) *= 0.999f;
    }

    // fade away
    FOR_EACH_CELL {
        old_density(x, y) *= 0.99f;
    }

    add_density(nx*0.25f, 30);
    add_density(nx*0.75f, 30);

    double t[10];

    t[0] = sec();
    vorticity_confinement();
    t[1] = sec();
    //diffuse_velocity();
    //project_velocity();
    advect_velocity();
    t[2] = sec();
    project_velocity();
    t[3] = sec();

    //diffuse_density();
    advect_density();
    t[4] = sec();

    // zero out stuff at bottom
    FOR_EACH_CELL {
        if (y < 10){
            old_density(x, y) = 0.0f;
            old_velocity(x, y) = vec2f{0.0f, 0.0f};
        }
    }

    for (int i = 0; i < 4; i++){
        t[i] = (t[i + 1] - t[i])*1000;
    }

    char title[256];
    snprintf(title, sizeof(title), "%f %f %f %f\n", t[0], t[1], t[2], t[3]);
    glutSetWindowTitle(title);
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

uint32_t swap_bytes(uint32_t x, int i, int j){
    union {
        uint32_t x;
        uint8_t bytes[4];
    } u;
    u.x = x;
    std::swap(u.bytes[i], u.bytes[j]);
    return u.x;
}

uint32_t rgba32(uint32_t r, uint32_t g, uint32_t b, uint32_t a){
    r = clamp(r, 0u, 255u);
    g = clamp(g, 0u, 255u);
    b = clamp(b, 0u, 255u);
    a = clamp(a, 0u, 255u);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

uint32_t rgba(float r, float g, float b, float a){
    return rgba32(r*256, g*256, b*256, a*256);
}

void on_frame(){
    CHECK_GL
    glClearColor(0.1f, 0.2f, 0.3f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    fluid_simulation_step();

    double t = sec();
    // density field to pixels
    FOR_EACH_CELL {
        float f = old_density(x, y);
        f = log2f(f*0.25f + 1.0f);
        float f3 = f*f*f;
        float r = 1.5f*f;
        float g = 1.5f*f3;
        float b = f3*f3;
        pixels(x, y) = rgba(r, g, b, 1.0);
    }
    double dt = sec() - t;
    printf("%f\n", dt*1000);

    // upload pixels to texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, nx, ny, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // draw texture
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(+1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(+1.0f, +1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, +1.0f);
    glEnd();

#if 0
    // To make video from frames:
    // ffmpeg -i frame_%d.ppm video.mp4
    static int frame = 0;
    char path[256];
    snprintf(path, sizeof(path), "frames/frame_%d.ppm", frame);
    screenshot(path);
    puts(path);
    frame++;
    if (frame >= 1024){
        exit(0);
    }
#endif
    glutSwapBuffers();
    CHECK_GL
}

void work(int frame){
    glutPostRedisplay();
    glutTimerFunc(20, work, frame + 1);
}

void on_move(int x, int y){
    y = h - 1 - y;
    mouse = vec2f{x*1.0f*nx/w, y*1.0f*ny/h};
}

void on_mouse_button(int button, int action, int x, int y){
    on_move(x, y);

    int down = action == GLUT_DOWN;

    if (button == GLUT_LEFT_BUTTON){
        if (down){
            add_density(mouse.x, mouse.y, 10, 300.0f);
        }
    }
}

int main(int argc, char **argv){
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(w, h);
    glutCreateWindow("");

    init();

    glutMouseFunc(on_mouse_button);
    glutMotionFunc(on_move);
    glutPassiveMotionFunc(on_move);

    glutDisplayFunc(on_frame);
    work(0);
    glutMainLoop();
    return 0;
}
