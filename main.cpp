#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <limits>
#include "gl.h"
#include "model.h"

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer

struct RandomShader : IShader {
    const Model &model;
    TGAColor color = {};
    vec3 tri[3];  // triangle in eye coordinates

    RandomShader(const Model &m) : model(m) {
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        return {false, color};                                    // do not discard the pixel
    }
};

struct PhongShader : IShader {
    const Model &model;
    vec3 uniform_l; // correctly transformed light dir
    mutable TGAColor color = {};
    vec3 tri[3];  // triangle in eye coordinates

    PhongShader(const Model &m, vec3 light_dir) : model(m), uniform_l(light_dir) {
    }

    virtual vec4 vertex(const int face, const int vert) {
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates
        if(vert==2){ // completed other verticies. can do math now!!
            vec3 frag_centre = (tri[0] + tri[1] + tri[2])/3;
            vec3 n = normalized(cross(tri[0]-tri[2], tri[0]-tri[1])); // fragment normal
            vec3 l = normalized(-1*uniform_l); // light ray dir (originating from fragment)
            vec3 r = normalized(2*n * dot(n, l)-l); // reflected light ray dir
            vec3 v = normalized(-1 * frag_centre); // fragment to camera center

            float cosalpha = fmax(0.2, dot(n, l)); // Diffuse (Lambert) intensity
            float cosbeta  = fmax(0.0, dot(r, v)); // Specular intensity
            float specular = pow(cosbeta, 5.0);    // Shininess exponent
            //std::cout << cosbeta << '\n';
            float col = fmin(1.0, 0.6 * cosalpha + 0.5 * specular);
            color = {static_cast<uint8_t>(col*255),static_cast<uint8_t>(col*255),static_cast<uint8_t>(col*255),255};
        }
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
        return {false, color};                                    // do not discard the pixel
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }

    constexpr int width  = 800;      // output image size
    constexpr int height = 800;
    constexpr vec3    eye{-1, 0, 2}; // camera position
    constexpr vec3 center{ 0, 0, 0}; // camera direction
    constexpr vec3     up{ 0, 1, 0}; // camera up vector

    constexpr vec3 sun_dir{0.8,0.5,0.8}; // sun direction

    lookat(eye, center, up);                                   // build the ModelView   matrix
    init_perspective(norm(eye-center));                        // build the Perspective matrix
    init_viewport(width/16, height/16, width*7/8, height*7/8); // build the Viewport matrix
    init_zbuffer(width, height);
    TGAImage framebuffer(width, height, TGAImage::RGB, {0, 0, 0, 255});

    for (int m=1; m<argc; m++) {                    // iterate through all input objects
        Model model(argv[m]);                       // load the data
        vec4 light_transform = ModelView * vec4{sun_dir.x, sun_dir.y, sun_dir.z, 0.0};
        vec3 light_eye_dir = normalized(light_transform.xyz());
        PhongShader shader(model, light_eye_dir);
        for (int f=0; f<model.nfaces(); f++) {      // iterate through all facets
            //shader.color = { static_cast<uint8_t>(col*255), static_cast<uint8_t>(col*255), static_cast<uint8_t>(col*255), 255 };
            Triangle clip = { shader.vertex(f, 0),  // assemble the primitive
                              shader.vertex(f, 1),
                              shader.vertex(f, 2) };
            rasterize(clip, shader, framebuffer);   // rasterize the primitive
        }
    }

    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}