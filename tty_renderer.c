#include <fcntl.h>      // For open
#include <linux/fb.h>   // For FBIOGET_VSCREENINFO
#include <sys/ioctl.h>  // For ioctl
#include <sys/mman.h>   // For mmap
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>      // For error checking
#include <libevdev-1.0/libevdev/libevdev.h>  // For input handling
#include <dirent.h>     // For finding DRM render nodes
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "config.h"
#include "vectors.h"
#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include "tinyobj_loader_c.h"

#define PI 3.14159265
#define EPSILON 1e-6f

#define GL_CHECK(x) \
    x; \
    { \
        GLenum gl_err = glGetError(); \
        if (gl_err != GL_NO_ERROR) { \
            fprintf(stderr, "GL error %d at %s:%d\n", gl_err, __FILE__, __LINE__); \
        } \
    }

volatile sig_atomic_t done = 0;

// Function pointers for VAO extension
PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;

// Helper function to load extensions
void load_gl_extensions() {
    glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)
        eglGetProcAddress("glGenVertexArraysOES");
    glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)
        eglGetProcAddress("glBindVertexArrayOES");
    glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)
        eglGetProcAddress("glDeleteVertexArraysOES");
        
    // Verify extension availability
    if (!glGenVertexArraysOES || !glBindVertexArrayOES || !glDeleteVertexArraysOES) {
        fprintf(stderr, "VAO extensions not available\n");
        exit(1);
    }
}

// Structure to track key states (1 = pressed, 0 = released)
typedef struct {
    int w, a, s, d;
    int h, j, k, l;
    int q;
    int shift; // For moving down (y+)
    int space; // For moving up (y-)
} KeyState;


// Mesh structure
typedef struct {
    GLuint vao;
    GLuint vbo_positions;
    GLuint vbo_normals;
    GLuint vbo_texcoords;
    GLuint ebo;
    unsigned int num_indices;
    vec3 position;
    vec3 rotation;
    vec3 scale;
} Mesh;

// OpenGL/EGL structures
struct render_device {
    int fd;
    uint32_t width;
    uint32_t height;
    
    struct gbm_device *gbm_dev;
    
    EGLDisplay egl_display;
    EGLContext egl_context;
    
    char device_path[256];
    
    // Shader variables
    GLuint program;
    
    // Uniforms
    GLint u_mvp;
    GLint u_model;
    GLint u_view;
    GLint u_light_dir;
    GLint u_light_color;
    GLint u_camera_pos;
};

KeyState key_state = {0}; // Initialize all keys to not pressed
struct libevdev *input_dev = NULL;
int input_fd = -1;

// Vertex shader for 3D rendering
const char *vertex_shader_source =
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec2 a_texcoord;\n"
    "uniform mat4 u_mvp;\n"       // Model-view-projection
    "uniform mat4 u_model;\n"     // Model matrix
    "uniform mat4 u_view;\n"      // View matrix
    "varying vec3 v_normal;\n"
    "varying vec3 v_position;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "  v_normal = mat3(u_model) * a_normal;\n"  // Transform normal to world space
    "  v_position = (u_model * vec4(a_position, 1.0)).xyz;\n"  // Position in world space
    "  v_texcoord = a_texcoord;\n"
    "}\n";

// Fragment shader for basic lighting
const char *fragment_shader_source =
    "precision mediump float;\n"
    "varying vec3 v_normal;\n"
    "varying vec3 v_position;\n"
    "varying vec2 v_texcoord;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_light_color;\n"
    "uniform vec3 u_camera_pos;\n"
    "void main() {\n"
    "  vec3 normal = normalize(v_normal);\n"
    "  vec3 view_dir = normalize(u_camera_pos - v_position);\n"
    "  float diffuse = max(dot(normal, normalize(u_light_dir)), 0.0);\n"
    "  vec3 reflect_dir = reflect(-normalize(u_light_dir), normal);\n"
    "  float specular = pow(max(dot(view_dir, reflect_dir), 0.0), 32.0) * 0.5;\n"
    "  vec3 ambient = vec3(0.1, 0.1, 0.1);\n"
    "  //vec3 result = (ambient + diffuse * u_light_color + specular * u_light_color) * vec3(0.8, 0.8, 0.8);\n"
    "vec3 result = vec3(1.0, 1.0, 0.0);\n"
    "  gl_FragColor = vec4(result, 1.0);\n"
    "}\n";

const char *debug_fragment_shader_source =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);\n" // Bright yellow
    "}\n";

void term(int signum) {
    done = 1;
}

void cleanup_input() {
    if (input_dev) {
        libevdev_free(input_dev);
    }
    if (input_fd >= 0) {
        close(input_fd);
    }
}

