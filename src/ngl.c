// NDBX OpenGL Utility functions

#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "nfile.h"
#include "obj.h"

#include "ngl.h"
#include "nwm.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Error checking ////////////////////////////////////////////////////////////

void ngl_check_gl_error(const char *file, int line) {
    GLenum err = glGetError();
    int has_error = 0;
    while (err != GL_NO_ERROR) {
        has_error = 1;
        char *msg = NULL;
        switch(err) {
            case GL_INVALID_OPERATION:
            msg = "GL_INVALID_OPERATION";
            break;
            case GL_INVALID_ENUM:
            msg = "GL_INVALID_ENUM";
            fprintf(stderr, "OpenGL error: GL_INVALID_ENUM\n");
            break;
            case GL_INVALID_VALUE:
            msg = "GL_INVALID_VALUE";
            fprintf(stderr, "OpenGL error: GL_INVALID_VALUE\n");
            break;
            case GL_OUT_OF_MEMORY:
            msg = "GL_OUT_OF_MEMORY";
            fprintf(stderr, "OpenGL error: GL_OUT_OF_MEMORY\n");
            break;
            default:
            msg = "UNKNOWN_ERROR";
        }
        fprintf(stderr, "OpenGL error: %s - %s:%d\n", msg, file, line);
        err = glGetError();
    }
    if (has_error) {
        exit(EXIT_FAILURE);
    }
}

// Color /////////////////////////////////////////////////////////////////////

ngl_color ngl_color_init_rgba(float red, float green, float blue, float alpha) {
    ngl_color c;
    c.red = red;
    c.green = green;
    c.blue = blue;
    c.alpha = alpha;
    return c;
}

// Background ////////////////////////////////////////////////////////////////

void ngl_clear(float red, float green, float blue, float alpha) {
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ngl_clear_depth() {
    glClear(GL_DEPTH_BUFFER_BIT);
}

// Shaders ///////////////////////////////////////////////////////////////////

void ngl_check_compile_error(GLuint shader) {
    const int LOG_MAX_LENGTH = 2048;
    GLint status = -1;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char infoLog[LOG_MAX_LENGTH];
        glGetShaderInfoLog(shader, LOG_MAX_LENGTH, NULL, infoLog);
        fprintf(stderr, "Shader %d compile error: %s\n", shader, infoLog);
        exit(EXIT_FAILURE);
    }
}

void ngl_check_link_error(GLuint program) {
    const int LOG_MAX_LENGTH = 2048;
    GLint status = -1;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        char infoLog[LOG_MAX_LENGTH];
        glGetProgramInfoLog(program, LOG_MAX_LENGTH, NULL, infoLog);
        fprintf(stderr, "Shader link error: %s\n", infoLog);
        exit(EXIT_FAILURE);
    }
}

ngl_shader *ngl_shader_new(GLenum draw_mode, const char *vertex_shader_source, const char *fragment_shader_source) {
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
    glCompileShader(vertex_shader);
    ngl_check_compile_error(vertex_shader);
    NGL_CHECK_ERROR();

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
    glCompileShader(fragment_shader);
    ngl_check_compile_error(fragment_shader);
    NGL_CHECK_ERROR();

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    ngl_check_link_error(program);
    NGL_CHECK_ERROR();

    ngl_shader *shader = calloc(1, sizeof(ngl_shader));
    shader->draw_mode = draw_mode;
    shader->vertex_shader = vertex_shader;
    shader->fragment_shader = fragment_shader;
    shader->program = program;
    shader->time_uniform = glGetUniformLocation(program, "uTime");
    shader->view_matrix_uniform = glGetUniformLocation(program, "uViewMatrix");
    shader->projection_matrix_uniform = glGetUniformLocation(program, "uProjectionMatrix");

    return shader;
}

ngl_shader *ngl_shader_new_from_file(GLenum draw_mode, const char *vertex_fname, const char *fragment_fname) {
    char *vertex_shader_source = nfile_read(vertex_fname);
    char *fragment_shader_source = nfile_read(fragment_fname);
    ngl_shader *shader = ngl_shader_new(draw_mode, vertex_shader_source, fragment_shader_source);
    free(vertex_shader_source);
    free(fragment_shader_source);
    return shader;
}

