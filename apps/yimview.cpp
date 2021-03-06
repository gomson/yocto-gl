//
// LICENSE:
//
// Copyright (c) 2016 -- 2017 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include "../yocto/yocto_cmd.h"
#include "../yocto/yocto_glu.h"
#include "../yocto/yocto_img.h"
#include "../yocto/yocto_math.h"

namespace yimview_app {

struct img {
    // image path
    std::string filename;

    // original image data size
    int width = 0;
    int height = 0;
    int ncomp = 0;

    // pixel data
    std::vector<float> hdr;
    std::vector<unsigned char> ldr;

    // opengl texture
    yglu::uint tex_glid = 0;

    // hdr controls
    float exposure = 0;
    float gamma = 2.2f;
    bool srgb = true;
    yimg::tonemap_type tonemap = yimg::tonemap_type::srgb;

    // check hdr
    bool is_hdr() const { return !hdr.empty(); }
};

struct params {
    std::vector<std::string> filenames;
    std::vector<img*> imgs;

    float exposure = 0;
    float gamma = 1;
    yimg::tonemap_type tonemap = yimg::tonemap_type::gamma;

    bool legacy_gl = false;

    int cur_img = 0;
    int cur_background = 0;
    float zoom = 1;
    ym::vec2f offset = ym::vec2f();

    float background = 0;

    void* widget_ctx = nullptr;

    ~params() {
        for (auto img : imgs) delete img;
    }
};

std::vector<img*> load_images(const std::vector<std::string>& img_filenames,
                              float exposure, yimg::tonemap_type tonemap,
                              float gamma) {
    auto imgs = std::vector<img*>();
    for (auto filename : img_filenames) {
        imgs.push_back(new img());
        auto img = imgs.back();
        img->filename = filename;
        auto yim = yimg::load_image(filename);
        img->width = yim->width;
        img->height = yim->height;
        img->ncomp = yim->ncomp;
        if (yim->hdr)
            img->hdr = std::vector<float>(
                yim->hdr, yim->hdr + yim->width * yim->height * yim->ncomp);
        if (yim->ldr)
            img->ldr = std::vector<unsigned char>(
                yim->ldr, yim->ldr + yim->width * yim->height * yim->ncomp);
        delete yim;
        if (!img->hdr.empty()) {
            img->ldr.resize(img->hdr.size());
            yimg::tonemap_image(img->width, img->height, img->ncomp,
                                img->hdr.data(), img->ldr.data(), img->exposure,
                                img->tonemap, img->gamma);
            img->exposure = exposure;
            img->gamma = gamma;
            img->tonemap = tonemap;
        }
        if (img->hdr.empty() && img->ldr.empty()) {
            printf("cannot load image %s\n", img->filename.c_str());
            exit(1);
        }
        img->tex_glid = 0;
    }
    return imgs;
}

void init_params(params* pars, ycmd::parser* parser) {
    static auto tmtype_names = std::vector<std::pair<std::string, int>>{
        {"default", (int)yimg::tonemap_type::def},
        {"linear", (int)yimg::tonemap_type::linear},
        {"srgb", (int)yimg::tonemap_type::srgb},
        {"gamma", (int)yimg::tonemap_type::gamma},
        {"filmic", (int)yimg::tonemap_type::filmic}};

    pars->exposure =
        ycmd::parse_optf(parser, "--exposure", "-e", "hdr image exposure", 0);
    pars->gamma =
        ycmd::parse_optf(parser, "--gamma", "-g", "hdr image gamma", 2.2f);
    pars->tonemap = (yimg::tonemap_type)ycmd::parse_opte(
        parser, "--tonemap", "-t", "hdr image tonemap",
        (int)yimg::tonemap_type::srgb, tmtype_names);
    pars->legacy_gl = ycmd::parse_flag(parser, "--legacy_opengl", "-L",
                                       "uses legacy OpenGL", false);
    auto filenames =
        ycmd::parse_argas(parser, "image", "image filename", {}, true);

    // loading images
    pars->imgs =
        load_images(filenames, pars->exposure, pars->tonemap, pars->gamma);
}
}  // namespace

const int hud_width = 256;

