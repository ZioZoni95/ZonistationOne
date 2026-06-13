#include "renderer.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h> // For malloc, free, exit
#include <string.h> // For memcpy, memset

// Make sure GLEW/GLAD is included if not done in the header
// #define GLEW_STATIC
// #include <GL/glew.h>

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
    renderer_draw(renderer); // Flush before changing modulation behavior
    renderer->raw_texture_enabled = enabled;
    if (renderer->uniform_raw_texture_loc >= 0) {
        glUseProgram(renderer->shader_program);
        glUniform1i(renderer->uniform_raw_texture_loc, enabled ? 1 : 0);
        glUseProgram(0);
    }
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

    renderer_draw(renderer); // Keep batches consistent when scale changes

    renderer->screen_width = (float)width;
    renderer->screen_height = (float)height;

    glUseProgram(renderer->shader_program);
    glUniform2f(renderer->uniform_screen_scale_loc,
                renderer->screen_width * 0.5f,
                renderer->screen_height * 0.5f);
    glUseProgram(0);
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

    // Flush current batch before changing uniforms
    renderer_draw(renderer);

    glUseProgram(renderer->shader_program);
    if (renderer->uniform_tex_window_loc >= 0) {
        glUniform4i(renderer->uniform_tex_window_loc, (GLint)and_x, (GLint)and_y, (GLint)or_x, (GLint)or_y);
    }
    glUseProgram(0);
}

void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data) {
    if (!renderer->initialized) return;
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RED_INTEGER, GL_UNSIGNED_SHORT, vram_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderer_upload_vram_rect(Renderer* renderer, const uint16_t* vram_data,
                                uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!renderer->initialized || w == 0 || h == 0) return;
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 1024);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                    &vram_data[(uint32_t)y * 1024u + x]);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Uploads buffered data and performs the OpenGL draw call.
void renderer_draw(Renderer* renderer) {
     if (!renderer->initialized) {
         LOG_RENDERER_ERROR("[RENDERER] Renderer Error: Draw called before initialization.");
         return;
     }
     if (renderer->vertex_count == 0) {
        return; // Nothing to draw
     }

    LOG_RENDERER_DEBUG("[RENDERER] Renderer: Drawing %u vertices...", renderer->vertex_count);

    glUseProgram(renderer->shader_program); check_gl_error("draw - glUseProgram");
    glBindVertexArray(renderer->vao); check_gl_error("draw - glBindVertexArray");

    // Set texture uniforms
    glUniform1i(renderer->uniform_use_texture_loc, renderer->texture_enabled ? 1 : 0);
    if (renderer->uniform_raw_texture_loc >= 0) {
        glUniform1i(renderer->uniform_raw_texture_loc, renderer->raw_texture_enabled ? 1 : 0);
    }
    glUniform1i(renderer->uniform_vram_texture_loc, 0); // Texture unit 0
    if (renderer->uniform_dither_loc >= 0) {
        glUniform1i(renderer->uniform_dither_loc, renderer->dither_enabled ? 1 : 0);
    }

    if (renderer->texture_enabled) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    }

    // --- Upload Buffered Vertex Data via glBufferSubData ---
glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); check_gl_error("draw - glBindBuffer pos");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererPosition), renderer->positions_data);
    check_gl_error("draw - glBufferSubData pos");

glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer); check_gl_error("draw - glBindBuffer col");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererColor), renderer->colors_data);
    check_gl_error("draw - glBufferSubData col");

glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer); check_gl_error("draw - glBindBuffer tex");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererTexCoord), renderer->texcoords_data);
    check_gl_error("draw - glBufferSubData tex");

glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer); check_gl_error("draw - glBindBuffer tpage");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererTPage), renderer->tpage_data);
    check_gl_error("draw - glBufferSubData tpage");

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind GL_ARRAY_BUFFER target
    // ------------------------------------------------------

    /* Two-pass STP rendering for semi-transparent textured primitives.
     * Pass 1: blend OFF, discard STP=1 pixels → opaque STP=0 drawn.
     * Pass 2: blend ON,  discard STP=0 pixels → blended STP=1 drawn. */
    if (renderer->semi_trans_enabled && renderer->texture_enabled) {
        /* Pass 1 — opaque pixels (STP=0) */
        glDisable(GL_BLEND);
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, 0);
        glDrawArrays(GL_TRIANGLES, 0, renderer->vertex_count);
        check_gl_error("draw - pass1 STP=0");

        /* Pass 2 — blended pixels (STP=1): restore blend state */
        glEnable(GL_BLEND);
        switch (renderer->semi_trans_mode) {
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
        glDrawArrays(GL_TRIANGLES, 0, renderer->vertex_count);
        check_gl_error("draw - pass2 STP=1");

        /* Reset for next batch */
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, -1);
    } else {
        /* Single pass: opaque draw or non-textured semi-trans */
        if (renderer->uniform_stp_mode_loc >= 0)
            glUniform1i(renderer->uniform_stp_mode_loc, -1);
        glDrawArrays(GL_TRIANGLES, 0, renderer->vertex_count);
        check_gl_error("draw - glDrawArrays");
    }

    // --- Unbind ---
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Reset the CPU buffer count for the next batch
    renderer->vertex_count = 0;
    LOG_RENDERER_DEBUG("[RENDERER] Renderer: Draw finished, vertex count reset.");
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
    
    // Temporarily disable offset for screen blit
    glUseProgram(renderer->shader_program);
    glUniform2i(renderer->uniform_offset_loc, 0, 0);
    glUseProgram(0);
    
    // Push as textured quad
    renderer_set_texture_mode(renderer, true);
    renderer_push_quad(renderer, positions, colors, texcoords, clut, tpage);
    renderer_draw(renderer);
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
     LOG_RENDERER_DEBUG("[RENDERER] Renderer: Setting Draw Offset (%d, %d), forcing draw first.", x, y);
     renderer_draw(renderer);
     glUseProgram(renderer->shader_program); check_gl_error("set_draw_offset - glUseProgram");
     glUniform2i(renderer->uniform_offset_loc, (GLint)x, (GLint)y);
     check_gl_error("set_draw_offset - glUniform2i");
     glUseProgram(0);
}