void ngl_shader_uniform_set_float(ngl_shader *shader, const char *uniform_name, GLfloat value) {
    glUseProgram(shader->program);
    int loc = glGetUniformLocation(shader->program, uniform_name);
    NGL_CHECK_ERROR();
    glUniform1f(loc, value);
    NGL_CHECK_ERROR();
    glUseProgram(0);
}

void ngl_shader_free(ngl_shader *shader) {
    glDeleteShader(shader->vertex_shader);
    glDeleteShader(shader->fragment_shader);
    glDeleteProgram(shader->program);
    NGL_CHECK_ERROR();
    free(shader);
}

// Textures //////////////////////////////////////////////////////////////////

ngl_texture *ngl_texture_new(ngl_shader *shader, const char *uniform_name) {
    ngl_texture *texture = calloc(1, sizeof(ngl_texture));
    texture->shader = shader;

    glGenTextures(1, &texture->texture_id);
    NGL_CHECK_ERROR();
    glActiveTexture(GL_TEXTURE0);
    NGL_CHECK_ERROR();
    glBindTexture(GL_TEXTURE_2D, texture->texture_id);
    NGL_CHECK_ERROR();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    NGL_CHECK_ERROR();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    NGL_CHECK_ERROR();

    GLuint u_texture = glGetUniformLocation(shader->program, uniform_name);
    NGL_CHECK_ERROR();
    if (u_texture == -1) {
        fprintf(stderr, "WARN OpenGL: Could not find uniform %s\n", uniform_name);
    }

    return texture;
}

ngl_texture *ngl_texture_new_from_file(const char *file_name, ngl_shader *shader, const char *uniform_name) {
    int width, height, channels;
    uint8_t *image_data = stbi_load(file_name, &width, &height, &channels, 4);
    if (!image_data) {
        fprintf (stderr, "ERROR: could not load texture %s\n", file_name);
        exit(1);
    }
    ngl_texture *texture = ngl_texture_new(shader, uniform_name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    NGL_CHECK_ERROR();
    free(image_data);
    return texture;
}
//SM MOD: Update the texture from image
void ngl_texture_update_from_file(ngl_texture *texture,const char *file_name ) {
    int width, height, channels;
    uint8_t *image_data = stbi_load(file_name, &width, &height, &channels, 4);
    if (!image_data) {
        fprintf (stderr, "ERROR: could not load texture %s\n", file_name);
        // exit(1);
    }
    else{
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
        NGL_CHECK_ERROR();
        free(image_data);
        NGL_CHECK_ERROR();
    }
}


// Update the texture with the given data.
// Channels is the number of color channels. 1 = red only, 2 = red/green, 3 = r/g/b, 4 = r/g/b/a.
void ngl_texture_update(ngl_texture *texture, nut_buffer *buffer, int width, int height) {
    GLint format;
    if (buffer->channels == 1) {
        format = GL_RED;
    } else if (buffer->channels == 2) {
        format = GL_RG;
    } else if (buffer->channels == 3) {
        format = GL_RGB;
    } else if (buffer->channels == 4) {
        format = GL_RGBA;
    } else {
        fprintf(stderr, "ERROR OpenGL: Invalid texture channels %d\n", buffer->channels);
        exit(1);
    }

    glActiveTexture(GL_TEXTURE0);
    NGL_CHECK_ERROR();
    glBindTexture(GL_TEXTURE_2D, texture->texture_id);
    NGL_CHECK_ERROR();

    if (width * height > buffer->length) {
        fprintf(stderr, "ERROR ngl_texture_update: Invalid width / height (%d x %d) for buffer length %d\n", width, height, buffer->length);
        exit(1);
    }

    if (buffer->type == NUT_BUFFER_U8) {
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, buffer->data.u8);
    } else {
        const int size = width * height * buffer->channels;
        float *tex = calloc(size, sizeof(float));
        for (int i = 0; i < size; i++) {
            tex[i] = (float) buffer->data.f64[i];
        }
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_FLOAT, tex);
        free(tex);
    }
    NGL_CHECK_ERROR();
}

void ngl_texture_free(ngl_texture *texture) {
    glDeleteTextures(1, &texture->texture_id);
    NGL_CHECK_ERROR();
    free(texture);
}

// Model initialization //////////////////////////////////////////////////////