int setup_input(const char *device_path) {
    // Open the input device
    input_fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (input_fd < 0) {
        fprintf(stderr, "Error opening input device '%s': %s\n", 
                device_path, strerror(errno));
        return 0;
    }
    
    // Create libevdev device
    int rc = libevdev_new_from_fd(input_fd, &input_dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to initialize libevdev: %s\n", strerror(-rc));
        close(input_fd);
        input_fd = -1;
        return 0;
    }
    
    printf("Input device name: \"%s\"\n", libevdev_get_name(input_dev));
    printf("Ready for input...\n");
    
    // Register cleanup function
    atexit(cleanup_input);
    return 1;
}

void process_input_events() {
    struct input_event ev;
    int rc;
    
    do {
        rc = libevdev_next_event(input_dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        
        if (rc == 0 && ev.type == EV_KEY) {
            int pressed = ev.value != 0; // 1 for press, 2 for repeat, 0 for release
            
            // Map key codes to our key states
            switch (ev.code) {
                case KEY_W: key_state.w = pressed; break;
                case KEY_A: key_state.a = pressed; break;
                case KEY_S: key_state.s = pressed; break;
                case KEY_D: key_state.d = pressed; break;
                case KEY_H: key_state.h = pressed; break;
                case KEY_J: key_state.j = pressed; break;
                case KEY_K: key_state.k = pressed; break;
                case KEY_L: key_state.l = pressed; break;
                case KEY_Q: key_state.q = pressed; break;
                case KEY_SPACE: key_state.space = pressed; break;
                
                // Track shift key (either left or right shift)
                case KEY_LEFTSHIFT:
                case KEY_RIGHTSHIFT: 
                    key_state.shift = pressed;
                    break;
            }
        }
    } while (rc == 1 || rc == 0); // Continue as long as there are events
}

// Compile shader
GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar info_log[512];
        glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "Shader compilation error: %s\n", info_log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// Link shader program
GLuint create_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar info_log[512];
        glGetProgramInfoLog(program, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "Program linking error: %s\n", info_log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

// Find an available DRM render node
static int find_drm_render_node(struct render_device *dev) {
    DIR *dir;
    struct dirent *entry;
    int found = 0;
    
    dir = opendir("/dev/dri");
    if (!dir) {
        fprintf(stderr, "Failed to open /dev/dri directory: %s\n", strerror(errno));
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        // Look for render nodes (renderD*)
        if (strncmp(entry->d_name, "renderD", 7) == 0) {
            snprintf(dev->device_path, sizeof(dev->device_path), "/dev/dri/%s", entry->d_name);
            dev->fd = open(dev->device_path, O_RDWR);
            if (dev->fd >= 0) {
                found = 1;
                printf("Using DRM render node: %s\n", dev->device_path);
                break;
            } else {
                fprintf(stderr, "Failed to open %s: %s\n", dev->device_path, strerror(errno));
            }
        }
    }
    
    closedir(dir);
    
    if (!found) {
        fprintf(stderr, "No available DRM render nodes found in /dev/dri/\n");
        return -1;
    }
    
    return 0;
}

static int init_egl_surfaceless(struct render_device *dev, int width, int height) {
    // Set render size
    dev->width = width;
    dev->height = height;

    // Create GBM device
    dev->gbm_dev = gbm_create_device(dev->fd);
    if (!dev->gbm_dev) {
        fprintf(stderr, "Failed to create GBM device: %s\n", strerror(errno));
        return -1;
    }

    // Get EGL display
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display = 
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_platform_display) {
        fprintf(stderr, "Failed to get eglGetPlatformDisplayEXT function\n");
        gbm_device_destroy(dev->gbm_dev);
        return -1;
    }

    dev->egl_display = get_platform_display(EGL_PLATFORM_GBM_KHR, dev->gbm_dev, NULL);
    if (dev->egl_display == EGL_NO_DISPLAY) {
        // Try the old method if the platform method fails
        fprintf(stderr, "Failed to get EGL display via platform extension, trying default method\n");
        dev->egl_display = eglGetDisplay((EGLNativeDisplayType)dev->gbm_dev);
        if (dev->egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "Failed to get EGL display: %04x\n", eglGetError());
            gbm_device_destroy(dev->gbm_dev);
            return -1;
        }
    }

    // Initialize EGL
    if (!eglInitialize(dev->egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL: %04x\n", eglGetError());
        gbm_device_destroy(dev->gbm_dev);
        return -1;
    }

    // Print EGL information
    printf("EGL Version: %s\n", eglQueryString(dev->egl_display, EGL_VERSION));
    printf("EGL Vendor: %s\n", eglQueryString(dev->egl_display, EGL_VENDOR));
    printf("EGL Extensions: %s\n", eglQueryString(dev->egl_display, EGL_EXTENSIONS));

    // Configure EGL
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        fprintf(stderr, "Failed to bind OpenGL ES API: %04x\n", eglGetError());
        eglTerminate(dev->egl_display);
        gbm_device_destroy(dev->gbm_dev);
        return -1;
    }

    // Choose EGL config for surfaceless rendering
    const EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,  // Add depth buffer for 3D rendering
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint num_configs;
    
    if (!eglChooseConfig(dev->egl_display, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
        fprintf(stderr, "Failed to choose EGL config: %04x\n", eglGetError());
        fprintf(stderr, "Trying with minimal configuration...\n");
        
        // Try with minimal requirements
        const EGLint minimal_attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        
        if (!eglChooseConfig(dev->egl_display, minimal_attribs, &config, 1, &num_configs) || num_configs == 0) {
            fprintf(stderr, "Still failed to choose EGL config: %04x\n", eglGetError());
            eglTerminate(dev->egl_display);
            gbm_device_destroy(dev->gbm_dev);
            return -1;
        }
    }

    printf("Found compatible EGL config\n");

    // Create EGL context (no surface needed with surfaceless context)
    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    dev->egl_context = eglCreateContext(
        dev->egl_display,
        config,
        EGL_NO_CONTEXT,
        context_attribs
    );
    if (dev->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context: %04x\n", eglGetError());
        eglTerminate(dev->egl_display);
        gbm_device_destroy(dev->gbm_dev);
        return -1;
    }

    // Make context current without a surface
    if (!eglMakeCurrent(dev->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, dev->egl_context)) {
        fprintf(stderr, "Failed to make EGL context current: %04x\n", eglGetError());
        eglDestroyContext(dev->egl_display, dev->egl_context);
        eglTerminate(dev->egl_display);
        gbm_device_destroy(dev->gbm_dev);
        return -1;
    }

    return 0;
}

static void cleanup_egl(struct render_device *dev) {
    // Clean up EGL
    eglMakeCurrent(dev->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dev->egl_display, dev->egl_context);
    eglTerminate(dev->egl_display);

    // Clean up GBM
    gbm_device_destroy(dev->gbm_dev);

    // Close DRM FD
    close(dev->fd);
}

// Set up shader program for 3D rendering
int setup_3d_rendering(struct render_device *dev) {
    // Load GL extensions
    load_gl_extensions();
    
    // Compile shaders
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    if (!vertex_shader) {
        return -1;
    }

    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, debug_fragment_shader_source);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return -1;
    }

    // Create shader program
    dev->program = create_program(vertex_shader, fragment_shader);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (!dev->program) {
        return -1;
    }

    // Get uniform locations
    dev->u_mvp = glGetUniformLocation(dev->program, "u_mvp");
    dev->u_model = glGetUniformLocation(dev->program, "u_model");
    dev->u_view = glGetUniformLocation(dev->program, "u_view");
    dev->u_light_dir = glGetUniformLocation(dev->program, "u_light_dir");
    dev->u_light_color = glGetUniformLocation(dev->program, "u_light_color");
    dev->u_camera_pos = glGetUniformLocation(dev->program, "u_camera_pos");
    
    // Enable depth testing for 3D rendering
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    glDisable(GL_CULL_FACE);
    
    // Set default viewport
    glViewport(0, 0, dev->width, dev->height);
    
    return 0;
}

