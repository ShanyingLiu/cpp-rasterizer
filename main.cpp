#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <limits>
#include "gl.h"
#include "model.h"

extern mat<4,4> ModelView, Perspective; // "OpenGL" state matrices and
extern std::vector<double> zbuffer;     // the depth buffer
TGAImage normal_map;
constexpr int width  = 1024;      // output image size
constexpr int height = 1024;

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
    std::pair<float,float> varying_uv[3]; 
    //vec3 varying_uv[3]; //uv per vertex, not to be sampled
    std::pair<float,float> uv; // uv coordinates

    PhongShader(const Model &m, vec3 light_dir) : model(m){
        uniform_l = normalized((ModelView*vec4{light_dir.x, light_dir.y, light_dir.z, 0.}).xyz());
    }

    virtual vec4 vertex(const int face, const int vert) { // handles the vertices in each tri
        vec3 v = model.vert(face, vert);                          // current vertex in object coordinates
        vec3 n = model.normal(face, vert); 
        uv = model.uv(face, vert);
        //TGAColor normal_color = normal_map.get(int(uv.first*normal_map.width()), int(((1-uv.second)*normal_map.height())));
        //varying_nrm[vert] = (ModelView.invert_transpose() * vec4{n.x, n.y, n.z, 0.}).xyz(); // transform coordinate space
        //varying_uv[vert] = (ModelView.invert_transpose() * vec4{static_cast<double>(normal_color.bgra[2]/255.0 *2 -1), static_cast<double>(normal_color.bgra[1]/255.0 *2 -1), static_cast<double>(normal_color.bgra[0]/255.0 *2 -1), 0.}).xyz();
        varying_uv[vert] = model.uv(face, vert);
        vec4 gl_Position = ModelView * vec4{v.x, v.y, v.z, 1.};
        tri[vert] = gl_Position.xyz();                            // in eye coordinates
        return Perspective * gl_Position;                         // in clip coordinates
    }

    virtual std::pair<bool,TGAColor> fragment(const vec3 bar) const {
       
            TGAColor gl_FragColor = {255, 255, 255, 255};             // output color of the fragment
            //vec3 n = normalized(cross(tri[1]-tri[0], tri[2]-tri[0])); // faceted triangle
            TGAColor normal_color = normal_map.get(int(uv.first*normal_map.width()), int(((1-uv.second)*normal_map.height())));

            // interpolate for accurate u v across random smaple point on triagnle
            // use barycentric weights to get accurate coordinate lookup
            double u = varying_uv[0].first  * bar[0] + varying_uv[1].first  * bar[1] + varying_uv[2].first  * bar[2];
            double v = varying_uv[0].second * bar[0] + varying_uv[1].second * bar[1] + varying_uv[2].second * bar[2];
            TGAColor uv_color = normal_map.get(int(u*normal_map.width()), int(((1-v)*normal_map.height())));
            vec3 n_obj = { uv_color.bgra[2]/255.*2-1,  uv_color.bgra[1]/255.*2-1,  uv_color.bgra[0]/255.*2-1 };
            vec3 n = normalized((ModelView.invert_transpose() * vec4{n_obj.x, n_obj.y,n_obj.z, 0.}).xyz());
            //vec3 n = normalized(vec3_uv); 
            /* // smooth appearance
            vec3 n = normalized(varying_nrm[0] * bar[0] +
                                varying_nrm[1] * bar[1] +
                                varying_nrm[2] * bar[2]);             // per-vertex normal interpolation
            */

            vec3 r = normalized(n * (n * uniform_l)*2 - uniform_l); // reflected light direction
            double ambient = .3;                                      // ambient light intensity
            double diff = std::max(0., n * uniform_l);                        // diffuse light intensity
            double spec = std::pow(std::max(r.z, 0.), 35);            // specular intensity, note that the camera lies on the z-axis (in eye coordinates), therefore simple r.z, since (0,0,1)*(r.x, r.y, r.z) = r.z
            for (int channel : {0,1,2})
                gl_FragColor[channel] *= std::min(1., ambient + .4*diff + .9*spec);
            return {false, gl_FragColor};                             // do not discard the pixel
        
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " obj/model.obj" << std::endl;
        return 1;
    }
    normal_map.read_tga_file("obj/african_head_nm.tga");

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