#include "renderer.h"
#include "log.h"
#include "lua_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * GPU Thread — Double-Buffered Batch Recording (Phase 2)
 *
 * CPU thread calls renderer_draw() → records a GpuBatch into s_frame[write_idx].
 * At frame end, renderer_submit_frame() swaps write_idx and wakes GPU thread.
 * GPU thread drains s_frame[read_idx], executing all GL calls.
 * CPU runs next frame immediately while GPU renders previous frame.
 * ========================================================================= */

#define GPU_MAX_BATCHES        1024
/* An FMV frame arrives as dozens of narrow VRAM upload strips per frame, on
 * top of the 2 MB VRAM-viewer snapshot and any full-VRAM upload, so both the
 * update count and the staging pool have to carry a whole frame's worth or
 * the tail of every frame is dropped ("VRAM pool full — skipping rect"). */
#define GPU_MAX_VRAM_UPDATES   1024
#define GPU_VRAM_POOL_SIZE     (16 * 1024 * 1024)  /* 16 MB per slot */

typedef struct {
    uint32_t vertex_start;      /* index into s_pos/col/tex/tpg pools */
    uint32_t vertex_count;
    bool is_lines;              /* true → GL_LINES, false → GL_TRIANGLES */
    /* Renderer state snapshot */
    bool texture_enabled;
    bool raw_texture_enabled;
    bool semi_trans_enabled;
    bool dither_enabled;
    uint8_t semi_trans_mode;
    float screen_w, screen_h;
    int16_t offset_x, offset_y;
    int32_t tex_window[4];      /* and_x, and_y, or_x, or_y */
    int32_t scissor[4];         /* gl_x, gl_y, clip_w, clip_h */
} GpuBatch;

typedef struct {
    uint16_t x, y, w, h;
    uint16_t dst_x;             /* display_texture column (differs from x in 24bpp) */
    uint32_t data_offset;       /* byte offset into s_vram_pool[slot] */
    bool     update_display;    /* true → also update display_texture (R16UI→RGB) */
    bool     depth24;           /* true → rect holds packed 24bpp display data */
    bool     full_upload;       /* unused, kept for alignment */
    bool     is_viewer;         /* true → upload to vram_viewer_texture (RGBA8) */
} GpuVramUpdate;

/* Ops record VRAM updates and draw batches in the exact order they were
 * submitted by the CPU thread. A VRAM texture page can be uploaded to,
 * drawn from, then re-uploaded within the same frame (e.g. text glyphs
 * reusing a page previously holding a sprite) — executing all VRAM updates
 * before any draw would apply the *later* upload before the *earlier* draw
 * runs, corrupting whatever that draw was supposed to sample. */
typedef enum { GPU_OP_VRAM_UPDATE, GPU_OP_BATCH } GpuOpType;
typedef struct { GpuOpType type; uint32_t index; } GpuOp;
#define GPU_MAX_OPS (GPU_MAX_BATCHES + GPU_MAX_VRAM_UPDATES)

typedef struct {
    GpuBatch       batches[GPU_MAX_BATCHES];
    uint32_t       batch_count;
    GpuVramUpdate  vram_updates[GPU_MAX_VRAM_UPDATES];
    uint32_t       vram_update_count;
    GpuOp          ops[GPU_MAX_OPS];
    uint32_t       op_count;
    void*          imgui_draw_data;  /* ImDrawData* — valid until next NewFrame */
    uint16_t       disp_x, disp_y, disp_w, disp_h;  /* snapshot of CRTC display region */
} GpuFrame;

/* Record submission order for the GPU thread — see GpuOp comment above. */
static inline void gpu_frame_record_op(GpuFrame* frame, GpuOpType type, uint32_t index) {
    if (frame->op_count < GPU_MAX_OPS)
        frame->ops[frame->op_count++] = (GpuOp){ type, index };
}

/* Double-buffered vertex pools — in BSS (static), not on stack */
static RendererPosition s_pos[2][VERTEX_BUFFER_LEN];
static RendererColor    s_col[2][VERTEX_BUFFER_LEN];
static RendererTexCoord s_tex[2][VERTEX_BUFFER_LEN];
static RendererTPage    s_tpg[2][VERTEX_BUFFER_LEN];
static uint32_t         s_vtx[2];   /* vertex pool write position per slot */

/* Double-buffered VRAM copy pools */
static uint8_t   s_vram_pool[2][GPU_VRAM_POOL_SIZE];
static uint32_t  s_vram_pool_used[2];
static uint32_t  s_vram_pool_skips;      /* rects dropped because the pool was full */
static uint32_t  s_vram_pool_peak;       /* high-water mark of a single frame's usage */

/* Frame command lists */
static GpuFrame  s_frame[2];

// --- Helper: Check for OpenGL Errors ---
void check_gl_error(const char* location) {
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        const char* error_str;
        switch (error) {
            case GL_INVALID_ENUM: error_str = "INVALID_ENUM"; break;
            case GL_INVALID_VALUE: error_str = "INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: error_str = "INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW: error_str = "STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW: error_str = "STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY: error_str = "OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: error_str = "INVALID_FRAMEBUFFER_OPERATION"; break;
            default: error_str = "UNKNOWN_ERROR"; break;
        }
        LOG_RENDERER_ERROR("[RENDERER] OpenGL Error at %s: %s (0x%04x)", location, error_str, error);
    }
}


// --- GLSL Shader Source ---
// Based on Guide Section 5.3 and 5.4

// Vertex Shader: Transforms PSX VRAM coordinates and colors to OpenGL format.
const char* vertex_shader_source =
    "#version 330 core\n"
    // Input attributes from VBOs (locations match glVertexAttribIPointer setup)
    "layout (location = 0) in ivec2 vertex_position; // PSX VRAM coords (int16)\n"
    "layout (location = 1) in uvec3 vertex_color;    // PSX BGR color (uint8)\n"
    "layout (location = 2) in ivec2 vertex_texcoord; // PSX VRAM TexCoords (int16)\n"
    "layout (location = 3) in uvec2 vertex_tpage;    // CLUT (x) and TPage (y) (uint16)\n"
    "\n"
    // Uniform: A single value passed to the shader for a batch of vertices
    "uniform ivec2 offset; // Drawing offset (applied to vertex_position)\n"
    "uniform vec2 screen_scale; // Half-width/height used for coordinate conversion\n"
    "\n"
    // Output: Color passed to the fragment shader (interpolated)
    "out vec3 color;\n"
    "out vec2 tex_coord;\n"
    "flat out uvec2 tpage_info; // Pass TPage info to fragment shader (no interpolation)\n"
    "\n"
    "void main() {\n"
    // Apply the drawing offset
    "    ivec2 p = vertex_position + offset;\n"
    "\n"
    // Convert X coordinate from PSX VRAM (0..1023) to OpenGL NDC (-1.0..+1.0)
    // screen_scale is (width/2, height/2)
    // xpos = (p.x / (width/2)) - 1.0 = (2*p.x / width) - 1.0
    // If p.x = 0, xpos = -1.0. If p.x = width, xpos = 1.0.
    "    float xpos = (float(p.x) / screen_scale.x) - 1.0;\n"
    "\n"
    // Convert Y coordinate from PSX VRAM (0..511, top-to-bottom) to OpenGL NDC (-1.0..+1.0, bottom-to-top)
    // ypos = 1.0 - (p.y / (height/2)) = 1.0 - (2*p.y / height)
    // If p.y = 0, ypos = 1.0. If p.y = height, ypos = -1.0.
    "    float ypos = 1.0 - (float(p.y) / screen_scale.y); // Flip Y axis\n"
    "\n"
    // Set the final position for this vertex. Z=0 (2D), W=1 (position).
    "    gl_Position = vec4(xpos, ypos, 0.0, 1.0);\n"
    "\n"
    // Convert color from 8-bit BGR to 32-bit float RGB [0.0..1.0]
    "    color = vec3(float(vertex_color.r) / 255.0,\n"
    "                   float(vertex_color.g) / 255.0,\n"
    "                   float(vertex_color.b) / 255.0);\n"
    "\n"
    // Pass texture coordinates directly (0..255)
    "    tex_coord = vec2(float(vertex_texcoord.x), float(vertex_texcoord.y));\n"
    "    tpage_info = vertex_tpage;\n"
    "}\n";