void text_callback(yglu::ui::window* win, unsigned int key) {
    auto pars = (yimview_app::params*)get_user_pointer(win);
    switch (key) {
        case ' ':
        case '.':
            pars->cur_img = (pars->cur_img + 1) % pars->imgs.size();
            break;
        case ',':
            pars->cur_img = (pars->cur_img - 1 + (int)pars->imgs.size()) %
                            pars->imgs.size();
            break;
        case '-':
        case '_': pars->zoom /= 2; break;
        case '+':
        case '=': pars->zoom *= 2; break;
        case '[': pars->exposure -= 1; break;
        case ']': pars->exposure += 1; break;
        case '{': pars->gamma -= 0.1f; break;
        case '}': pars->gamma += 0.1f; break;
        case '1':
            pars->exposure = 0;
            pars->gamma = 1;
            break;
        case '2':
            pars->exposure = 0;
            pars->gamma = 2.2f;
            break;
        case 'z': pars->zoom = 1; break;
        case 'h':
            // TODO: hud
            break;
        default: printf("unsupported key\n"); break;
    }
}

void draw_image(yglu::ui::window* win) {
    auto pars = (yimview_app::params*)get_user_pointer(win);
    auto framebuffer_size = get_framebuffer_size(win);
    yglu::set_viewport({0, 0, framebuffer_size[0], framebuffer_size[1]});

    auto img = pars->imgs[pars->cur_img];

    // begin frame
    yglu::clear_buffers(
        {pars->background, pars->background, pars->background, 0});

    // draw image
    auto window_size = get_window_size(win);
    if (pars->legacy_gl) {
        yglu::legacy::draw_image(img->tex_glid, img->width, img->height,
                                 window_size[0], window_size[1],
                                 pars->offset[0], pars->offset[1], pars->zoom);
    } else {
        yglu::modern::shade_image(img->tex_glid, img->width, img->height,
                                  window_size[0], window_size[1],
                                  pars->offset[0], pars->offset[1], pars->zoom);
    }
}

template <typename T>
ym::vec<T, 4> lookup_image(int w, int h, int nc, const T* pixels, int x, int y,
                           T one) {
    if (x < 0 || y < 0 || x > w - 1 || y > h - 1) return {0, 0, 0, 0};
    auto v = ym::vec<T, 4>{0, 0, 0, 0};
    auto vv = pixels + ((w * y) + x) * nc;
    switch (nc) {
        case 1: v = {vv[0], 0, 0, one}; break;
        case 2: v = {vv[0], vv[1], 0, one}; break;
        case 3: v = {vv[0], vv[1], vv[2], one}; break;
        case 4: v = {vv[0], vv[1], vv[2], vv[3]}; break;
        default: assert(false);
    }
    return v;
}

void draw_widgets(yglu::ui::window* win) {
    static auto tmtype_names = std::vector<std::pair<std::string, int>>{
        {"default", (int)yimg::tonemap_type::def},
        {"linear", (int)yimg::tonemap_type::linear},
        {"srgb", (int)yimg::tonemap_type::srgb},
        {"gamma", (int)yimg::tonemap_type::gamma},
        {"filmic", (int)yimg::tonemap_type::filmic}};

    auto pars = (yimview_app::params*)get_user_pointer(win);
    auto& img = pars->imgs[pars->cur_img];
    auto mouse_pos = (ym::vec2f)get_mouse_posf(win);
    if (begin_widgets(win)) {
        dynamic_widget_layout(win, 1);
        label_widget(win, img->filename);
        dynamic_widget_layout(win, 3);
        int_label_widget(win, "w", img->width);
        int_label_widget(win, "h", img->height);
        int_label_widget(win, "c", img->ncomp);
        auto xy = (mouse_pos - pars->offset) / pars->zoom;
        auto ij = ym::vec2i{(int)round(xy[0]), (int)round(xy[1])};
        auto inside = ij[0] >= 0 && ij[1] >= 0 && ij[0] < img->width &&
                      ij[1] < img->height;
        dynamic_widget_layout(win, 4);
        auto ldrp =
            lookup_image(img->width, img->height, img->ncomp, img->ldr.data(),
                         ij[0], ij[1], (unsigned char)255);
        int_label_widget(win, "r", (inside) ? ldrp[0] : 0);
        int_label_widget(win, "g", (inside) ? ldrp[1] : 0);
        int_label_widget(win, "b", (inside) ? ldrp[2] : 0);
        int_label_widget(win, "a", (inside) ? ldrp[3] : 0);
        if (img->is_hdr()) {
            auto hdrp = lookup_image(img->width, img->height, img->ncomp,
                                     img->hdr.data(), ij[0], ij[1], 1.0f);
            dynamic_widget_layout(win, 2);
            float_label_widget(win, "r", (inside) ? hdrp[0] : 0);
            float_label_widget(win, "g", (inside) ? hdrp[1] : 0);
            float_label_widget(win, "b", (inside) ? hdrp[2] : 0);
            float_label_widget(win, "a", (inside) ? hdrp[3] : 0);
            dynamic_widget_layout(win, 1);
            float_widget(win, "exposure", &pars->exposure, -20, 20, 1);
            float_widget(win, "gamma", &pars->gamma, 0.1, 5, 0.1);
            enum_widget(win, "tonemap", (int*)&pars->tonemap, tmtype_names);
        }
    }
    end_widgets(win);
}