// Load an .obj model
Mesh* load_obj_model(const char* filename) {
    // Check if file exists first
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", 
                filename, strerror(errno));
        return NULL;
    }
    fclose(file);
    
    printf("Loading OBJ file: %s\n", filename);
    
    // Check if OES extension functions are initialized
    if (!glGenVertexArraysOES || !glBindVertexArrayOES || !glDeleteVertexArraysOES) {
        fprintf(stderr, "Error: VAO extension functions not initialized!\n");
        return NULL;
    }
    
    // Create empty mesh
    Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
    if (!mesh) {
        fprintf(stderr, "Failed to allocate mesh memory\n");
        return NULL;
    }
    
    // Set default position, rotation, scale
    mesh->position = (vec3){0.0f, 0.0f, 0.0f};
    mesh->rotation = (vec3){0.0f, 0.0f, 0.0f};
    mesh->scale = (vec3){3.0f, 3.0f, 3.0f};

    
    // Initialize tinyobj structures
    tinyobj_attrib_t attrib;
    tinyobj_shape_t* shapes = NULL;
    size_t num_shapes = 0;
    tinyobj_material_t* materials = NULL;
    size_t num_materials = 0;
    
    memset(&attrib, 0, sizeof(attrib));
    
    // Set up file content buffer for safe parsing
    unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
    
    // Set up error reporting
    fprintf(stdout, "Starting OBJ parsing...\n");
    
    int ret = tinyobj_parse_obj(&attrib, &shapes, &num_shapes, &materials,
                         &num_materials, filename, NULL, NULL, flags);
                         
    fprintf(stdout, "Parsing completed with result: %d\n", ret);
    
    if (ret != TINYOBJ_SUCCESS) {
        fprintf(stderr, "Failed to load OBJ file: %s (error code %d)\n", filename, ret);
        free(mesh);
        return NULL;
    }
    
    // Verify that we have some data
    if (attrib.num_vertices == 0 || attrib.num_faces == 0) {
        fprintf(stderr, "Invalid OBJ file: No vertices or faces found\n");
        tinyobj_attrib_free(&attrib);
        tinyobj_shapes_free(shapes, num_shapes);
        tinyobj_materials_free(materials, num_materials);
        free(mesh);
        return NULL;
    }
    
    printf("  Vertices: %d\n", (int)attrib.num_vertices);
    printf("  Normals: %d\n", (int)attrib.num_normals);
    printf("  Texcoords: %d\n", (int)attrib.num_texcoords);
    printf("  Faces: %d\n", (int)attrib.num_faces);
    printf("  Shapes: %d\n", (int)num_shapes);
    
    // Create VAO
    glGenVertexArraysOES(1, &mesh->vao);
    glBindVertexArrayOES(mesh->vao);
    
    // Create position VBO
    glGenBuffers(1, &mesh->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, attrib.num_vertices * 3 * sizeof(float),
                attrib.vertices, GL_STATIC_DRAW);
    
    // Set up position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    
    // Create normal VBO if available
    if (attrib.num_normals > 0) {
        glGenBuffers(1, &mesh->vbo_normals);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
        glBufferData(GL_ARRAY_BUFFER, attrib.num_normals * 3 * sizeof(float),
                    attrib.normals, GL_STATIC_DRAW);
        
        // Set up normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(1);
    } else {
        // If no normals, generate simple normals (all pointing out)
        float* normals = (float*)malloc(attrib.num_vertices * 3 * sizeof(float));
        for (size_t i = 0; i < attrib.num_vertices * 3; i += 3) {
            normals[i] = 0.0f;
            normals[i+1] = 1.0f;  // All pointing up as a fallback
            normals[i+2] = 0.0f;
        }
        
        glGenBuffers(1, &mesh->vbo_normals);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
        glBufferData(GL_ARRAY_BUFFER, attrib.num_vertices * 3 * sizeof(float),
                   normals, GL_STATIC_DRAW);
        
        // Set up normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(1);
        
        free(normals);
    }
    
    // Create texcoord VBO if available
    if (attrib.num_texcoords > 0) {
        glGenBuffers(1, &mesh->vbo_texcoords);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_texcoords);
        glBufferData(GL_ARRAY_BUFFER, attrib.num_texcoords * 2 * sizeof(float),
                    attrib.texcoords, GL_STATIC_DRAW);
        
        // Set up texcoord attribute
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(2);
    } else {
        mesh->vbo_texcoords = 0;
    }
    
    // Create element buffer for indices
    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    
    // Use face indices - construct an index buffer from the face data
    unsigned int* indices = (unsigned int*)malloc(attrib.num_faces * sizeof(unsigned int));
    if (!indices) {
        fprintf(stderr, "Failed to allocate index buffer\n");
        glDeleteVertexArraysOES(1, &mesh->vao);
        glDeleteBuffers(1, &mesh->vbo_positions);
        if (mesh->vbo_normals) glDeleteBuffers(1, &mesh->vbo_normals);
        if (mesh->vbo_texcoords) glDeleteBuffers(1, &mesh->vbo_texcoords);
        free(mesh);
        tinyobj_attrib_free(&attrib);
        tinyobj_shapes_free(shapes, num_shapes);
        tinyobj_materials_free(materials, num_materials);
        return NULL;
    }
    
    // Use vertex indices directly
    for (size_t i = 0; i < attrib.num_faces; i++) {
        indices[i] = attrib.faces[i].v_idx;
    }
    
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, attrib.num_faces * sizeof(unsigned int),
                indices, GL_STATIC_DRAW);
    
    mesh->num_indices = attrib.num_faces;
    
    free(indices);
    
    // Clean up tinyobj data
    tinyobj_attrib_free(&attrib);
    tinyobj_shapes_free(shapes, num_shapes);
    tinyobj_materials_free(materials, num_materials);
    
    // Unbind VAO
    glBindVertexArrayOES(0);
    
    return mesh;
}