// Fragment Shader: Determines the final color of each pixel fragment.
// Uses usampler2D for R16UI integer texture - preserves exact 16-bit PSX pixel values
const char* fragment_shader_source =
    "#version 330 core\n"
    // Input: Color interpolated from the vertex shader outputs
    "in vec3 color;\n"
    "in vec2 tex_coord;\n"
    "flat in uvec2 tpage_info; // x=CLUT, y=TPage\n"
    "\n"
    "uniform usampler2D vram_texture;\n"  // Integer sampler for R16UI
    "uniform int use_texture;\n"
    "uniform ivec4 u_texWindow; // (and_x, and_y, or_x, or_y) pre-computed masks\n"
    "uniform int raw_texture; // 1 = use texture color directly (no modulation)\n"
    "uniform int u_dither_enable; // 1 = apply PSX 4x4 dither before 15-bit quantization\n"
    "uniform int u_stp_mode;     // -1=off, 0=opaque pass (discard STP=1), 1=blend pass (discard STP=0)\n"
    "\n"
    // Output: Final color of the fragment (RGBA)
    "out vec4 frag_color;\n"
    "\n"
    // Helper: Convert PSX 1555 color to vec3
    "vec3 psx_to_rgb(uint raw) {\n"
    "    float r = float(raw & 0x1Fu) / 31.0;\n"
    "    float g = float((raw >> 5) & 0x1Fu) / 31.0;\n"
    "    float b = float((raw >> 10) & 0x1Fu) / 31.0;\n"
    "    return vec3(r, g, b);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 final_color = vec4(color, 1.0);\n"
    "    if (use_texture == 1) {\n"
    // "        frag_color = vec4(1.0, 0.0, 0.0, 1.0); return;\n" // DEBUG: Uncomment to verify geometry
    "        uint clut = tpage_info.x;\n"
    "        uint tpage = tpage_info.y;\n"
    "        uint depth = (tpage >> 7) & 3u;\n"
    "        uint page_x = (tpage & 0xFu) * 64u;\n"
    "        uint page_y = ((tpage >> 4) & 1u) * 256u;\n"
    "        uint clut_x = (clut & 0x3Fu) * 16u;\n"
    "        uint clut_y = (clut >> 6) & 0x1FFu;\n"
    "\n"
    "        // Apply Texture Window to UV coordinates (0-255)\n"
    "        uint u_raw = uint(tex_coord.x) & 0xFFu;\n"
    "        uint v_raw = uint(tex_coord.y) & 0xFFu;\n"
    "        uint u = (u_raw & uint(u_texWindow.x)) | uint(u_texWindow.z);\n"
    "        uint v = (v_raw & uint(u_texWindow.y)) | uint(u_texWindow.w);\n"
    "\n"
    "        vec3 tex_rgb = vec3(0.0);\n"
    "        uint raw_color = 0u;\n"
    "\n"
    "        if (depth == 0u) { // 4-bit paletted\n"
    "            uint tex_x = page_x + (u / 4u);\n"
    "            uint tex_y = page_y + v;\n"
    "            uint raw_word = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0).r;\n"
    "            uint shift = (u & 3u) * 4u;\n"
    "            uint index = (raw_word >> shift) & 0xFu;\n"
    "            uint clut_pos_x = clut_x + index;\n"
    "            raw_color = texelFetch(vram_texture, ivec2(clut_pos_x, clut_y), 0).r;\n"
    "            if (raw_color == 0u) discard;\n"
    "            if (u_stp_mode == 0 && (raw_color & 0x8000u) != 0u) discard;\n"  /* pass1: discard STP=1 */
    "            if (u_stp_mode == 1 && (raw_color & 0x8000u) == 0u) discard;\n"  /* pass2: discard STP=0 */
    "            tex_rgb = psx_to_rgb(raw_color);\n"
    "\n"
    "        } else if (depth == 1u) { // 8-bit paletted\n"
    "            uint tex_x = page_x + (u / 2u);\n"
    "            uint tex_y = page_y + v;\n"
    "            uint raw_word = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0).r;\n"
    "            uint shift = (u & 1u) * 8u;\n"
    "            uint index = (raw_word >> shift) & 0xFFu;\n"
    "            uint clut_pos_x = clut_x + index;\n"
    "            raw_color = texelFetch(vram_texture, ivec2(clut_pos_x, clut_y), 0).r;\n"
    "            if (raw_color == 0u) discard;\n"
    "            if (u_stp_mode == 0 && (raw_color & 0x8000u) != 0u) discard;\n"
    "            if (u_stp_mode == 1 && (raw_color & 0x8000u) == 0u) discard;\n"
    "            tex_rgb = psx_to_rgb(raw_color);\n"
    "\n"
    "        } else { // 15-bit direct color (depth == 2 or 3)\n"
    "            uint tex_x = page_x + u;\n"
    "            uint tex_y = page_y + v;\n"
    "            raw_color = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0).r;\n"
    "            if (raw_color == 0u) discard;\n"
    "            if (u_stp_mode == 0 && (raw_color & 0x8000u) != 0u) discard;\n"
    "            if (u_stp_mode == 1 && (raw_color & 0x8000u) == 0u) discard;\n"
    "            tex_rgb = psx_to_rgb(raw_color);\n"
    "        }\n"
    "\n"
    "        if (raw_texture == 1) {\n"
    "            final_color = vec4(tex_rgb, 1.0);\n"
    "        } else {\n"
    "            final_color = vec4(tex_rgb * color * 2.0, 1.0);\n"
    "        }\n"
    "    }\n"
    // PSX 4x4 dithering matrix (applied before 24-to-15bit quantization).
    // Per PSX-SPX: offsets added to 8-bit channel, result clamped [0,255], then >>3 to 5-bit.
    // Applied to: gouraud-shaded polygons, textured-blend polygons, all lines.
    // NOT applied to: mono polygons, raw-texture polygons, rectangles.
    "    if (u_dither_enable == 1) {\n"
    "        const int dither_table[16] = int[16](\n"
    "            -4,  0, -3,  1,\n"
    "             2, -2,  3, -1,\n"
    "            -3,  1, -4,  0,\n"
    "             3, -1,  2, -2\n"
    "        );\n"
    "        int dx = int(mod(gl_FragCoord.x, 4.0));\n"
    "        int dy = int(mod(gl_FragCoord.y, 4.0));\n"
    "        float doff = float(dither_table[dy * 4 + dx]) / 255.0;\n"
    "        // Clamp after adding dither offset, then quantize to 5-bit and normalize back\n"
    "        vec3 c_d = clamp(final_color.rgb + vec3(doff), 0.0, 1.0);\n"
    "        final_color.rgb = floor(c_d * 255.0 / 8.0) / 31.0;\n"
    "    }\n"
    "    frag_color = final_color;\n"
    "}\n";


// --- OpenGL Helper Functions ---

// Compiles a shader from source code.
// Based on Guide Section 5.5
static GLuint compile_shader(const char* source, GLenum shader_type) {
    GLuint shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char* log_buffer = (char*)malloc(log_len + 1);
        if (log_buffer) {
            glGetShaderInfoLog(shader, log_len, NULL, log_buffer);
            log_buffer[log_len] = '\0';
            LOG_RENDERER_ERROR("[RENDERER] Shader Compilation Error (%s):%s",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment",
                log_buffer);
            free(log_buffer);
        } else {
            LOG_RENDERER_ERROR("[RENDERER] Shader Compilation Error (%s) - Failed to allocate log buffer",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
        }
        glDeleteShader(shader); // Delete the failed shader object
        check_gl_error("compile_shader (error path)");
        return 0; // Return 0 on failure
    }
    LOG_RENDERER_DEBUG("[RENDERER] Shader compiled successfully (Type: %s)", (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
    check_gl_error("compile_shader (success path)");
    return shader;
}

// Links vertex and fragment shaders into a shader program.
// Based on Guide Section 5.5
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        char* log_buffer = (char*)malloc(log_len + 1);
        if (log_buffer) {
            glGetProgramInfoLog(program, log_len, NULL, log_buffer);
            log_buffer[log_len] = '\0';
            LOG_RENDERER_ERROR("[RENDERER] Shader Program Linking Error:%s", log_buffer);
            free(log_buffer);
        } else {
            LOG_RENDERER_ERROR("[RENDERER] Shader Program Linking Error - Failed to allocate log buffer");
        }
        glDeleteProgram(program); // Delete the failed program object
        // Shaders are still attached if linking failed, detach and delete them
        glDetachShader(program, vertex_shader);
        glDetachShader(program, fragment_shader);
        // Don't delete shaders here if they were passed in, caller might reuse
        check_gl_error("link_program (error path)");
        return 0; // Return 0 on failure
    }

    // Shaders can be detached and deleted after successful linking
    glDetachShader(program, vertex_shader);
    glDetachShader(program, fragment_shader);
    // Caller should delete the individual shaders if they are no longer needed
    // glDeleteShader(vertex_shader); // Optional: Delete here if not needed elsewhere
    // glDeleteShader(fragment_shader);

    LOG_RENDERER_DEBUG("[RENDERER] Shader program linked successfully (ID: %u)", program);
    check_gl_error("link_program (success path)");
    return program;
}


// --- Renderer Implementation ---