ngl_model* ngl_model_new(int component_count, int point_count, float* positions, float* normals, float* uvs) {
    ngl_model *model = calloc(1, sizeof(ngl_model));
    model->point_count = point_count;
    model->transform = mat4_init_identity();

    if (positions != NULL) {
        glGenBuffers(1, &model->position_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, model->position_vbo);
        glBufferData(GL_ARRAY_BUFFER, point_count * component_count * sizeof(GLfloat), positions, GL_DYNAMIC_DRAW);
        NGL_CHECK_ERROR();
    } else {
        model->position_vbo = -1;
    }

    if (normals != NULL) {
        glGenBuffers(1, &model->normal_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, model->normal_vbo);
        glBufferData(GL_ARRAY_BUFFER, point_count * component_count * sizeof(GLfloat), normals, GL_DYNAMIC_DRAW);
        NGL_CHECK_ERROR();
    } else {
        model->normal_vbo = -1;
    }

    if (uvs != NULL) {
        glGenBuffers(1, &model->uv_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, model->uv_vbo);
        glBufferData(GL_ARRAY_BUFFER, point_count * 2 * sizeof(GLfloat), uvs, GL_DYNAMIC_DRAW);
        NGL_CHECK_ERROR();
    } else {
        model->uv_vbo = -1;
    }

    glGenVertexArrays(1, &model->vao);
    glBindVertexArray(model->vao);

    if (positions != NULL) {
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, model->position_vbo);
        glVertexAttribPointer(0, component_count, GL_FLOAT, GL_FALSE, 0, NULL);
        NGL_CHECK_ERROR();
    }

    if (normals != NULL) {
        glEnableVertexAttribArray(1);
        glBindBuffer(GL_ARRAY_BUFFER, model->normal_vbo);
        glVertexAttribPointer(1, component_count, GL_FLOAT, GL_FALSE, 0, NULL);
        NGL_CHECK_ERROR();
    }

    if (uvs != NULL) {
        glEnableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, model->uv_vbo);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, NULL);
        NGL_CHECK_ERROR();
    }

    return model;
}

ngl_model* ngl_model_new_with_buffer(nut_buffer *buffer) {
    int size = buffer->channels * buffer->length;
    float *positions = calloc(size, sizeof(float));
    for (int i = 0; i < size; i++) {
        positions[i] = nut_buffer_get_f64(buffer, i);
    }
    ngl_model *model = ngl_model_new(buffer->channels, buffer->length, positions, NULL, NULL);
    free(positions);
    return model;
}

ngl_model* ngl_model_new_grid_points(int row_count, int column_count, float row_height, float column_width) {
    int point_count = row_count * column_count;
    float* points = calloc(point_count * 2, sizeof(float));
    float total_width = (column_count - 1) * column_width;
    float total_height = (row_count - 1) * row_height;
    float left = - total_width / 2;
    float top = - total_height / 2;
    int i = 0;
    for (int y = 0; y < row_count; y++) {
        for (int x = 0; x < column_count; x++) {
            points[i++] = left + x * column_width;
            points[i++] = top + y * row_height;
        }
    }
    ngl_model *model = ngl_model_new(2, i / 2, points, NULL, NULL);
    free(points);
    return model;
}