void window_refresh_callback(yglu::ui::window* win) {
    draw_image(win);
    draw_widgets(win);
    swap_buffers(win);
}

void run_ui(yimview_app::params* pars) {
    // window
    auto win = yglu::ui::init_window(pars->imgs[0]->width + hud_width,
                                     pars->imgs[0]->height, "yimview",
                                     pars->legacy_gl, pars);
    set_callbacks(win, text_callback, window_refresh_callback);

    // window values
    int mouse_button = 0;
    ym::vec2f mouse_pos, mouse_last;

    init_widgets(win);

    // load textures
    for (auto& img : pars->imgs) {
        if (pars->legacy_gl) {
            img->tex_glid = yglu::legacy::make_texture(
                img->width, img->height, img->ncomp,
                (unsigned char*)img->ldr.data(), false, false);
        } else {
            img->tex_glid = yglu::modern::make_texture(
                img->width, img->height, img->ncomp,
                (unsigned char*)img->ldr.data(), false, false, false);
        }
    }

    while (!should_close(win)) {
        mouse_last = mouse_pos;
        mouse_pos = get_mouse_posf(win);
        mouse_button = get_mouse_button(win);

        auto& img = pars->imgs[pars->cur_img];
        set_window_title(win,
                         ("yimview | " + img->filename + " | " +
                          std::to_string(img->width) + "x" +
                          std::to_string(img->height) + "@" +
                          std::to_string(img->ncomp))
                             .c_str());

        // handle mouse
        if (mouse_button && mouse_pos != mouse_last &&
            !get_widget_active(win)) {
            switch (mouse_button) {
                case 1: pars->offset += mouse_pos - mouse_last; break;
                case 2:
                    pars->zoom *=
                        powf(2, (mouse_pos[0] - mouse_last[0]) * 0.001f);
                    break;
                default: break;
            }
        }

        // refresh hdr
        if (img->is_hdr() &&
            (pars->exposure != img->exposure || pars->gamma != img->gamma ||
             pars->tonemap != img->tonemap)) {
            yimg::tonemap_image(img->width, img->height, img->ncomp,
                                img->hdr.data(), img->ldr.data(),
                                pars->exposure, pars->tonemap, pars->gamma);
            img->exposure = pars->exposure;
            img->gamma = pars->gamma;
            img->tonemap = pars->tonemap;
            if (pars->legacy_gl) {
                yglu::legacy::update_texture(img->tex_glid, img->width,
                                             img->height, img->ncomp,
                                             img->ldr.data(), false);
            } else {
                yglu::modern::update_texture(img->tex_glid, img->width,
                                             img->height, img->ncomp,
                                             img->ldr.data(), false);
            }
        }

        // draw
        draw_image(win);
        draw_widgets(win);

        // swap buffers
        swap_buffers(win);

        // event hadling
        wait_events(win);
    }

    clear_widgets(win);
    clear_window(win);
}

int main(int argc, char* argv[]) {
    // command line params
    auto pars = new yimview_app::params();
    auto parser = ycmd::make_parser(argc, argv, "view images");
    yimview_app::init_params(pars, parser);
    ycmd::check_parser(parser);

    // run ui
    run_ui(pars);

    // done
    delete pars;
    return EXIT_SUCCESS;
}