bool renderer_init(Renderer* renderer) {
    if (log_get_level() >= LOG_LEVEL_INFO) {
        LOG_RENDERER_DEBUG("[RENDERER] Initializing renderer");
    }
    LOG_RENDERER_DEBUG("[RENDERER] Initializing Renderer...");
    renderer->initialized = false;
    renderer->vertex_count = 0;
    // Clear CPU-side buffers initially (optional but good practice)
    memset(renderer->positions_data, 0, sizeof(renderer->positions_data));
    memset(renderer->colors_data, 0, sizeof(renderer->colors_data));
    if (renderer->screen_width <= 0.0f) {
        renderer->screen_width = 1024.0f;
    }
    if (renderer->screen_height <= 0.0f) {
        renderer->screen_height = 512.0f;
    }


    // Compile Shaders
    LOG_RENDERER_DEBUG("[RENDERER] Compiling vertex shader...");
    GLuint vs = compile_shader(vertex_shader_source, GL_VERTEX_SHADER);
    LOG_RENDERER_DEBUG("[RENDERER] Compiling fragment shader...");
    GLuint fs = compile_shader(fragment_shader_source, GL_FRAGMENT_SHADER);
    if (vs == 0 || fs == 0) {
        LOG_RENDERER_ERROR("[RENDERER] Renderer Init Failed: Shader compilation error.");
        if (vs != 0) glDeleteShader(vs); // Clean up if one succeeded
        if (fs != 0) glDeleteShader(fs);
        return false;
    }

    // Link Program
    LOG_RENDERER_DEBUG("[RENDERER] Linking shader program...");
    renderer->shader_program = link_program(vs, fs);
    // Delete individual shaders now that they are linked into the program
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (renderer->shader_program == 0) {
        LOG_RENDERER_ERROR("[RENDERER] Renderer Init Failed: Shader linking error.");
        return false;
    }
    check_gl_error("After linking program");


    // Get Uniform Location for the drawing offset
    renderer->uniform_offset_loc = glGetUniformLocation(renderer->shader_program, "offset");
    if (renderer->uniform_offset_loc < 0) {
        // This isn't fatal, but offset won't work. Check for GL errors too.
        LOG_RENDERER_WARN("[RENDERER] Could not find uniform 'offset'. Draw offset will not work.");
        check_gl_error("glGetUniformLocation offset"); // Check if there was an error other than not found
    } else {
        LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'offset' at location: %d", renderer->uniform_offset_loc);
        // Set initial offset to 0,0
        glUseProgram(renderer->shader_program); // Need to bind program to set uniform
        glUniform2i(renderer->uniform_offset_loc, 0, 0);
        glUseProgram(0); // Unbind program
    }
    check_gl_error("After getting/setting offset uniform");

    renderer->uniform_screen_scale_loc = glGetUniformLocation(renderer->shader_program, "screen_scale");
    if (renderer->uniform_screen_scale_loc < 0) {
        LOG_RENDERER_WARN("[RENDERER] Could not find uniform 'screen_scale'. Display scaling will be incorrect.");
    } else {
        LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'screen_scale' at location: %d", renderer->uniform_screen_scale_loc);
        glUseProgram(renderer->shader_program);
        glUniform2f(renderer->uniform_screen_scale_loc,
                    renderer->screen_width * 0.5f,
                    renderer->screen_height * 0.5f);
        glUseProgram(0);
    }


    // --- Create Vertex Array Object (VAO) ---
    // VAO stores the links between VBOs and shader attributes.
    // Based on Guide Section 5.6
    LOG_RENDERER_DEBUG("[RENDERER] Creating VAO...");
    glGenVertexArrays(1, &renderer->vao);
    glBindVertexArray(renderer->vao); // Bind the VAO to make it active
    LOG_RENDERER_DEBUG("[RENDERER] VAO created (ID: %u) and bound.", renderer->vao);
    check_gl_error("After creating/binding VAO");


    // --- Create and Configure Position Vertex Buffer Object (VBO) ---
    LOG_RENDERER_DEBUG("[RENDERER] Creating Position VBO...");
    glGenBuffers(1, &renderer->position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); // Bind the new buffer to the GL_ARRAY_BUFFER target
    LOG_RENDERER_DEBUG("[RENDERER] Position VBO created (ID: %u) and bound.", renderer->position_buffer);

    // Allocate buffer storage on the GPU. We'll upload data later using glBufferSubData.
    // GL_DYNAMIC_DRAW is a hint that the data will be modified frequently.
    glBufferData(GL_ARRAY_BUFFER,               // Target buffer type
                 VERTEX_BUFFER_LEN * sizeof(RendererPosition), // Total buffer size in bytes
                 NULL,                         // Initial data (none)
                 GL_DYNAMIC_DRAW);             // Usage hint
    LOG_RENDERER_DEBUG("[RENDERER] Position VBO allocated %lu bytes.", VERTEX_BUFFER_LEN * sizeof(RendererPosition));
    check_gl_error("After position VBO glBufferData");

    // --- Link Position VBO to Shader Attribute ---
    // Get the location of the 'vertex_position' attribute in the shader (should be 0 as per layout qualifier)
    GLint pos_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_position");
     if (pos_attrib_loc < 0) { LOG_RENDERER_WARN("[RENDERER] Could not find attribute 'vertex_position'."); }
     else { LOG_RENDERER_DEBUG("[RENDERER] Attribute 'vertex_position' found at location %d.", pos_attrib_loc); }

    // Enable this vertex attribute array
    glEnableVertexAttribArray(pos_attrib_loc); // Use the obtained location

    // Specify how OpenGL should interpret the data in the VBO for this attribute
    glVertexAttribIPointer(pos_attrib_loc,       // Attribute location in the shader
                           2,                  // Number of components per vertex (x, y)
                           GL_SHORT,           // Data type of each component (signed 16-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererPosition)? Set 0 for now.
                           (void*)0);          // Offset of the first component in the buffer
    LOG_RENDERER_DEBUG("[RENDERER] Position VBO linked to vertex shader attribute location %d.", pos_attrib_loc);
    check_gl_error("After setting position attribute pointer");


    // --- Create and Configure Color Vertex Buffer Object (VBO) ---
    LOG_RENDERER_DEBUG("[RENDERER] Creating Color VBO...");
    glGenBuffers(1, &renderer->color_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    LOG_RENDERER_DEBUG("[RENDERER] Color VBO created (ID: %u) and bound.", renderer->color_buffer);

    // Allocate storage
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererColor), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_DEBUG("[RENDERER] Color VBO allocated %lu bytes.", VERTEX_BUFFER_LEN * sizeof(RendererColor));
    check_gl_error("After color VBO glBufferData");

    // --- Link Color VBO to Shader Attribute ---
    GLint col_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_color");
     if (col_attrib_loc < 0) { LOG_RENDERER_WARN("[RENDERER] Could not find attribute 'vertex_color'."); }
     else { LOG_RENDERER_DEBUG("[RENDERER] Attribute 'vertex_color' found at location %d.", col_attrib_loc); }

    glEnableVertexAttribArray(col_attrib_loc);

    // Specify data format for the color attribute
    glVertexAttribIPointer(col_attrib_loc,       // Attribute location
                           3,                  // Number of components (r, g, b)
                           GL_UNSIGNED_BYTE,   // Data type (unsigned 8-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererColor)? Set 0 for now.
                           (void*)0);          // Offset
    LOG_RENDERER_DEBUG("[RENDERER] Color VBO linked to vertex shader attribute location %d.", col_attrib_loc);
    check_gl_error("After setting color attribute pointer");

    // --- Create and Configure Texture Coordinate VBO ---
    LOG_RENDERER_DEBUG("[RENDERER] Creating TexCoord VBO...");
    glGenBuffers(1, &renderer->texcoord_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererTexCoord), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_DEBUG("[RENDERER] TexCoord VBO created (ID: %u) and bound.", renderer->texcoord_buffer);

    GLint tex_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_texcoord");
    if (tex_attrib_loc >= 0) {
        glEnableVertexAttribArray(tex_attrib_loc);
        glVertexAttribIPointer(tex_attrib_loc, 2, GL_SHORT, 0, (void*)0);
        LOG_RENDERER_DEBUG("[RENDERER] Attribute 'vertex_texcoord' found at location %d.", tex_attrib_loc);
    } else {
        LOG_RENDERER_WARN("[RENDERER] Could not find attribute 'vertex_texcoord'.");
    }

    // --- Create and Configure TPage/CLUT VBO ---
    LOG_RENDERER_DEBUG("[RENDERER] Creating TPage VBO...");
    glGenBuffers(1, &renderer->tpage_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererTPage), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_DEBUG("[RENDERER] TPage VBO created (ID: %u) and bound.", renderer->tpage_buffer);

    GLint tpage_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_tpage");
    if (tpage_attrib_loc >= 0) {
        glEnableVertexAttribArray(tpage_attrib_loc);
        glVertexAttribIPointer(tpage_attrib_loc, 2, GL_UNSIGNED_SHORT, 0, (void*)0);
        LOG_RENDERER_DEBUG("[RENDERER] Attribute 'vertex_tpage' found at location %d.", tpage_attrib_loc);
    } else {
        LOG_RENDERER_WARN("[RENDERER] Could not find attribute 'vertex_tpage'.");
    }

    // --- Create VRAM Texture ---
    // Use R16UI (16-bit unsigned integer) to preserve raw PSX pixel values exactly
    glGenTextures(1, &renderer->vram_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Allocate texture storage (1024x512, 16-bit unsigned integer)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    LOG_RENDERER_DEBUG("[RENDERER] VRAM Texture created (ID: %u) as R16UI.", renderer->vram_texture);

    // --- Create Off-screen Display FBO (PCSX-Redux pattern) ---
    glGenFramebuffers(1, &renderer->display_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->display_fbo);

    glGenTextures(1, &renderer->display_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->display_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 512, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderer->display_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_RENDERER_ERROR("[RENDERER] Renderer Init Failed: Display FBO is not complete.");
        return false;
    }
    LOG_RENDERER_DEBUG("[RENDERER] Display FBO created successfully (FBO: %u, Texture: %u).", renderer->display_fbo, renderer->display_texture);
    // Note: We leave display_fbo bound so all PSX rendering goes here!

    // --- VRAM viewer texture (RGBA8, updated on CPU each frame) ---
    glGenTextures(1, &renderer->vram_viewer_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->vram_viewer_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    LOG_RENDERER_DEBUG("[RENDERER] VRAM viewer texture created (ID: %u).", renderer->vram_viewer_texture);

    renderer->uniform_use_texture_loc = glGetUniformLocation(renderer->shader_program, "use_texture");
    renderer->uniform_raw_texture_loc = glGetUniformLocation(renderer->shader_program, "raw_texture");
    renderer->uniform_vram_texture_loc = glGetUniformLocation(renderer->shader_program, "vram_texture");
    renderer->uniform_tex_window_loc = glGetUniformLocation(renderer->shader_program, "u_texWindow");
    renderer->uniform_dither_loc = glGetUniformLocation(renderer->shader_program, "u_dither_enable");

    LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'use_texture' at location: %d", renderer->uniform_use_texture_loc);
    LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'raw_texture' at location: %d", renderer->uniform_raw_texture_loc);
    LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'vram_texture' at location: %d", renderer->uniform_vram_texture_loc);
    LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'u_texWindow' at location: %d", renderer->uniform_tex_window_loc);
    LOG_RENDERER_DEBUG("[RENDERER] Found uniform 'u_dither_enable' at location: %d", renderer->uniform_dither_loc);

    // Default texture window: no masking (and_x=0xFF, and_y=0xFF, or_x=0, or_y=0)
    glUseProgram(renderer->shader_program);
    if (renderer->uniform_tex_window_loc >= 0) glUniform4i(renderer->uniform_tex_window_loc, 0xFF, 0xFF, 0, 0);
    renderer->uniform_stp_mode_loc = glGetUniformLocation(renderer->shader_program, "u_stp_mode");
    if (renderer->uniform_stp_mode_loc >= 0) glUniform1i(renderer->uniform_stp_mode_loc, -1);
    if (renderer->uniform_raw_texture_loc >= 0) glUniform1i(renderer->uniform_raw_texture_loc, 0);
    if (renderer->uniform_dither_loc >= 0) glUniform1i(renderer->uniform_dither_loc, 0);
    glUseProgram(0);

    // --- Unbind ---
    glBindVertexArray(0); // Unbind the VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind the VBO from the target
    LOG_RENDERER_DEBUG("[RENDERER] VAO and VBO unbound.");


    // --- Initial GL State ---
    // Set the default clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    check_gl_error("After glClearColor");

    // Enable scissor test with default bounds covering entire VRAM (1024×512)
    // This ensures clipping is enabled even if GPU drawing area setup is called before renderer init
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1024, 512);
    LOG_RENDERER_DEBUG("[RENDERER] GL_SCISSOR_TEST enabled with default bounds (0,0,1024,512)");
    check_gl_error("After scissor initialization");

    // Potentially enable depth testing if needed later
    // glEnable(GL_DEPTH_TEST);

    renderer->initialized = true;
    renderer->texture_enabled = false;
    renderer->raw_texture_enabled = false;
    LOG_RENDERER_DEBUG("[RENDERER] Renderer Initialized Successfully.");
    return true;
}

// Buffers a triangle's vertex data
void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3], RendererTexCoord tex[3], uint16_t clut, uint16_t tpage) {
    if (!renderer->initialized) {
        LOG_RENDERER_ERROR("[RENDERER] Renderer Error: push_triangle called before initialization.");
        return;
    }

    if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_DEBUG("[RENDERER] Renderer: Vertex buffer full (%u verts), forcing draw before push_triangle.", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
             LOG_RENDERER_ERROR("[RENDERER] Renderer Error: Cannot push triangle, buffer still full after draw.");
             return;
        }
    }

    // Copy data to CPU-side buffers