ngl_model* _ngl_model_new_grid_triangles_with_buffer(int row_count, int column_count, float row_height, float column_width, float height_multiplier, int buffer_stride, int buffer_offset, const float *buffer) {
    int square_count = (row_count - 1) * (column_count - 1);
    int face_count = square_count * 2;
    int point_count = face_count * 3;
    float* points = calloc(point_count * 3, sizeof(float));
    float* normals = calloc(point_count * 3, sizeof(float));
    float* uvs = calloc(point_count * 2, sizeof(float));
    float total_width = (column_count - 1) * column_width;
    float total_height = (row_count - 1) * row_height;
    float left = - total_width / 2;
    float top = - total_height / 2;
    int pt_index = 0;
    int uv_index = 0;
    for (int ri = 0; ri < row_count - 1; ri++) {
        for (int ci = 0; ci < column_count - 1; ci++) {
            float x = left + ci * column_width;
            float z = top + ri * row_height;

            float y11, y12, y21, y22;
            if (buffer != NULL) {
                y11 = buffer[buffer_offset + (ri * column_count * buffer_stride) + ci * buffer_stride] * height_multiplier;
                y12 = buffer[buffer_offset + (ri * column_count * buffer_stride) + (ci + 1) * buffer_stride] * height_multiplier;
                y21 = buffer[buffer_offset + ((ri + 1) * column_count * buffer_stride) + ci * buffer_stride] * height_multiplier;
                y22 = buffer[buffer_offset + ((ri + 1) * column_count * buffer_stride) + (ci + 1) * buffer_stride] * height_multiplier;
            } else {
                y11 = y12 = y21 = y22 = 0;
            }

            vec3 v11 = vec3_init(x, y11, z);
            vec3 v12 = vec3_init(x + column_width, y12, z);
            vec3 v21 = vec3_init(x, y21, z + row_height);
            vec3 v22 = vec3_init(x + column_width, y22, z + row_height);
            vec3 n1 = vec3_normal(&v11, &v12, &v21);
            vec3 n2 = vec3_normal(&v12, &v22, &v21);
            vec2 uv11 = vec2_init(ci / (float) (column_count - 1), ri / (float) (row_count - 1));
            vec2 uv12 = vec2_init((ci + 1) / (float) (column_count - 1), ri / (float) (row_count - 1));
            vec2 uv21 = vec2_init(ci / (float) (column_count - 1), (ri + 1) / (float) (row_count - 1));
            vec2 uv22 = vec2_init((ci + 1) / (float) (column_count - 1), (ri + 1) / (float) (row_count - 1));

            points[pt_index] = v11.x;
            points[pt_index + 1] = v11.y;
            points[pt_index + 2] = v11.z;
            normals[pt_index] = n1.x;
            normals[pt_index + 1] = n1.y;
            normals[pt_index + 2] = n1.z;
            uvs[uv_index] = uv11.x;
            uvs[uv_index + 1] = uv11.y;

            points[pt_index + 3] = v12.x;
            points[pt_index + 4] = v12.y;
            points[pt_index + 5] = v12.z;
            normals[pt_index + 3] = n1.x;
            normals[pt_index + 4] = n1.y;
            normals[pt_index + 5] = n1.z;
            uvs[uv_index + 2] = uv12.x;
            uvs[uv_index + 3] = uv12.y;

            points[pt_index + 6] = v21.x;
            points[pt_index + 7] = v21.y;
            points[pt_index + 8] = v21.z;
            normals[pt_index + 6] = n1.x;
            normals[pt_index + 7] = n1.y;
            normals[pt_index + 8] = n1.z;
            uvs[uv_index + 4] = uv21.x;
            uvs[uv_index + 5] = uv21.y;

            points[pt_index + 9] = v12.x;
            points[pt_index + 10] = v12.y;
            points[pt_index + 11] = v12.z;
            normals[pt_index + 9] = n2.x;
            normals[pt_index + 10] = n2.y;
            normals[pt_index + 11] = n2.z;
            uvs[uv_index + 6] = uv12.x;
            uvs[uv_index + 7] = uv12.y;

            points[pt_index + 12] = v22.x;
            points[pt_index + 13] = v22.y;
            points[pt_index + 14] = v22.z;
            normals[pt_index + 12] = n2.x;
            normals[pt_index + 13] = n2.y;
            normals[pt_index + 14] = n2.z;
            uvs[uv_index + 8] = uv22.x;
            uvs[uv_index + 9] = uv22.y;

            points[pt_index + 15] = v21.x;
            points[pt_index + 16] = v21.y;
            points[pt_index + 17] = v21.z;
            normals[pt_index + 15] = n2.x;
            normals[pt_index + 16] = n2.y;
            normals[pt_index + 17] = n2.z;
            uvs[uv_index + 10] = uv21.x;
            uvs[uv_index + 11] = uv21.y;

            pt_index += 18;
            uv_index += 12;
        }
    }
    ngl_model* model = ngl_model_new(3, point_count, points, normals, uvs);
    free(points);
    free(normals);
    free(uvs);
    return model;
}

ngl_model* ngl_model_new_grid_triangles(int row_count, int column_count, float row_height, float column_width) {
    return _ngl_model_new_grid_triangles_with_buffer(row_count, column_count, row_height, column_width, 0, 0, 0, NULL);
}

// We ask for the channels to calculate the stride, but only use the first value.
ngl_model* ngl_model_new_with_height_map(int row_count, int column_count, float row_height, float column_width, float height_multiplier, int stride, int offset, const float *buffer) {
    return _ngl_model_new_grid_triangles_with_buffer(row_count, column_count, row_height, column_width, height_multiplier, stride, offset, buffer);
}