// ---------------------------------------------------------------------------
// renderer_set_drawing_area — enable scissor clipping to PSX drawing area
// ---------------------------------------------------------------------------
void renderer_set_drawing_area(Renderer* renderer, uint16_t left, uint16_t top,
                                uint16_t right, uint16_t bottom)
{
    if (!renderer->initialized) return;
    renderer_draw(renderer); // flush before changing scissor

    // Scale drawing-area VRAM coordinates to window pixels.
    // screen_width/height hold the active display dimensions (e.g. 320x240).
    // The SDL window is always 1024x512. With screen_scale = (sw/2, sh/2) the
    // display area fills the window, so: win_x = vram_x * 1024 / sw.
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

    // OpenGL scissor: Y=0 is bottom of window; flip.
    int gl_y = 512 - gl_bot;
    if (gl_y < 0) gl_y = 0;

    glEnable(GL_SCISSOR_TEST);
    glScissor((GLint)gl_left, (GLint)gl_y, (GLsizei)clip_w, (GLsizei)clip_h);
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

    renderer_draw(renderer); // flush before blend state change

    renderer->semi_trans_enabled = enabled;
    renderer->semi_trans_mode    = mode;

    if (!enabled) {
        glDisable(GL_BLEND);
        return;
    }

    glEnable(GL_BLEND);
    switch (mode) {
        case 0: // B/2 + F/2
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
            glBlendColor(0.0f, 0.0f, 0.0f, 0.5f);
            break;
        case 1: // B + F
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case 2: // B - F
            glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case 3: // B + F/4
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
            glBlendColor(0.0f, 0.0f, 0.0f, 0.25f);
            break;
        default:
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

// ---------------------------------------------------------------------------
// renderer_push_line — draw a 2-vertex line segment immediately
// Flushes any pending triangle batch first, then draws GL_LINES.
// ---------------------------------------------------------------------------
void renderer_push_line(Renderer* renderer, RendererPosition pos[2], RendererColor col[2])
{
    if (!renderer->initialized) return;

    // Flush pending triangle batch
    renderer_draw(renderer);

    // Upload 2 vertices to the existing VBOs
    renderer->positions_data[0] = pos[0];
    renderer->positions_data[1] = pos[1];
    renderer->colors_data[0]    = col[0];
    renderer->colors_data[1]    = col[1];
    // Zero out texcoord/tpage for untextured lines
    RendererTexCoord zero_tc = {0, 0};
    RendererTPage    zero_tp = {0, 0};
    renderer->texcoords_data[0] = zero_tc;
    renderer->texcoords_data[1] = zero_tc;
    renderer->tpage_data[0]     = zero_tp;
    renderer->tpage_data[1]     = zero_tp;

    glUseProgram(renderer->shader_program);
    glBindVertexArray(renderer->vao);

    glUniform1i(renderer->uniform_use_texture_loc, 0);
    if (renderer->uniform_raw_texture_loc >= 0)
        glUniform1i(renderer->uniform_raw_texture_loc, 0);
    glUniform1i(renderer->uniform_vram_texture_loc, 0);

    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(RendererPosition), renderer->positions_data);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(RendererColor), renderer->colors_data);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(RendererTexCoord), renderer->texcoords_data);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, 2 * sizeof(RendererTPage), renderer->tpage_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDrawArrays(GL_LINES, 0, 2);
    check_gl_error("renderer_push_line - glDrawArrays");

    glBindVertexArray(0);
    glUseProgram(0);
}

GLuint renderer_get_display_texture(Renderer* renderer) {
    if (!renderer || !renderer->initialized) return 0;
    return renderer->display_texture;
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

    LOG_RENDERER_DEBUG("[RENDERER]   Deleting VAO (ID: %u)", renderer->vao);
    glDeleteVertexArrays(1, &renderer->vao); check_gl_error("destroy - glDeleteVertexArrays");

    renderer->initialized = false;
    LOG_RENDERER_DEBUG("[RENDERER] Renderer Destroyed.");
}