memcpy(&renderer->positions_data[renderer->vertex_count], pos, 3 * sizeof(RendererPosition));
    memcpy(&renderer->colors_data[renderer->vertex_count], col, 3 * sizeof(RendererColor));
    if (tex) {
        memcpy(&renderer->texcoords_data[renderer->vertex_count], tex, 3 * sizeof(RendererTexCoord));
        for(int i=0; i<3; ++i) {
            renderer->tpage_data[renderer->vertex_count + i].clut = clut;
            renderer->tpage_data[renderer->vertex_count + i].tpage = tpage;
        }
    } else {
        memset(&renderer->texcoords_data[renderer->vertex_count], 0, 3 * sizeof(RendererTexCoord));
        memset(&renderer->tpage_data[renderer->vertex_count], 0, 3 * sizeof(RendererTPage));
    }

    renderer->vertex_count += 3;
}

// Buffers a quad's vertex data (as two triangles)
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4], RendererTexCoord tex[4], uint16_t clut, uint16_t tpage) {
     if (!renderer->initialized) {
        LOG_RENDERER_ERROR("[RENDERER] Renderer Error: push_quad called before initialization.");
        return;
     }

     if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_DEBUG("[RENDERER] Renderer Info: Vertex buffer full (%u verts), forcing draw before push_quad.", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
            LOG_RENDERER_ERROR("[RENDERER] Renderer Error: Cannot push quad, buffer still full after draw.");
            return;
        }
     }

    LOG_RENDERER_DEBUG("[RENDERER] Renderer: Buffering Quad (Start Index: %u)", renderer->vertex_count);
    // Decompose quad into two triangles
    // PSX Quad vertex order: 0--1
    //                        |  |
    //                        2--3
    // Triangle 1: V0, V1, V2
    renderer->positions_data[renderer->vertex_count + 0] = pos[0];
    renderer->colors_data[renderer->vertex_count + 0]    = col[0];
    renderer->positions_data[renderer->vertex_count + 1] = pos[1];
    renderer->colors_data[renderer->vertex_count + 1]    = col[1];
    renderer->positions_data[renderer->vertex_count + 2] = pos[2];
    renderer->colors_data[renderer->vertex_count + 2]    = col[2];

    if (tex) {
        renderer->texcoords_data[renderer->vertex_count + 0] = tex[0];
        renderer->texcoords_data[renderer->vertex_count + 1] = tex[1];
        renderer->texcoords_data[renderer->vertex_count + 2] = tex[2];
        for(int i=0; i<3; ++i) {
            renderer->tpage_data[renderer->vertex_count + i].clut = clut;
            renderer->tpage_data[renderer->vertex_count + i].tpage = tpage;
        }
    } else {
        memset(&renderer->texcoords_data[renderer->vertex_count], 0, 3 * sizeof(RendererTexCoord));
        memset(&renderer->tpage_data[renderer->vertex_count], 0, 3 * sizeof(RendererTPage));
    }

    // Triangle 2: V1, V2, V3
    renderer->positions_data[renderer->vertex_count + 3] = pos[1]; // V1
    renderer->colors_data[renderer->vertex_count + 3]    = col[1]; // C1
    renderer->positions_data[renderer->vertex_count + 4] = pos[2]; // V2
    renderer->colors_data[renderer->vertex_count + 4]    = col[2]; // C2
    renderer->positions_data[renderer->vertex_count + 5] = pos[3]; // V3
    renderer->colors_data[renderer->vertex_count + 5]    = col[3]; // C3

    if (tex) {
        renderer->texcoords_data[renderer->vertex_count + 3] = tex[1];
        renderer->texcoords_data[renderer->vertex_count + 4] = tex[2];
        renderer->texcoords_data[renderer->vertex_count + 5] = tex[3];
        for(int i=3; i<6; ++i) {
            renderer->tpage_data[renderer->vertex_count + i].clut = clut;
            renderer->tpage_data[renderer->vertex_count + i].tpage = tpage;
        }
    } else {
        memset(&renderer->texcoords_data[renderer->vertex_count + 3], 0, 3 * sizeof(RendererTexCoord));
        memset(&renderer->tpage_data[renderer->vertex_count + 3], 0, 3 * sizeof(RendererTPage));
    }

    renderer->vertex_count += 6;
}

void renderer_set_texture_mode(Renderer* renderer, bool enabled) {
    if (renderer->texture_enabled != enabled) {
        renderer_draw(renderer); // Flush current batch
        renderer->texture_enabled = enabled;
    }
}

void renderer_set_raw_texture_mode(Renderer* renderer, bool enabled) {
    if (!renderer->initialized) return;
    if (renderer->raw_texture_enabled == enabled) return;
    renderer_draw(renderer);
    renderer->raw_texture_enabled = enabled;
    /* GL uniform applied per-batch in renderer_draw_gl() on GPU thread */
}

void renderer_set_screen_scale(Renderer* renderer, uint16_t width, uint16_t height) {
    if (!renderer->initialized) {
        return;
    }
    if (renderer->uniform_screen_scale_loc < 0) {
        return;
    }

    if (width == 0) {
        width = 1024;
    }
    if (height == 0) {
        height = 512;
    }

    if (renderer->screen_width == (float)width && renderer->screen_height == (float)height) {
        return; // Nothing to update
    }

    renderer_draw(renderer);
    renderer->screen_width  = (float)width;
    renderer->screen_height = (float)height;
    /* GL uniform applied per-batch in renderer_draw_gl() on GPU thread */
}

void renderer_set_texture_window(Renderer* renderer, uint8_t mask_x, uint8_t mask_y, uint8_t offset_x, uint8_t offset_y) {
    if (!renderer->initialized) return;

    // Calculate AND/OR masks based on DuckStation/Nocash logic
    // Mask: 0=Don't mask, 1-31=Mask (size = 8, 16, 32... 256 pixels)
    // Offset: Base address of the window (in 8 pixel steps)
    
    // Formula:
    // AND = ~(mask * 8)
    // OR = (offset & mask) * 8
    
    uint32_t and_x = ~(mask_x * 8u) & 0xFF;
    uint32_t and_y = ~(mask_y * 8u) & 0xFF;
    uint32_t or_x = (offset_x & mask_x) * 8u;
    uint32_t or_y = (offset_y & mask_y) * 8u;

    renderer_draw(renderer);
    /* Cache tex window — applied per-batch in renderer_draw_gl() on GPU thread */
    renderer->cached_tex_window[0] = (int32_t)and_x;
    renderer->cached_tex_window[1] = (int32_t)and_y;
    renderer->cached_tex_window[2] = (int32_t)or_x;
    renderer->cached_tex_window[3] = (int32_t)or_y;
}

/* -------------------------------------------------------------------------
 * renderer_record_vram_update — CPU thread: copy VRAM rect into pool and
 * record a GpuVramUpdate command for the GPU thread to execute.
 * ------------------------------------------------------------------------- */