ngl_model* ngl_model_load_obj(const char* fname) {
    ngl_model *model = calloc(1, sizeof(ngl_model));

    model->transform = mat4_init_identity();

    static float *points;
    static float *normals;
    int face_count;
    obj_parse(fname, &points, &normals, &face_count);
    // Each triangle face has 3 vertices
    model->point_count = face_count * 3;

    glGenBuffers(1, &model->position_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, model->position_vbo);
    glBufferData(GL_ARRAY_BUFFER, model->point_count * 3 * sizeof(GLfloat), points, GL_DYNAMIC_DRAW);
    NGL_CHECK_ERROR();

    glGenBuffers(1, &model->normal_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, model->normal_vbo);
    glBufferData(GL_ARRAY_BUFFER, model->point_count * 3 * sizeof(GLfloat), normals, GL_DYNAMIC_DRAW);
    NGL_CHECK_ERROR();

    // Vertex array object
    // FIXME glUseProgram?
    glGenVertexArrays(1, &model->vao);
    glBindVertexArray(model->vao);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, model->position_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, model->normal_vbo);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    NGL_CHECK_ERROR();

    return model;
}

void ngl_model_translate(ngl_model *model, float tx, float ty, float tz) {
    mat4 m = mat4_init_translate(tx, ty, tz);
    model->transform = mat4_mul(&model->transform, &m);
}

void ngl_model_free(ngl_model *model) {
    glDeleteBuffers(1, &model->position_vbo);
    glDeleteBuffers(1, &model->normal_vbo);
    glDeleteBuffers(1, &model->uv_vbo);
    glDeleteVertexArrays(1, &model->vao);
    free(model);
}

// Camera ////////////////////////////////////////////////////////////////////

#define NGL_CAMERA_DEFAULT_FOV 100

ngl_camera* ngl_camera_new() {
    ngl_camera *camera = calloc(1, sizeof(ngl_camera));
    camera->view = mat4_init_identity();
    camera->projection = mat4_init_perspective(NGL_CAMERA_DEFAULT_FOV, 800 / 600, 0.01f, 1000.0f);
    camera->background = ngl_color_init_rgba(0, 0, 1, 1);
    return camera;
}

ngl_camera* ngl_camera_new_look_at(float x, float y, float z) {
    ngl_camera *camera = calloc(1, sizeof(ngl_camera));
    vec3 loc = vec3_init(x, y, z);
    vec3 target = vec3_zero();
    vec3 up = vec3_init(0.0f, 1.0f, 0.0f);
    camera->view = mat4_init_look_at(&loc, &target, &up);
    camera->projection = mat4_init_perspective(NGL_CAMERA_DEFAULT_FOV, 800 / 600, 0.01f, 1000.0f);
    camera->background = ngl_color_init_rgba(0, 0, 1, 1);
    return camera;
}

void ngl_camera_translate(ngl_camera *camera, float tx, float ty, float tz) {
    mat4 m = mat4_init_translate(tx, ty, tz);
    camera->view = mat4_mul(&camera->view, &m);
}

void ngl_camera_rotate_x(ngl_camera *camera, float deg) {
    mat4 m = mat4_init_rotation_x(deg);
    camera->view = mat4_mul(&camera->view, &m);
}

void ngl_camera_rotate_y(ngl_camera *camera, float deg) {
    mat4 m = mat4_init_rotation_y(deg);
    camera->view = mat4_mul(&camera->view, &m);
}

void ngl_camera_rotate_z(ngl_camera *camera, float deg) {
    mat4 m = mat4_init_rotation_z(deg);
    camera->view = mat4_mul(&camera->view, &m);
}

void ngl_camera_free(ngl_camera* camera) {
    free(camera);
}

// Skybox /////////////////////////////////////////////////////////////////////

void _ngl_skybox_load_side(GLuint texture_id, GLenum side_target, const char *file_name) {
    glBindTexture (GL_TEXTURE_CUBE_MAP, texture_id);
    int width, height, n;
    int force_channels = 4;
    unsigned char *image_data = stbi_load(file_name, &width, &height, &n, force_channels);
    if (!image_data) {
        fprintf (stderr, "ERROR: could not load %s\n", file_name);
        exit(1);
    }
    // Non-power-of-2 dimensions check
    if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) {
        fprintf(stderr, "WARNING: image %s is not power-of-2 dimensions\n", file_name);
    }

    // copy image data into 'target' side of cube map
    glTexImage2D(side_target, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);

    free(image_data);
}