// testing
// Add this function to create a simple cube
Mesh* create_debug_cube() {
    float vertices[] = {
        // Front face
        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,
        // Back face
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f
    };
    
    float normals[] = {
        // Front face normals
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        // Back face normals
        0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, -1.0f,
        0.0f, 0.0f, -1.0f
    };
    
    unsigned int indices[] = {
        // Front face
        0, 1, 2,
        2, 3, 0,
        // Right face
        1, 5, 6,
        6, 2, 1,
        // Back face
        5, 4, 7,
        7, 6, 5,
        // Left face
        4, 0, 3,
        3, 7, 4,
        // Top face
        3, 2, 6,
        6, 7, 3,
        // Bottom face
        4, 5, 1,
        1, 0, 4
    };
    
    Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
    if (!mesh) return NULL;
    
    mesh->position = (vec3){0.0f, 0.0f, 0.0f};
    mesh->rotation = (vec3){0.0f, 0.0f, 0.0f};
    mesh->scale = (vec3){3.0f, 3.0f, 3.0f};
    
    glGenVertexArraysOES(1, &mesh->vao);
    glBindVertexArrayOES(mesh->vao);
    
    glGenBuffers(1, &mesh->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    
    glGenBuffers(1, &mesh->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER, sizeof(normals), normals, GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    
    mesh->vbo_texcoords = 0;
    
    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    mesh->num_indices = sizeof(indices) / sizeof(unsigned int);
    
    glBindVertexArrayOES(0);
    
    return mesh;
}
Mesh* load_simple_obj(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", 
                filename, strerror(errno));
        return NULL;
    }
    
    printf("Loading simple OBJ file: %s\n", filename);
    
    // Count vertices and faces
    char line[1024];
    int vertex_count = 0;
    int face_count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == 'v' && line[1] == ' ') {
            vertex_count++;
        } else if (line[0] == 'f' && line[1] == ' ') {
            face_count++;
        }
    }
    
    rewind(file);
    
    // Allocate arrays
    float* vertices = (float*)malloc(vertex_count * 3 * sizeof(float));
    unsigned int* indices = (unsigned int*)malloc(face_count * 3 * sizeof(unsigned int));
    float* normals = (float*)malloc(vertex_count * 3 * sizeof(float));
    
    // Read vertices and faces
    int v_idx = 0;
    int f_idx = 0;
    
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == 'v' && line[1] == ' ') {
            sscanf(line, "v %f %f %f", &vertices[v_idx*3], &vertices[v_idx*3+1], &vertices[v_idx*3+2]);
            
            // Generate simple normal (point outward)
            float len = sqrtf(vertices[v_idx*3]*vertices[v_idx*3] + 
                             vertices[v_idx*3+1]*vertices[v_idx*3+1] + 
                             vertices[v_idx*3+2]*vertices[v_idx*3+2]);
            if (len > 0.0001f) {
                normals[v_idx*3] = vertices[v_idx*3] / len;
                normals[v_idx*3+1] = vertices[v_idx*3+1] / len;
                normals[v_idx*3+2] = vertices[v_idx*3+2] / len;
            } else {
                normals[v_idx*3] = 0.0f;
                normals[v_idx*3+1] = 1.0f;
                normals[v_idx*3+2] = 0.0f;
            }
            
            v_idx++;
        } 
        else if (line[0] == 'f' && line[1] == ' ') {
            int v1, v2, v3;
            // OBJ indices start at 1, so subtract 1 for 0-based arrays
            sscanf(line, "f %d %d %d", &v1, &v2, &v3);
            indices[f_idx*3] = v1-1;
            indices[f_idx*3+1] = v2-1;
            indices[f_idx*3+2] = v3-1;
            f_idx++;
        }
    }
    
    fclose(file);
    
    // Create mesh
    Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
    if (!mesh) {
        free(vertices);
        free(indices);
        free(normals);
        return NULL;
    }
    
    // Set default position, rotation, scale
    mesh->position = (vec3){0.0f, 0.0f, 0.0f};
    mesh->rotation = (vec3){0.0f, 0.0f, 0.0f};
    mesh->scale = (vec3){3.0f, 3.0f, 3.0f};
    
    // Create VAO
    glGenVertexArraysOES(1, &mesh->vao);
    glBindVertexArrayOES(mesh->vao);
    
    // Create position VBO
    glGenBuffers(1, &mesh->vbo_positions);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_positions);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 3 * sizeof(float), vertices, GL_STATIC_DRAW);
    
    // Set up position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    
    // Create normal VBO
    glGenBuffers(1, &mesh->vbo_normals);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_normals);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 3 * sizeof(float), normals, GL_STATIC_DRAW);
    
    // Set up normal attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    
    // No texcoords in simple implementation
    mesh->vbo_texcoords = 0;
    
    // Create element buffer
    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, face_count * 3 * sizeof(unsigned int), indices, GL_STATIC_DRAW);
    
    mesh->num_indices = face_count * 3;
    
    // Clean up
    free(vertices);
    free(indices);
    free(normals);
    
    // Unbind VAO
    glBindVertexArrayOES(0);
    
    printf("  Vertices: %d\n", vertex_count);
    printf("  Faces: %d\n", face_count);
    
    return mesh;
}