static void renderer_record_vram_update(Renderer* renderer, const uint16_t* vram_data,
                                         uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                         bool update_display) {
    if (!renderer->initialized || w == 0 || h == 0) return;
    int wi = renderer->write_idx;
    GpuFrame* frame = &s_frame[wi];

    if (frame->vram_update_count >= GPU_MAX_VRAM_UPDATES) {
        LOG_RENDERER_WARN("[RENDERER] VRAM update overflow — skipping %u×%u rect", w, h);
        return;
    }

    /* Copy only the rect rows (R16UI, row-stride = 1024 halfwords) */
    uint32_t bytes_needed = (uint32_t)w * h * sizeof(uint16_t);
    uint32_t aligned = (bytes_needed + 3u) & ~3u;
    if (s_vram_pool_used[wi] + aligned > GPU_VRAM_POOL_SIZE) {
        s_vram_pool_skips++;
        LOG_RENDERER_WARN("[RENDERER] VRAM pool full — skipping %ux%u rect (%u KB needed, %u KB used)",
                          w, h, aligned >> 10, s_vram_pool_used[wi] >> 10);
        return;
    }

    uint8_t* dst = s_vram_pool[wi] + s_vram_pool_used[wi];
    for (uint16_t row = 0; row < h; row++) {
        const uint16_t* src_row = &vram_data[((uint32_t)(y + row)) * 1024u + x];
        memcpy(dst + (uint32_t)row * w * 2u, src_row, w * sizeof(uint16_t));
    }

    uint32_t idx = frame->vram_update_count++;
    GpuVramUpdate* u = &frame->vram_updates[idx];
    u->x              = x;
    u->y              = y;
    u->w              = w;
    u->h              = h;
    u->data_offset    = s_vram_pool_used[wi];
    u->update_display = update_display;
    u->depth24        = renderer->display_depth24;
    u->full_upload    = false;
    /* In 24bpp the VRAM halfword grid and the pixel grid run at different
     * rates (3 bytes per pixel = 1.5 halfwords), so a rect starting N
     * halfwords into the display area holds pixel (N*2)/3, not pixel N.
     * Games upload FMV frames as narrow vertical strips (Ace Combat 2: 24
     * halfwords = 16 pixels each), so writing each strip at its halfword x
     * smears the frame progressively further right across the screen. */
    u->dst_x = x;
    if (u->depth24 && update_display && x >= renderer->display_x)
        u->dst_x = (uint16_t)(renderer->display_x +
                              ((uint32_t)(x - renderer->display_x) * 2u) / 3u);
    s_vram_pool_used[wi] += aligned;
    if (s_vram_pool_used[wi] > s_vram_pool_peak) s_vram_pool_peak = s_vram_pool_used[wi];
    gpu_frame_record_op(frame, GPU_OP_VRAM_UPDATE, idx);
}

void renderer_get_pool_stats(Renderer* renderer, uint32_t* used, uint32_t* peak,
                             uint32_t* updates, uint32_t* skips) {
    int wi = renderer->write_idx;
    if (used)    *used    = s_vram_pool_used[wi];
    if (peak)    *peak    = s_vram_pool_peak;
    if (updates) *updates = s_frame[wi].vram_update_count;
    if (skips)   *skips   = s_vram_pool_skips;
}

void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data) {
    if (!renderer->initialized) return;
    lua_debug_notify("vram_full_upload");
    renderer_record_vram_update(renderer, vram_data, 0, 0, 1024, 512, false);
}

void renderer_upload_vram_rect(Renderer* renderer, const uint16_t* vram_data,
                                uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!renderer->initialized || w == 0 || h == 0) return;
    /* Mirror the rect into display_texture too. On real hardware a CPU/DMA
     * write straight into the displayed VRAM area is immediately visible;
     * display_texture only ever carried GL-rasterized pixels, so anything a
     * game paints by uploading (FMV frames decoded by the MDEC, 2D backdrops)
     * never showed up at all. Only the uploaded rect is stamped, so this does
     * not clobber rasterized pixels outside it. */
    renderer_record_vram_update(renderer, vram_data, x, y, w, h, true);
}

void renderer_apply_vram_readback(Renderer* renderer, uint16_t* vram_data) {
    if (!renderer->initialized || !vram_data) return;
    const uint8_t* rgb = renderer->vram_readback_rgb;
    for (uint32_t i = 0; i < 1024u * 512u; i++) {
        uint16_t r5 = (uint16_t)(rgb[i * 3 + 0] >> 3);
        uint16_t g5 = (uint16_t)(rgb[i * 3 + 1] >> 3);
        uint16_t b5 = (uint16_t)(rgb[i * 3 + 2] >> 3);
        /* Preserve the existing mask bit — the RGB8 readback carries no mask info
         * (that's Gap A, tracked separately); only the color channels come from
         * the composited display_texture. */
        uint16_t mask_bit = vram_data[i] & 0x8000u;
        vram_data[i] = mask_bit | (b5 << 10) | (g5 << 5) | r5;
    }
}

/* -------------------------------------------------------------------------
 * renderer_draw_gl — INTERNAL: called by GPU thread to execute one batch.
 * All GL calls are here; CPU thread never calls this directly.
 * ------------------------------------------------------------------------- */
static void renderer_draw_gl(Renderer* renderer, const GpuBatch* b, int slot) {
    if (b->vertex_count == 0) return;

    glDisable(GL_BLEND);  /* each batch starts with blend off; two-pass re-enables for STP pass */
    glUseProgram(renderer->shader_program);

    /* Apply cached state from batch snapshot */
    glUniform2i(renderer->uniform_offset_loc, b->offset_x, b->offset_y);
    if (renderer->uniform_screen_scale_loc >= 0)
        glUniform2f(renderer->uniform_screen_scale_loc,
                    b->screen_w * 0.5f, b->screen_h * 0.5f);
    if (renderer->uniform_tex_window_loc >= 0)
        glUniform4i(renderer->uniform_tex_window_loc,
                    b->tex_window[0], b->tex_window[1],
                    b->tex_window[2], b->tex_window[3]);
    if (renderer->uniform_dither_loc >= 0)
        glUniform1i(renderer->uniform_dither_loc, b->dither_enabled ? 1 : 0);
    glUniform1i(renderer->uniform_use_texture_loc, b->texture_enabled ? 1 : 0);
    if (renderer->uniform_raw_texture_loc >= 0)
        glUniform1i(renderer->uniform_raw_texture_loc, b->raw_texture_enabled ? 1 : 0);
    glUniform1i(renderer->uniform_vram_texture_loc, 0);

    /* Scissor */
    glEnable(GL_SCISSOR_TEST);
    glScissor(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3]);

    if (b->texture_enabled) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    }

    glBindVertexArray(renderer->vao);

    /* Upload vertex data from pool */
    uint32_t vs = b->vertex_start;
    uint32_t vc = b->vertex_count;
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(RendererPosition), &s_pos[slot][vs]);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(RendererColor),    &s_col[slot][vs]);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(RendererTexCoord), &s_tex[slot][vs]);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vc * sizeof(RendererTPage),    &s_tpg[slot][vs]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLenum prim = b->is_lines ? GL_LINES : GL_TRIANGLES;

    if (!b->is_lines && b->semi_trans_enabled && b->texture_enabled) {
        glDisable(GL_BLEND);
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, 0);
        glDrawArrays(prim, 0, vc);

        glEnable(GL_BLEND);
        switch (b->semi_trans_mode) {
            case 0:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
                glBlendColor(0.0f, 0.0f, 0.0f, 0.5f);
                break;
            case 1:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case 2:
                glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case 3:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
                glBlendColor(0.0f, 0.0f, 0.0f, 0.25f);
                break;
        }
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, 1);
        glDrawArrays(prim, 0, vc);
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, -1);
    } else if (!b->is_lines && b->semi_trans_enabled) {
        /* Flat/gouraud-shaded semi-transparent primitive: no per-texel STP bit
           to discard on (that only exists for textured sources) — the whole
           primitive is uniformly semi-transparent, so a single blended pass
           is correct, unlike the textured two-pass case above. */
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, -1);
        glEnable(GL_BLEND);
        switch (b->semi_trans_mode) {
            case 0:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
                glBlendColor(0.0f, 0.0f, 0.0f, 0.5f);
                break;
            case 1:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case 2:
                glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case 3:
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
                glBlendColor(0.0f, 0.0f, 0.0f, 0.25f);
                break;
        }
        glDrawArrays(prim, 0, vc);
    } else {
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, -1);
        glDrawArrays(prim, 0, vc);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* -------------------------------------------------------------------------
 * renderer_draw — CPU thread: records a batch. No GL calls.
 * ------------------------------------------------------------------------- */
void renderer_draw(Renderer* renderer) {
    if (!renderer->initialized) return;
    if (renderer->vertex_count == 0) return;

    int wi = renderer->write_idx;
    GpuFrame* frame = &s_frame[wi];

    if (frame->batch_count >= GPU_MAX_BATCHES) {
        LOG_RENDERER_WARN("[RENDERER] batch overflow — dropping %u vertices", renderer->vertex_count);
        renderer->vertex_count = 0;
        return;
    }

    uint32_t vtx_start = s_vtx[wi];
    uint32_t vtx_count = renderer->vertex_count;

    if (vtx_start + vtx_count > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_WARN("[RENDERER] vertex pool overflow — dropping batch");
        renderer->vertex_count = 0;
        return;
    }

    /* Copy vertex data to pool */
    memcpy(&s_pos[wi][vtx_start], renderer->positions_data, vtx_count * sizeof(RendererPosition));
    memcpy(&s_col[wi][vtx_start], renderer->colors_data,    vtx_count * sizeof(RendererColor));
    memcpy(&s_tex[wi][vtx_start], renderer->texcoords_data, vtx_count * sizeof(RendererTexCoord));
    memcpy(&s_tpg[wi][vtx_start], renderer->tpage_data,     vtx_count * sizeof(RendererTPage));
    s_vtx[wi] += vtx_count;

    /* Record batch with full state snapshot */
    uint32_t batch_idx = frame->batch_count++;
    GpuBatch* b = &frame->batches[batch_idx];
    b->vertex_start       = vtx_start;
    b->vertex_count       = vtx_count;
    b->is_lines           = false;
    b->texture_enabled    = renderer->texture_enabled;
    b->raw_texture_enabled = renderer->raw_texture_enabled;
    b->semi_trans_enabled = renderer->semi_trans_enabled;
    b->semi_trans_mode    = renderer->semi_trans_mode;
    b->dither_enabled     = renderer->dither_enabled;
    b->screen_w           = renderer->screen_width  ? renderer->screen_width  : 1024.0f;
    b->screen_h           = renderer->screen_height ? renderer->screen_height : 512.0f;
    b->offset_x           = renderer->cached_offset_x;
    b->offset_y           = renderer->cached_offset_y;
    b->tex_window[0]      = renderer->cached_tex_window[0];
    b->tex_window[1]      = renderer->cached_tex_window[1];
    b->tex_window[2]      = renderer->cached_tex_window[2];
    b->tex_window[3]      = renderer->cached_tex_window[3];
    b->scissor[0]         = renderer->cached_scissor[0];
    b->scissor[1]         = renderer->cached_scissor[1];
    b->scissor[2]         = renderer->cached_scissor[2];
    b->scissor[3]         = renderer->cached_scissor[3];

    gpu_frame_record_op(frame, GPU_OP_BATCH, batch_idx);
    renderer->vertex_count = 0;
    LOG_RENDERER_DEBUG("[RENDERER] batch recorded: %u verts (slot %d, batch %u)",
                       vtx_count, wi, frame->batch_count - 1);
}