ngl_skybox *ngl_skybox_new(const char *front, const char *back, const char *top, const char *bottom, const char *left, const char *right) {
    ngl_skybox *skybox = calloc(1, sizeof(ngl_skybox));

    float points[] = {
      -10.0f,  10.0f, -10.0f,
      -10.0f, -10.0f, -10.0f,
       10.0f, -10.0f, -10.0f,
       10.0f, -10.0f, -10.0f,
       10.0f,  10.0f, -10.0f,
      -10.0f,  10.0f, -10.0f,

      -10.0f, -10.0f,  10.0f,
      -10.0f, -10.0f, -10.0f,
      -10.0f,  10.0f, -10.0f,
      -10.0f,  10.0f, -10.0f,
      -10.0f,  10.0f,  10.0f,
      -10.0f, -10.0f,  10.0f,

       10.0f, -10.0f, -10.0f,
       10.0f, -10.0f,  10.0f,
       10.0f,  10.0f,  10.0f,
       10.0f,  10.0f,  10.0f,
       10.0f,  10.0f, -10.0f,
       10.0f, -10.0f, -10.0f,

      -10.0f, -10.0f,  10.0f,
      -10.0f,  10.0f,  10.0f,
       10.0f,  10.0f,  10.0f,
       10.0f,  10.0f,  10.0f,
       10.0f, -10.0f,  10.0f,
      -10.0f, -10.0f,  10.0f,

      -10.0f,  10.0f, -10.0f,
       10.0f,  10.0f, -10.0f,
       10.0f,  10.0f,  10.0f,
       10.0f,  10.0f,  10.0f,
      -10.0f,  10.0f,  10.0f,
      -10.0f,  10.0f, -10.0f,

      -10.0f, -10.0f, -10.0f,
      -10.0f, -10.0f,  10.0f,
       10.0f, -10.0f, -10.0f,
       10.0f, -10.0f, -10.0f,
      -10.0f, -10.0f,  10.0f,
       10.0f, -10.0f,  10.0f
    };
    glGenBuffers(1, &skybox->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, skybox->vbo);
    glBufferData(GL_ARRAY_BUFFER, 3 * 36 * sizeof (float), &points, GL_STATIC_DRAW);

    glGenVertexArrays(1, &skybox->vao);
    glBindVertexArray(skybox->vao);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, skybox->vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);

    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &skybox->texture);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, front);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_POSITIVE_Z, back);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, top);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, bottom);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_NEGATIVE_X, left);
    _ngl_skybox_load_side(skybox->texture, GL_TEXTURE_CUBE_MAP_POSITIVE_X, right);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    skybox->shader = ngl_shader_new_from_file(GL_TRIANGLES, "../shaders/skybox.vert", "../shaders/skybox.frag");

    return skybox;
}

void ngl_skybox_draw(ngl_skybox *skybox, ngl_camera *camera) {
    glDepthMask(GL_FALSE);
    NGL_CHECK_ERROR();
    glUseProgram(skybox->shader->program);
    NGL_CHECK_ERROR();
    glUniformMatrix4fv(skybox->shader->view_matrix_uniform, 1, GL_FALSE, (GLfloat *)&camera->view.m);
    NGL_CHECK_ERROR();
    glUniformMatrix4fv(skybox->shader->projection_matrix_uniform, 1, GL_FALSE, (GLfloat *)&camera->projection.m);
    NGL_CHECK_ERROR();
    glActiveTexture(GL_TEXTURE0);
    NGL_CHECK_ERROR();
    glBindTexture(GL_TEXTURE_CUBE_MAP, skybox->texture);
    NGL_CHECK_ERROR();
    glBindVertexArray(skybox->vao);
    NGL_CHECK_ERROR();
    glDrawArrays(GL_TRIANGLES, 0, 36);
    NGL_CHECK_ERROR();
    glDepthMask(GL_TRUE);
    NGL_CHECK_ERROR();
}