// Free mesh resources
void free_mesh(Mesh* mesh) {
    if (!mesh) return;
    
    glDeleteVertexArraysOES(1, &mesh->vao);
    glDeleteBuffers(1, &mesh->vbo_positions);
    if (mesh->vbo_normals) glDeleteBuffers(1, &mesh->vbo_normals);
    if (mesh->vbo_texcoords) glDeleteBuffers(1, &mesh->vbo_texcoords);
    glDeleteBuffers(1, &mesh->ebo);
    
    free(mesh);
}

// Copy rendered pixels to framebuffer with proper format conversion
void copy_to_framebuffer(unsigned char *pixels, int width, int height, struct fb_var_screeninfo vinfo, struct fb_fix_screeninfo finfo, char *fbp) {
    int fb_width = vinfo.xres;
    int fb_height = vinfo.yres;
    int bpp = vinfo.bits_per_pixel;
    
    // Calculate how much of the image to draw (don't exceed framebuffer dimensions)
    int draw_width = (width < fb_width) ? width : fb_width;
    int draw_height = (height < fb_height) ? height : fb_height;
    
    if (bpp == 32) { // 32 bits per pixel (RGBA or ARGB)
        for (int y = 0; y < draw_height; y++) {
            for (int x = 0; x < draw_width; x++) {
                int fb_offset = (y * finfo.line_length) + (x * (bpp / 8));
                int pixels_offset = ((height - 1 - y) * width + x) * 4; // Flip vertically
                
                // Map RGBA to framebuffer format
                *((uint8_t*)(fbp + fb_offset + vinfo.red.offset/8)) = pixels[pixels_offset];
                *((uint8_t*)(fbp + fb_offset + vinfo.green.offset/8)) = pixels[pixels_offset + 1];
                *((uint8_t*)(fbp + fb_offset + vinfo.blue.offset/8)) = pixels[pixels_offset + 2];
                
                if (vinfo.transp.length > 0) {
                    *((uint8_t*)(fbp + fb_offset + vinfo.transp.offset/8)) = pixels[pixels_offset + 3];
                }
            }
        }
    } else if (bpp == 24) { // 24 bits per pixel (RGB)
        for (int y = 0; y < draw_height; y++) {
            for (int x = 0; x < draw_width; x++) {
                int fb_offset = (y * finfo.line_length) + (x * (bpp / 8));
                int pixels_offset = ((height - 1 - y) * width + x) * 4; // Flip vertically
                
                // Map RGB to framebuffer
                *((uint8_t*)(fbp + fb_offset + vinfo.red.offset/8)) = pixels[pixels_offset];
                *((uint8_t*)(fbp + fb_offset + vinfo.green.offset/8)) = pixels[pixels_offset + 1];
                *((uint8_t*)(fbp + fb_offset + vinfo.blue.offset/8)) = pixels[pixels_offset + 2];
            }
        }
    } else if (bpp == 16) { // 16 bits per pixel (RGB565 usually)
        for (int y = 0; y < draw_height; y++) {
            for (int x = 0; x < draw_width; x++) {
                int fb_offset = (y * finfo.line_length) + (x * (bpp / 8));
                int pixels_offset = ((height - 1 - y) * width + x) * 4; // Flip vertically
                
                // Convert RGB to RGB565 or other 16-bit format
                uint16_t color = 
                    ((pixels[pixels_offset] >> (8 - vinfo.red.length)) << vinfo.red.offset) |
                    ((pixels[pixels_offset + 1] >> (8 - vinfo.green.length)) << vinfo.green.offset) |
                    ((pixels[pixels_offset + 2] >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
                
                *((uint16_t*)(fbp + fb_offset)) = color;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <obj_file.obj> [input_device_path]\n", argv[0]);
        return 1;
    }

    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGINT, &action, NULL);

    // Open the framebuffer device
    int fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd == -1) {
        perror("Error opening framebuffer device");
        exit(1);
    }

    struct fb_var_screeninfo vinfo;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information");
        exit(1);
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        perror("Error reading fixed information");
        exit(1);
    }

    long screensize = vinfo.yres_virtual * finfo.line_length;
    char* fbp = (char*)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);

    if ((intptr_t)fbp == -1) {
        perror("Error mapping framebuffer to memory");
        exit(1);
    }

    // Check for input device argument
    const char *input_device = "/dev/input/event3"; // Default
    if (argc > 2) {
        input_device = argv[2];
    }
    
    // Initialize input
    if (!setup_input(input_device)) {
        fprintf(stderr, "Could not initialize input. Try providing your input device path as argument.\n");
        fprintf(stderr, "For example: %s %s /dev/input/event3\n", argv[0], argv[1]);
        fprintf(stderr, "Find your keyboard device with: cat /proc/bus/input/devices\n");
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    // Initialize GL rendering
    struct render_device gl_dev = {0};
    
    // Find and open a DRM render node
    if (find_drm_render_node(&gl_dev) < 0) {
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    // Initialize EGL for surfaceless rendering
    if (init_egl_surfaceless(&gl_dev, vinfo.xres, vinfo.yres) < 0) {
        close(gl_dev.fd);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    printf("Initialized surfaceless rendering context: %dx%d\n", gl_dev.width, gl_dev.height);

    // Setup 3D rendering
    if (setup_3d_rendering(&gl_dev) < 0) {
        cleanup_egl(&gl_dev);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    if (!glGenVertexArraysOES || !glBindVertexArrayOES || !glDeleteVertexArraysOES) {
        fprintf(stderr, "Error: OpenGL ES VAO extensions were not properly initialized!\n");
        cleanup_egl(&gl_dev);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    // Load OBJ model
    Mesh* mesh = create_debug_cube();
    if (!mesh) {
        fprintf(stderr, "Failed to load OBJ model: %s\n", argv[1]);
        cleanup_egl(&gl_dev);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    // Allocate pixels buffer for reading back the rendered image
    unsigned char *pixels = (unsigned char *)malloc(gl_dev.width * gl_dev.height * 4);
    if (!pixels) {
        fprintf(stderr, "Failed to allocate memory for pixels\n");
        free_mesh(mesh);
        cleanup_egl(&gl_dev);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }

    // Time for timing/animation
    float time = 0;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &start);
    unsigned int delta_us = 0;
    float delta = delta_us;

    // Camera position and rotation vars
    vec3 camera_position = (vec3) {0, 0, 3.0f};  // Start a bit back from the origin
    vec3 camera_rotation = (vec3) {0, 0, 0};
    float move_speed = 2.0f;
    float rotation_speed = 1.0f;

    printf("Rendering OBJ model: %s\n", argv[1]);
    printf("Controls: WASD = move, HJKL = rotate camera, SPACE = up, SHIFT = down, Q = quit\n");

    // Frame buffer to use with renderbuffers
    GLuint fbo, color_rb, depth_rb;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    // Create color renderbuffer
    glGenRenderbuffers(1, &color_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, gl_dev.width, gl_dev.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);
    
    // Create depth renderbuffer
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, gl_dev.width, gl_dev.height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
    
    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer not complete: %d\n", status);
        free(pixels);
        free_mesh(mesh);
        cleanup_egl(&gl_dev);
        munmap(fbp, screensize);
        close(fbfd);
        return 1;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    while (!done)
    {
        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
        time += delta;

        // Process input events
        process_input_events();
        
        // Check if quit key pressed
        if (key_state.q) { 
            done = 1;
            continue; 
        }
        
        // Calculate basis vectors based on current rotation
        vec3 forward = { -sin(camera_rotation.y), 0, -cos(camera_rotation.y) };
        vec3 right = { cos(camera_rotation.y), 0, -sin(camera_rotation.y) };
        forward = normalize_vec3(forward);
        right = normalize_vec3(right);
        vec3 up = {0, 1, 0};

        // Apply movement based on key states
        if (key_state.w) { camera_position = add_vec3(camera_position, scale_vec3(forward, move_speed * delta)); }
        if (key_state.s) { camera_position = subtract_vec3(camera_position, scale_vec3(forward, move_speed * delta)); }
        if (key_state.a) { camera_position = subtract_vec3(camera_position, scale_vec3(right, move_speed * delta)); }
        if (key_state.d) { camera_position = add_vec3(camera_position, scale_vec3(right, move_speed * delta)); }

        // Apply rotation based on key states
        if (key_state.k) { camera_rotation.x += rotation_speed * delta; }
        if (key_state.j) { camera_rotation.x -= rotation_speed * delta; }
        if (key_state.h) { camera_rotation.y -= rotation_speed * delta; }
        if (key_state.l) { camera_rotation.y += rotation_speed * delta; }

        // Clamp pitch
        camera_rotation.x = fmaxf(-PI/2.0f + EPSILON, fminf(PI/2.0f - EPSILON, camera_rotation.x));

        // Vertical movement (y-axis)
        if (key_state.space) { camera_position.y += move_speed * delta; } // Move up (y+)
        if (key_state.shift) { camera_position.y -= move_speed * delta; } // Move down (y-)

        // Compute view direction based on rotation
        vec3 view_dir = {
            sin(camera_rotation.y) * cos(camera_rotation.x),
            sin(camera_rotation.x),
            cos(camera_rotation.y) * cos(camera_rotation.x)
        };
        
        // Calculate look-at target
        vec3 target = add_vec3(camera_position, view_dir);
        
        // Create view matrix (camera transform)
        mat4 view_matrix = mat4_look_at(camera_position, target, up);
        
        // Create projection matrix
        float aspect_ratio = (float)gl_dev.width / (float)gl_dev.height;
        mat4 projection_matrix = mat4_perspective(45.0f * (PI / 180.0f), aspect_ratio, 0.1f, 100.0f);
        
        // Create model matrix for the mesh
        mat4 model_matrix = mat4_identity();
        model_matrix = mat4_translate(model_matrix, mesh->position);
        
        // Apply rotations in order: Y, X, Z
        mat4 rot_y = mat4_rotate_y(mesh->rotation.y + time * 0.5f); // Add animation
        mat4 rot_x = mat4_rotate_x(mesh->rotation.x);
        mat4 rot_z = mat4_rotate_z(mesh->rotation.z);
        
        model_matrix = mat4_multiply(model_matrix, rot_y);
        model_matrix = mat4_multiply(model_matrix, rot_x);
        model_matrix = mat4_multiply(model_matrix, rot_z);
        
        model_matrix = mat4_scale(model_matrix, mesh->scale);
        
        // Compute MVP matrix
        mat4 mvp = mat4_multiply(projection_matrix, view_matrix);
        mvp = mat4_multiply(mvp, model_matrix);
        
        // Clear framebuffer
        glClearColor(0.2f, 0.2f, 0.4f, 1.0f); // Dark blue instead of red
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Use shader program
        // Super minimal rendering test
glUseProgram(0); // Disable any existing program

// Create absolute minimal shaders
const char *min_vs = 
    "attribute vec4 position;\n"
    "void main() {\n"
    "  gl_Position = position;\n"
    "}\n";

const char *min_fs =
    "precision mediump float;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n" // Green
    "}\n";

// Compile with extensive error checking
GLuint vs = glCreateShader(GL_VERTEX_SHADER);
glShaderSource(vs, 1, &min_vs, NULL);
glCompileShader(vs);

GLint success;
GLchar infoLog[512];
glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
printf("Vertex shader compile status: %d\n", success);
if (!success) {
    glGetShaderInfoLog(vs, 512, NULL, infoLog);
    printf("VS error: %s\n", infoLog);
}

GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
glShaderSource(fs, 1, &min_fs, NULL);
glCompileShader(fs);
glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
printf("Fragment shader compile status: %d\n", success);
if (!success) {
    glGetShaderInfoLog(fs, 512, NULL, infoLog);
    printf("FS error: %s\n", infoLog);
}

GLuint program = glCreateProgram();
glAttachShader(program, vs);
glAttachShader(program, fs);
glLinkProgram(program);
glGetProgramiv(program, GL_LINK_STATUS, &success);
printf("Program link status: %d\n", success);
if (!success) {
    glGetProgramInfoLog(program, 512, NULL, infoLog);
    printf("Program error: %s\n", infoLog);
}

glUseProgram(program);

        
        // Set uniforms
        if (gl_dev.u_mvp != -1) {
            glUniformMatrix4fv(gl_dev.u_mvp, 1, GL_FALSE, mvp.m);
        }
        
        if (gl_dev.u_model != -1) {
            glUniformMatrix4fv(gl_dev.u_model, 1, GL_FALSE, model_matrix.m);
        }
        
        if (gl_dev.u_view != -1) {
            glUniformMatrix4fv(gl_dev.u_view, 1, GL_FALSE, view_matrix.m);
        }
        
        if (gl_dev.u_light_dir != -1) {
            // Light direction (normalized)
            vec3 light_dir = {1.0f, 1.0f, 1.0f};
            light_dir = normalize_vec3(light_dir);
            glUniform3f(gl_dev.u_light_dir, light_dir.x, light_dir.y, light_dir.z);
        }
        
        if (gl_dev.u_light_color != -1) {
            // Light color
            glUniform3f(gl_dev.u_light_color, 1.0f, 1.0f, 1.0f);
        }
        
        if (gl_dev.u_camera_pos != -1) {
            // Camera position for specular calculations
            glUniform3f(gl_dev.u_camera_pos, camera_position.x, camera_position.y, camera_position.z);
        }
        
// Clear previous errors
while(glGetError() != GL_NO_ERROR);

// Create minimal working example with proper VBO
float triangle[] = {
    0.0f, 0.8f, 0.0f,   // top
   -0.8f, -0.8f, 0.0f,  // bottom left
    0.8f, -0.8f, 0.0f   // bottom right
};

// Create and bind VBO
GLuint vbo;
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);

// Use position attribute from the shader
GLint posAttrib = glGetAttribLocation(program, "position");
printf("Position attribute location: %d\n", posAttrib);
glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, 0, 0);
glEnableVertexAttribArray(posAttrib);

// Draw with error checking
GLenum err = glGetError();
printf("Before draw GL error: %d\n", err);

glDrawArrays(GL_TRIANGLES, 0, 3);

err = glGetError();
printf("After draw GL error: %d\n", err);

// Make sure we read pixels after drawing
glReadPixels(0, 0, gl_dev.width, gl_dev.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

// Check a few pixel values directly
printf("Center pixel: R=%d G=%d B=%d A=%d\n", 
       pixels[(gl_dev.height/2 * gl_dev.width + gl_dev.width/2) * 4],
       pixels[(gl_dev.height/2 * gl_dev.width + gl_dev.width/2) * 4 + 1],
       pixels[(gl_dev.height/2 * gl_dev.width + gl_dev.width/2) * 4 + 2],
       pixels[(gl_dev.height/2 * gl_dev.width + gl_dev.width/2) * 4 + 3]);
        
        // Copy to framebuffer
        // For some reason, the fb doesn't update fast enough
        // unless we print something first
        printf("\r");
        fflush(stdout);

        copy_to_framebuffer(pixels, gl_dev.width, gl_dev.height, vinfo, finfo, fbp);
        
        // Frame timing
        clock_gettime(CLOCK_MONOTONIC_RAW, &end);
        delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
        delta = delta_us / 1000000.0f;
        
    }

    // Cleanup
    glDeleteRenderbuffers(1, &color_rb);
    glDeleteRenderbuffers(1, &depth_rb);
    glDeleteFramebuffers(1, &fbo);
    free(pixels);
    free_mesh(mesh);
    glDeleteProgram(gl_dev.program);
    cleanup_egl(&gl_dev);
    munmap(fbp, screensize);
    close(fbfd);

    return 0;
}