// Blits a portion of the VRAM texture to the screen as a full-screen quad
// Uses the existing shader infrastructure - draws VRAM content directly
void renderer_blit_vram(Renderer* renderer, uint16_t vram_x, uint16_t vram_y, uint16_t width, uint16_t height) {
    if (!renderer->initialized) return;
    
    // First, flush any pending primitives
    renderer_draw(renderer);
    // Diagnostic: log the region being blitted so we can confirm renderer sampling
    LOG_RENDERER_WARN("[RENDERER] VRAM blit region: x=%u y=%u w=%u h=%u", vram_x, vram_y, width, height);
    
    // Create positions for a screen-filling quad using VRAM coordinates
    // The vertex shader will convert these to NDC
    RendererPosition positions[4];
    RendererColor colors[4];
    RendererTexCoord texcoords[4];
    
    // Full VRAM in screen coordinates (0-1023, 0-511 maps to -1..1, 1..-1)
    // The texture coordinates select the actual display region within VRAM.
    // Top-left
    positions[0].x = 0;
    positions[0].y = 0;
    // Top-right
    positions[1].x = 1024;
    positions[1].y = 0;
    // Bottom-left
    positions[2].x = 0;
    positions[2].y = 512;
    // Bottom-right
    positions[3].x = 1024;
    positions[3].y = 512;
    
    // Neutral color (the shader multiplies by 2, so 128 = 1.0)
    for (int i = 0; i < 4; i++) {
        colors[i].r = 128;
        colors[i].g = 128;
        colors[i].b = 128;
    }
    
    // Texture coordinates map to the display region in VRAM
    texcoords[0].u = vram_x;
    texcoords[0].v = vram_y;
    texcoords[1].u = vram_x + width;
    texcoords[1].v = vram_y;
    texcoords[2].u = vram_x;
    texcoords[2].v = vram_y + height;
    texcoords[3].u = vram_x + width;
    texcoords[3].v = vram_y + height;
    
    // Build TPage for 15-bit direct texture mode (depth = 2)
    // Page base X = vram_x / 64, Page base Y = vram_y / 256, Depth = 2 (15-bit)
    uint16_t tpage = (vram_x / 64) | ((vram_y / 256) << 4) | (2 << 7);
    uint16_t clut = 0; // Not used for 15-bit mode
    
    /* Save offset, zero it for the blit quad, then restore */
    int16_t saved_ox = renderer->cached_offset_x;
    int16_t saved_oy = renderer->cached_offset_y;
    renderer->cached_offset_x = 0;
    renderer->cached_offset_y = 0;

    renderer_set_texture_mode(renderer, true);
    renderer_push_quad(renderer, positions, colors, texcoords, clut, tpage);
    renderer_draw(renderer);

    renderer->cached_offset_x = saved_ox;
    renderer->cached_offset_y = saved_oy;
}

// Draws buffered primitives and requests buffer swap (swap happens in main loop)
void renderer_display(Renderer* renderer) {
    if (!renderer->initialized) return;
    LOG_RENDERER_DEBUG("[RENDERER] Renderer: Display requested.");
    // Draw any remaining buffered vertices
    renderer_draw(renderer);
    // Actual swap (SDL_GL_SwapWindow) happens in main.c/main loop
}

// Sets the drawing offset uniform. Forces a draw first.
// Based on Guide Section 5.10
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y) {
    if (!renderer->initialized) return;
    LOG_RENDERER_DEBUG("[RENDERER] draw offset (%d, %d) — flushing batch", x, y);
    renderer_draw(renderer);
    renderer->cached_offset_x = x;
    renderer->cached_offset_y = y;
    /* GL uniform applied per-batch in renderer_draw_gl() on GPU thread */
}

// ---------------------------------------------------------------------------
// renderer_set_drawing_area — enable scissor clipping to PSX drawing area
// ---------------------------------------------------------------------------
void renderer_set_drawing_area(Renderer* renderer, uint16_t left, uint16_t top,
                                uint16_t right, uint16_t bottom)
{
    if (!renderer->initialized) return;
    renderer_draw(renderer);

    float sw = renderer->screen_width  ? renderer->screen_width  : 1024.0f;
    float sh = renderer->screen_height ? renderer->screen_height : 512.0f;
    float sx = 1024.0f / sw;
    float sy = 512.0f  / sh;

    int gl_left  = (int)((float)left  * sx);
    int gl_right = (int)((float)(right  + 1) * sx);
    int gl_top   = (int)((float)top   * sy);
    int gl_bot   = (int)((float)(bottom + 1) * sy);
    int clip_w   = gl_right - gl_left;
    int clip_h   = gl_bot   - gl_top;
    if (clip_w <= 0) clip_w = 1;
    if (clip_h <= 0) clip_h = 1;

    int gl_y = 512 - gl_bot;
    if (gl_y < 0) gl_y = 0;

    /* Cache scissor — applied per-batch on GPU thread */
    renderer->cached_scissor[0] = gl_left;
    renderer->cached_scissor[1] = gl_y;
    renderer->cached_scissor[2] = clip_w;
    renderer->cached_scissor[3] = clip_h;
}

// ---------------------------------------------------------------------------
// renderer_set_dither_mode — enable/disable PSX 4x4 dithering in fragment shader
// ---------------------------------------------------------------------------
void renderer_set_dither_mode(Renderer* renderer, bool enabled)
{
    if (!renderer->initialized) return;
    if (renderer->dither_enabled == enabled) return;

    renderer_draw(renderer); // flush before changing dither state
    renderer->dither_enabled = enabled;
    // Uniform is set per-draw in renderer_draw(); no immediate GL call needed.
}

// renderer_set_semi_trans_mode — enable/disable GL blending for semi-trans
// ---------------------------------------------------------------------------
void renderer_set_semi_trans_mode(Renderer* renderer, bool enabled, uint8_t mode)
{
    if (!renderer->initialized) return;
    if (renderer->semi_trans_enabled == enabled && renderer->semi_trans_mode == mode)
        return;

    renderer_draw(renderer);
    renderer->semi_trans_enabled = enabled;
    renderer->semi_trans_mode    = mode;
    /* GL blend state applied per-batch in renderer_draw_gl() on GPU thread */
}

// ---------------------------------------------------------------------------
// renderer_push_line — CPU thread: record a 2-vertex line batch.
// ---------------------------------------------------------------------------
void renderer_push_line(Renderer* renderer, RendererPosition pos[2], RendererColor col[2])
{
    if (!renderer->initialized) return;

    /* Flush any pending triangle batch first (different primitive type) */
    renderer_draw(renderer);

    int wi = renderer->write_idx;
    GpuFrame* frame = &s_frame[wi];

    if (frame->batch_count >= GPU_MAX_BATCHES) {
        LOG_RENDERER_WARN("[RENDERER] batch overflow in push_line — skipping");
        return;
    }
    if (s_vtx[wi] + 2 > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_WARN("[RENDERER] vertex pool overflow in push_line — skipping");
        return;
    }

    uint32_t vs = s_vtx[wi];
    RendererTexCoord zero_tc = {0, 0};
    RendererTPage    zero_tp = {0, 0};
    s_pos[wi][vs]     = pos[0]; s_pos[wi][vs+1] = pos[1];
    s_col[wi][vs]     = col[0]; s_col[wi][vs+1] = col[1];
    s_tex[wi][vs]     = zero_tc; s_tex[wi][vs+1] = zero_tc;
    s_tpg[wi][vs]     = zero_tp; s_tpg[wi][vs+1] = zero_tp;
    s_vtx[wi] += 2;

    uint32_t line_batch_idx = frame->batch_count++;
    GpuBatch* b = &frame->batches[line_batch_idx];
    b->vertex_start       = vs;
    b->vertex_count       = 2;
    b->is_lines           = true;
    b->texture_enabled    = false;
    b->raw_texture_enabled = false;
    b->semi_trans_enabled = false;
    b->dither_enabled     = renderer->dither_enabled;
    b->semi_trans_mode    = 0;
    b->screen_w           = renderer->screen_width  ? renderer->screen_width  : 1024.0f;
    b->screen_h           = renderer->screen_height ? renderer->screen_height : 512.0f;
    b->offset_x           = renderer->cached_offset_x;
    b->offset_y           = renderer->cached_offset_y;
    b->tex_window[0]      = renderer->cached_tex_window[0];
    b->tex_window[1]      = renderer->cached_tex_window[1];
    b->tex_window[2]      = renderer->cached_tex_window[2];
    b->tex_window[3]      = renderer->cached_tex_window[3];
    b->scissor[0]         = renderer->cached_scissor[0];
    b->scissor[1]         = renderer->cached_scissor[1];
    b->scissor[2]         = renderer->cached_scissor[2];
    b->scissor[3]         = renderer->cached_scissor[3];

    gpu_frame_record_op(frame, GPU_OP_BATCH, line_batch_idx);
}