void ngl_skybox_free(ngl_skybox *skybox) {
    glDeleteBuffers(1, &skybox->vbo);
    glDeleteVertexArrays(1, &skybox->vao);
    glDeleteTextures(1, &skybox->texture);
    ngl_shader_free(skybox->shader);
    free(skybox);
}

// Model drawing /////////////////////////////////////////////////////////////

void ngl_draw_model(ngl_camera* camera, ngl_model* model, ngl_shader *shader) {
    mat4 view = camera->view;
    mat4 projection = camera->projection;
    mat4 mv = mat4_mul(&model->transform, &view);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    NGL_CHECK_ERROR();
    glEnable(GL_BLEND);
    NGL_CHECK_ERROR();
    glUseProgram(shader->program);
    NGL_CHECK_ERROR();
    glUniform1f(shader->time_uniform, nwm_get_time());
    NGL_CHECK_ERROR();
    glUniformMatrix4fv(shader->view_matrix_uniform, 1, GL_FALSE, (GLfloat *)&mv.m);
    NGL_CHECK_ERROR();
    glUniformMatrix4fv(shader->projection_matrix_uniform, 1, GL_FALSE, (GLfloat *)&projection.m);
    NGL_CHECK_ERROR();

    glBindVertexArray(model->vao);
    NGL_CHECK_ERROR();
    glDrawArrays(shader->draw_mode, 0, model->point_count);
    NGL_CHECK_ERROR();

    glBindVertexArray(0);
    NGL_CHECK_ERROR();
    glUseProgram(0);
    NGL_CHECK_ERROR();
}

void ngl_draw_background(ngl_camera *camera, ngl_model *model, ngl_shader *shader) {
    glDepthMask(GL_FALSE);
    NGL_CHECK_ERROR();
    ngl_draw_model(camera, model, shader);
    glDepthMask(GL_TRUE);
    NGL_CHECK_ERROR();
}

// Text drawing /////////////////////////////////////////////////////////////

#define NGL_FONT_BITMAP_WIDTH 1024
#define NGL_FONT_BITMAP_HEIGHT 1024

const char *_ngl_font_vertex_shader = "#version 400\n"
"layout (location = 0) in vec3 vp;\n"
"layout (location = 1) in vec3 vn;\n"
"layout (location = 2) in vec2 vt;\n"
"out vec3 color;\n"
"out vec2 texCoord;\n"
"uniform float uWidth;\n"
"uniform float uHeight;\n"
"uniform vec2 uPosition;\n"
"void main() {\n"
"    float x = (vp.x / uWidth) * 2 - 1;\n"
"    float y = (vp.y / uHeight) * -2 + 1;\n"
"    color = vec3(1.0, 1.0, 1.0);\n"
"    texCoord = vt;\n"
"    gl_Position = vec4(uPosition.x + x, uPosition.y + y, 0, 1.0);\n"
"}\n";

const char *_ngl_font_fragment_shader = "#version 400\n"
"in vec3 color;\n"
"in vec2 texCoord;\n"
"uniform sampler2D uTexture;\n"
"uniform float uAlpha;\n"
"layout (location = 0) out vec4 fragColor;\n"
"void main() {\n"
"    float v = texture(uTexture, texCoord).r;\n"
"    fragColor = vec4(1, 1, 1, v * uAlpha);\n"
"}\n";

