#pragma once
#include <epoxy/gl.h>

namespace dune3d {
class BackgroundRenderer {
public:
    BackgroundRenderer(class Canvas &c);
    void realize();
    void render();
    void render_error();

private:
    Canvas &m_ca;

    GLuint m_program;
    GLuint m_vao;

    GLuint m_color_top_loc;
    GLuint m_color_bottom_loc;
    GLuint m_alpha_loc;
    GLuint m_grid_enabled_loc;
    GLuint m_grid_spacing_loc;
    GLuint m_grid_minor_color_loc;
    GLuint m_grid_major_color_loc;
    GLuint m_grid_axis_x_color_loc;
    GLuint m_grid_axis_y_color_loc;
    GLuint m_viewport_size_loc;
    GLuint m_projmat_viewmat_inv_loc;
};
} // namespace dune3d