GLuint renderer_get_display_texture(Renderer* renderer) {
    if (!renderer || !renderer->initialized) return 0;
    return renderer->display_texture;
}

void renderer_update_vram_viewer(Renderer* renderer, const uint8_t* vram_bytes) {
    if (!renderer->initialized) return;

    int wi = renderer->write_idx;
    GpuFrame* frame = &s_frame[wi];
    if (frame->vram_update_count >= GPU_MAX_VRAM_UPDATES) return;

    /* RGBA8: 4 bytes/pixel × 1024×512 = 2 MB — fits in our 2 MB pool */
    uint32_t bytes_needed = 1024u * 512u * 4u;
    if (s_vram_pool_used[wi] + bytes_needed > GPU_VRAM_POOL_SIZE) return;

    uint8_t* dst = s_vram_pool[wi] + s_vram_pool_used[wi];
    const uint16_t* src = (const uint16_t*)vram_bytes;
    const VramViewParams* vp = &renderer->vram_view;

    /* Every mode writes one RGBA8 texel per VRAM *halfword* slot, so the
     * viewer image always stays 1024x512 and VRAM coordinates map 1:1 to
     * texels regardless of the decode mode — the sub-modes just reinterpret
     * what each slot's bytes mean (PCSX-Redux does the same, in a shader). */
    for (uint32_t i = 0; i < 1024u * 512u; i++) {
        uint16_t raw = src[i];
        uint8_t r, g, b;

        switch (vp->mode) {
            case VRAM_VIEW_4BPP: {
                /* Four 4-bit indices per halfword; look each up in the CLUT
                 * row and average them into this slot's texel so the whole
                 * page stays visible at 1:1 scale. */
                uint32_t clut_base = (uint32_t)vp->clut_y * 1024u + vp->clut_x;
                uint32_t sr = 0, sg = 0, sb = 0;
                for (int n = 0; n < 4; n++) {
                    uint16_t entry = src[(clut_base + ((raw >> (n * 4)) & 0xFu)) & 0x7FFFFu];
                    sr += (uint32_t)((entry      ) & 0x1Fu) << 3;
                    sg += (uint32_t)((entry >>  5) & 0x1Fu) << 3;
                    sb += (uint32_t)((entry >> 10) & 0x1Fu) << 3;
                }
                r = (uint8_t)(sr / 4); g = (uint8_t)(sg / 4); b = (uint8_t)(sb / 4);
                break;
            }
            case VRAM_VIEW_8BPP: {
                uint32_t clut_base = (uint32_t)vp->clut_y * 1024u + vp->clut_x;
                uint16_t e0 = src[(clut_base + (raw & 0xFFu)) & 0x7FFFFu];
                uint16_t e1 = src[(clut_base + ((raw >> 8) & 0xFFu)) & 0x7FFFFu];
                r = (uint8_t)(((((e0      ) & 0x1Fu) + ((e1      ) & 0x1Fu)) << 3) / 2);
                g = (uint8_t)(((((e0 >>  5) & 0x1Fu) + ((e1 >>  5) & 0x1Fu)) << 3) / 2);
                b = (uint8_t)(((((e0 >> 10) & 0x1Fu) + ((e1 >> 10) & 0x1Fu)) << 3) / 2);
                break;
            }
            case VRAM_VIEW_24BPP: {
                /* 3 bytes per pixel straddling halfword boundaries: read the
                 * byte stream directly at this slot, offset by the phase. */
                const uint8_t* bytes = (const uint8_t*)src;
                uint32_t off = i * 2u + (uint32_t)vp->shift24;
                r = bytes[(off    ) & 0xFFFFFu];
                g = bytes[(off + 1) & 0xFFFFFu];
                b = bytes[(off + 2) & 0xFFFFFu];
                break;
            }
            case VRAM_VIEW_16BPP:
            default:
                r = (uint8_t)((raw & 0x1Fu) << 3);
                g = (uint8_t)(((raw >> 5) & 0x1Fu) << 3);
                b = (uint8_t)(((raw >> 10) & 0x1Fu) << 3);
                break;
        }

        if (vp->show_alpha) {
            /* Mask bit only — makes it obvious which pixels are write-protected. */
            uint8_t a = (raw & 0x8000u) ? 255 : 0;
            r = g = b = a;
        } else if (vp->greyscale) {
            uint8_t y = (uint8_t)(((uint32_t)r * 77u + (uint32_t)g * 150u + (uint32_t)b * 29u) >> 8);
            r = g = b = y;
        }

        *dst++ = r; *dst++ = g; *dst++ = b; *dst++ = 255;
    }

    uint32_t viewer_idx = frame->vram_update_count++;
    GpuVramUpdate* u = &frame->vram_updates[viewer_idx];
    u->x              = 0;
    u->y              = 0;
    u->w              = 1024;
    u->h              = 512;
    u->dst_x          = 0;
    u->data_offset    = s_vram_pool_used[wi];
    u->update_display = false;
    u->depth24        = false;
    u->full_upload    = false;
    u->is_viewer      = true;
    s_vram_pool_used[wi] += bytes_needed;
    gpu_frame_record_op(frame, GPU_OP_VRAM_UPDATE, viewer_idx);
}

GLuint renderer_get_vram_viewer_texture(Renderer* renderer) {
    if (!renderer || !renderer->initialized) return 0;
    return renderer->vram_viewer_texture;
}

// Cleans up OpenGL resources
void renderer_destroy(Renderer* renderer) {
    if (!renderer || !renderer->initialized) return;
    LOG_RENDERER_DEBUG("[RENDERER] Destroying Renderer...");

    // Delete OpenGL objects
    LOG_RENDERER_DEBUG("[RENDERER]   Deleting shader program (ID: %u)", renderer->shader_program);
    glDeleteProgram(renderer->shader_program); check_gl_error("destroy - glDeleteProgram");

    LOG_RENDERER_DEBUG("[RENDERER]   Deleting VBOs (Pos: %u, Col: %u)", renderer->position_buffer, renderer->color_buffer);
    glDeleteBuffers(1, &renderer->position_buffer); check_gl_error("destroy - glDeleteBuffers pos");
    glDeleteBuffers(1, &renderer->color_buffer); check_gl_error("destroy - glDeleteBuffers col");
    // Add texcoord buffer deletion later if implemented

    glDeleteTextures(1, &renderer->vram_viewer_texture); check_gl_error("destroy - vram_viewer_texture");

    LOG_RENDERER_DEBUG("[RENDERER]   Deleting VAO (ID: %u)", renderer->vao);
    glDeleteVertexArrays(1, &renderer->vao); check_gl_error("destroy - glDeleteVertexArrays");

    renderer->initialized = false;
    LOG_RENDERER_DEBUG("[RENDERER] Renderer Destroyed.");
}

/* =========================================================================
 * GPU Thread — execute VRAM updates then draw batches from a read slot.
 * All functions below are GPU-thread-only (GL context owned by GPU thread).
 * ========================================================================= */

/* Execute a single VRAM update. Called in submission order, interleaved with
 * draw batches, so a texture page reused later in the same frame is only
 * visible to draws that were actually issued after it (see GpuOp comment). */