ngl_font *ngl_font_new(const char *file_name, const int font_size) {
    ngl_font *font = calloc(1, sizeof(ngl_font));
    font->font_size = font_size;
    font->bitmap = calloc(NGL_FONT_BITMAP_WIDTH * NGL_FONT_BITMAP_HEIGHT, 1);
    font->first_char = 32;
    font->num_chars = 96;
    font->chars = calloc(font->num_chars, sizeof(stbtt_bakedchar));

    font->shader = ngl_shader_new(GL_TRIANGLES, _ngl_font_vertex_shader, _ngl_font_fragment_shader);
    glUseProgram(font->shader->program);
    NGL_CHECK_ERROR();
    font->position_uniform = glGetUniformLocation(font->shader->program, "uPosition");
    NGL_CHECK_ERROR();
    font->alpha_uniform = glGetUniformLocation(font->shader->program, "uAlpha");
    NGL_CHECK_ERROR();
    glUseProgram(0);
    NGL_CHECK_ERROR();

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, (GLint *) &viewport);
    int width = viewport[2];
    int height = viewport[3];
    ngl_shader_uniform_set_float(font->shader, "uWidth", width);
    ngl_shader_uniform_set_float(font->shader, "uHeight", height);

    FILE *fp = fopen(file_name, "rb");
    fseek(fp, 0L, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);
    font->buffer = calloc(file_size, 1);
    fread(font->buffer, file_size, 1, fp);
    fclose(fp);

    stbtt_BakeFontBitmap(font->buffer, 0, font_size,
        font->bitmap, NGL_FONT_BITMAP_WIDTH, NGL_FONT_BITMAP_HEIGHT,
        font->first_char, font->num_chars, font->chars);

    font->texture = ngl_texture_new(font->shader, "uTexture");
    glActiveTexture(GL_TEXTURE0);
    NGL_CHECK_ERROR();
    glBindTexture(GL_TEXTURE_2D, font->texture->texture_id);
    NGL_CHECK_ERROR();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, NGL_FONT_BITMAP_WIDTH, NGL_FONT_BITMAP_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, font->bitmap);
    NGL_CHECK_ERROR();

    return font;
}

void ngl_font_draw(ngl_font *font, const char *text, const double x, const double y, const double alpha) {
    assert(font != NULL);
    assert(text != NULL);

    int len = strlen(text);

    int point_count = len * 6; // Each glyph consists of two triangles.

    float *positions = calloc(point_count * 2, sizeof(float));
    float *uvs = calloc(point_count * 2, sizeof(float));

    float xpos = 0;
    float ypos = 0;
    int p = 0;
    int u = 0;

    for (int i = 0; i < len; i++) {
        stbtt_aligned_quad q;
        char c = text[i];
        int char_index = c - font->first_char;
        stbtt_GetBakedQuad(font->chars, NGL_FONT_BITMAP_WIDTH, NGL_FONT_BITMAP_HEIGHT,
            char_index, &xpos, &ypos, &q, 1);
        positions[p++] = q.x1; // 0
        positions[p++] = q.y1;
        positions[p++] = q.x0; // 1
        positions[p++] = q.y1;
        positions[p++] = q.x1; // 2
        positions[p++] = q.y0;
        positions[p++] = q.x1; // 2
        positions[p++] = q.y0;
        positions[p++] = q.x0; // 1
        positions[p++] = q.y1;
        positions[p++] = q.x0; // 3
        positions[p++] = q.y0;

        uvs[u++] = q.s1; // 0
        uvs[u++] = q.t1;
        uvs[u++] = q.s0; // 1
        uvs[u++] = q.t1;
        uvs[u++] = q.s1; // 2
        uvs[u++] = q.t0;
        uvs[u++] = q.s1; // 2
        uvs[u++] = q.t0;
        uvs[u++] = q.s0; // 1
        uvs[u++] = q.t1;
        uvs[u++] = q.s0; // 3
        uvs[u++] = q.t0;
    }

    for (int i = 0; i < p; i += 2) {
        positions[i] -= xpos / 2;
    }

    ngl_model *model = ngl_model_new(2, point_count, positions, NULL, uvs);
    free(positions);
    free(uvs);

    glDisable(GL_DEPTH_TEST);
    NGL_CHECK_ERROR();
    glUseProgram(font->shader->program);
    NGL_CHECK_ERROR();
    glUniform2f(font->position_uniform, x, -y);
    NGL_CHECK_ERROR();
    glUniform1f(font->alpha_uniform, alpha);
    NGL_CHECK_ERROR();
    glActiveTexture(GL_TEXTURE0);
    NGL_CHECK_ERROR();
    glBindTexture(GL_TEXTURE_2D, font->texture->texture_id);
    NGL_CHECK_ERROR();
    glBindVertexArray(model->vao);
    NGL_CHECK_ERROR();
    glDrawArrays(font->shader->draw_mode, 0, model->point_count);
    NGL_CHECK_ERROR();

    glBindVertexArray(0);
    NGL_CHECK_ERROR();
    glUseProgram(0);
    NGL_CHECK_ERROR();
    glEnable(GL_DEPTH_TEST);
    NGL_CHECK_ERROR();

    free(model);
}

void ngl_font_free(ngl_font *font) {
    free(font->buffer);
    free(font->bitmap);
    free(font->chars);
    free(font->shader);
    free(font);
}