static void renderer_execute_one_vram_update(Renderer* renderer, const GpuVramUpdate* u, int slot) {
    const uint8_t* data = s_vram_pool[slot] + u->data_offset;

    if (u->is_viewer) {
        /* RGBA8 viewer upload */
        glBindTexture(GL_TEXTURE_2D, renderer->vram_viewer_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512,
                        GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else {
        /* R16UI VRAM texture upload */
        glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, u->x, u->y, u->w, u->h,
                        GL_RED_INTEGER, GL_UNSIGNED_SHORT, data);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (u->update_display) {
            /* Convert R16UI rect to RGB and upload to display_texture */
            static uint8_t rgb_buf[1024 * 512 * 3];
            const uint16_t* src = (const uint16_t*)data;
            uint16_t out_w = u->w;

            if (u->depth24) {
                /* GPUSTAT.21 set: the display area holds packed 24bpp pixels —
                 * three consecutive bytes per pixel spanning halfword
                 * boundaries, so w halfwords carry (w*2)/3 pixels (DuckStation
                 * GPU_SW::CopyOut24Bit). MDEC FMV output is written this way. */
                out_w = (uint16_t)((uint32_t)u->w * 2u / 3u);
                if (out_w == 0) return;
                for (uint16_t row = 0; row < u->h; row++) {
                    const uint8_t* srow = (const uint8_t*)(src + (uint32_t)row * u->w);
                    uint8_t* dst = rgb_buf + (uint32_t)row * out_w * 3u;
                    memcpy(dst, srow, (size_t)out_w * 3u);
                }
            } else {
                for (uint16_t row = 0; row < u->h; row++) {
                    uint8_t* dst = rgb_buf + (uint32_t)row * u->w * 3u;
                    for (uint16_t col = 0; col < u->w; col++) {
                        uint16_t raw = src[(uint32_t)row * u->w + col];
                        *dst++ = (uint8_t)((raw & 0x1Fu) << 3);
                        *dst++ = (uint8_t)(((raw >> 5) & 0x1Fu) << 3);
                        *dst++ = (uint8_t)(((raw >> 10) & 0x1Fu) << 3);
                    }
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, renderer->display_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, u->dst_x, u->y, out_w, u->h,
                            GL_RGB, GL_UNSIGNED_BYTE, rgb_buf);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, renderer->display_fbo);
        }
    }
}

/* GPU thread argument */
typedef struct {
    Renderer*     renderer;
    SDL_Window*   window;
    SDL_GLContext gl_context;
} GpuThreadArg;

static GpuThreadArg s_gpu_thread_arg;

static int gpu_thread_main(void* userdata) {
    GpuThreadArg* arg = (GpuThreadArg*)userdata;
    Renderer*   renderer = arg->renderer;
    SDL_Window* window   = arg->window;

    if (SDL_GL_MakeCurrent(window, arg->gl_context) != 0) {
        LOG_RENDERER_ERROR("[GPU-THREAD] SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return -1;
    }
    LOG_RENDERER_INFO("[GPU-THREAD] GPU thread started — GL context acquired");

    /* ImGui OpenGL backend needs to be initialized on this thread */
    extern void imgui_opengl_new_frame(void);

    while (!SDL_AtomicGet(&renderer->gpu_stop)) {
        SDL_LockMutex(renderer->gpu_mutex);
        while (renderer->frames_pending == 0 && !SDL_AtomicGet(&renderer->gpu_stop))
            SDL_CondWait(renderer->frame_ready, renderer->gpu_mutex);
        if (SDL_AtomicGet(&renderer->gpu_stop)) {
            SDL_UnlockMutex(renderer->gpu_mutex);
            break;
        }
        int ri = 1 - renderer->write_idx; /* read slot = opposite of current write slot */
        SDL_UnlockMutex(renderer->gpu_mutex); /* frames_pending stays 1 until render+reset done */

        /* Bind FBO for all draw commands — viewport MUST match FBO size, not window */
        glBindFramebuffer(GL_FRAMEBUFFER, renderer->display_fbo);
        glViewport(0, 0, 1024, 512);

        /* Replay VRAM updates and draw batches in original submission order —
         * required when a texture page is re-uploaded mid-frame (see GpuOp). */
        for (uint32_t i = 0; i < s_frame[ri].op_count; i++) {
            const GpuOp* op = &s_frame[ri].ops[i];
            if (op->type == GPU_OP_VRAM_UPDATE)
                renderer_execute_one_vram_update(renderer, &s_frame[ri].vram_updates[op->index], ri);
            else
                renderer_draw_gl(renderer, &s_frame[ri].batches[op->index], ri);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        /* GPU_GAP_ANALYSIS Gap B: read display_texture back periodically — it already
         * composites CPU VRAM updates + GL-rasterized draws in order, so this is the
         * single correct source for the CPU-side VRAM model too. Consumed by the main
         * thread via renderer_apply_vram_readback(), after renderer_wait_frame_done()
         * provides the needed happens-before. Throttled (not every frame): glGetTexImage
         * forces a full GL pipeline sync, and 1024x512x3 every single frame measured as
         * a severe framerate regression (WSLg-hosted GL especially). Every 6th frame
         * (~10Hz at 60fps) keeps render-to-texture/VRAM-copy/CPU-read staleness down to
         * ~100ms worst case — far better than "never updates" — at a fraction of the cost. */
        {
            static uint32_t s_readback_tick = 0;
            if ((++s_readback_tick % 6) == 0) {
                glBindTexture(GL_TEXTURE_2D, renderer->display_texture);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, renderer->vram_readback_rgb);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }

        {
            static int s_dump_counter = 0;
            const char* dump_path = getenv("ZS1_DUMP_FRAME");
            if (dump_path) {
                int target = 300;
                const char* target_env = getenv("ZS1_DUMP_FRAME_N");
                if (target_env) target = atoi(target_env);
                if (s_dump_counter == target) {
                    unsigned char* buf = (unsigned char*)malloc(1024 * 512 * 3);
                    glBindTexture(GL_TEXTURE_2D, renderer->display_texture);
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buf);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    FILE* f = fopen(dump_path, "wb");
                    if (f) { fwrite(buf, 1, 1024 * 512 * 3, f); fclose(f); }
                    free(buf);
                    LOG_RENDERER_INFO("[GPU-THREAD] Dumped display_texture frame %d to %s", s_dump_counter, dump_path);
                }
                s_dump_counter++;
            }
        }

        /* 3) Prepare default framebuffer for ImGui — display via draw_ps1_display() ImGui::Image */
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        {
            int win_w, win_h;
            SDL_GetWindowSize(window, &win_w, &win_h);
            glViewport(0, 0, win_w, win_h);
        }
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        /* 4) ImGui: prepare GL backend for next frame, then render current frame */
        imgui_opengl_new_frame();
        if (s_frame[ri].imgui_draw_data) {
            extern void imgui_render_draw_data(void* draw_data);
            imgui_render_draw_data(s_frame[ri].imgui_draw_data);
        }

        SDL_GL_SwapWindow(window);

        /* Reset read slot for reuse */
        s_frame[ri].batch_count        = 0;
        s_frame[ri].vram_update_count  = 0;
        s_frame[ri].op_count           = 0;
        s_frame[ri].imgui_draw_data    = NULL;
        s_vtx[ri]                      = 0;
        s_vram_pool_used[ri]           = 0;

        /* Signal CPU that frame is done — set pending=0 here (after render+reset) */
        SDL_LockMutex(renderer->gpu_mutex);
        renderer->frames_pending = 0;
        SDL_CondSignal(renderer->frame_done);
        SDL_UnlockMutex(renderer->gpu_mutex);
    }

    SDL_GL_MakeCurrent(window, NULL);
    LOG_RENDERER_INFO("[GPU-THREAD] GPU thread exiting");
    return 0;
}

/* -------------------------------------------------------------------------
 * Public GPU thread control API
 * ------------------------------------------------------------------------- */

void renderer_start_gpu_thread(Renderer* renderer, SDL_Window* window, SDL_GLContext ctx) {
    renderer->gpu_mutex   = SDL_CreateMutex();
    renderer->frame_ready = SDL_CreateCond();
    renderer->frame_done  = SDL_CreateCond();
    renderer->sdl_window  = window;
    renderer->gl_context  = ctx;
    renderer->frames_pending = 0;
    SDL_AtomicSet(&renderer->gpu_stop, 0);

    /* Reset both slots */
    for (int i = 0; i < 2; i++) {
        s_frame[i].batch_count       = 0;
        s_frame[i].vram_update_count = 0;
        s_frame[i].op_count          = 0;
        s_frame[i].imgui_draw_data   = NULL;
        s_vtx[i]           = 0;
        s_vram_pool_used[i] = 0;
    }
    renderer->write_idx = 0;

    s_gpu_thread_arg.renderer   = renderer;
    s_gpu_thread_arg.window     = window;
    s_gpu_thread_arg.gl_context = ctx;

    renderer->gpu_thread = SDL_CreateThread(gpu_thread_main, "GPU", &s_gpu_thread_arg);
    if (!renderer->gpu_thread)
        LOG_RENDERER_ERROR("[RENDERER] Failed to create GPU thread: %s", SDL_GetError());
    else
        LOG_RENDERER_INFO("[RENDERER] GPU thread started");
}

void renderer_stop_gpu_thread(Renderer* renderer) {
    if (!renderer->gpu_thread) return;
    SDL_AtomicSet(&renderer->gpu_stop, 1);
    SDL_LockMutex(renderer->gpu_mutex);
    SDL_CondSignal(renderer->frame_ready);
    SDL_UnlockMutex(renderer->gpu_mutex);
    SDL_WaitThread(renderer->gpu_thread, NULL);
    renderer->gpu_thread = NULL;
    SDL_DestroyMutex(renderer->gpu_mutex);
    SDL_DestroyCond(renderer->frame_ready);
    SDL_DestroyCond(renderer->frame_done);
    renderer->gpu_mutex   = NULL;
    renderer->frame_ready = NULL;
    renderer->frame_done  = NULL;
    LOG_RENDERER_INFO("[RENDERER] GPU thread stopped");
}

void renderer_set_display_region(Renderer* renderer, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    renderer->display_x = x;
    renderer->display_y = y;
    renderer->display_w = w;
    renderer->display_h = h;
}

void renderer_set_vram_view_params(Renderer* renderer, const VramViewParams* p) {
    if (p) renderer->vram_view = *p;
}

void renderer_set_display_depth24(Renderer* renderer, bool depth24) {
    renderer->display_depth24 = depth24;
}

void renderer_submit_frame(Renderer* renderer, void* imgui_draw_data) {
    if (!renderer->gpu_thread) return;

    /* Flush any leftover batch from CPU */
    renderer_draw(renderer);

    SDL_LockMutex(renderer->gpu_mutex);
    /* Block if GPU is still rendering the previous frame */
    while (renderer->frames_pending > 0)
        SDL_CondWait(renderer->frame_done, renderer->gpu_mutex);

    GpuFrame* f = &s_frame[renderer->write_idx];
    f->imgui_draw_data = imgui_draw_data;
    f->disp_x = renderer->display_x;
    f->disp_y = renderer->display_y;
    f->disp_w = renderer->display_w;
    f->disp_h = renderer->display_h;
    renderer->write_idx    = 1 - renderer->write_idx;  /* swap */
    renderer->frames_pending = 1;
    SDL_CondSignal(renderer->frame_ready);
    SDL_UnlockMutex(renderer->gpu_mutex);
}

void renderer_wait_frame_done(Renderer* renderer) {
    if (!renderer->gpu_thread) return;
    SDL_LockMutex(renderer->gpu_mutex);
    while (renderer->frames_pending > 0)
        SDL_CondWait(renderer->frame_done, renderer->gpu_mutex);
    SDL_UnlockMutex(renderer->gpu_mutex);
}