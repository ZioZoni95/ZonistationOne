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
        LOG_RENDERER_ERROR("OpenGL Error at %s: %s (0x%04x)\n", location, error_str, error);
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
    "    float xpos = (float(p.x) / 512.0) - 1.0;\n"
    "\n"
    // Convert Y coordinate from PSX VRAM (0..511, top-to-bottom) to OpenGL NDC (-1.0..+1.0, bottom-to-top)
    "    float ypos = 1.0 - (float(p.y) / 256.0); // Flip Y axis\n"
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
const char* fragment_shader_source =
    "#version 330 core\n"
    // Input: Color interpolated from the vertex shader outputs
    "in vec3 color;\n"
    "in vec2 tex_coord;\n"
    "flat in uvec2 tpage_info; // x=CLUT, y=TPage\n"
    "\n"
    "uniform sampler2D vram_texture;\n"
    "uniform int use_texture;\n"
    "\n"
    // Output: Final color of the fragment (RGBA)
    "out vec4 frag_color;\n"
    "\n"
    "void main() {\n"
    "    vec4 final_color = vec4(color, 1.0);\n"
    "    if (use_texture == 1) {\n"
    "        uint clut = tpage_info.x;\n"
    "        uint tpage = tpage_info.y;\n"
    "        uint depth = (tpage >> 7) & 3u;\n"
    "        uint page_x = (tpage & 0xFu) * 64u;\n"
    "        uint page_y = ((tpage >> 4) & 1u) * 256u;\n"
    "        uint clut_x = (clut & 0x3Fu) * 16u;\n"
    "        uint clut_y = (clut >> 6) & 0x1FFu;\n"
    "\n"
    "        vec4 tex_col = vec4(0.0);\n"
    "\n"
    "        if (depth == 0u) { // 4-bit\n"
    "            uint u = uint(tex_coord.x) & 0xFFu;\n"
    "            uint v = uint(tex_coord.y) & 0xFFu;\n"
    "            uint tex_x = page_x + (u / 4u);\n"
    "            uint tex_y = page_y + v;\n"
    "            \n"
    "            vec4 word = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0);\n"
    "            // Reconstruct raw 16-bit value from RGBA (1555 format)\n"
    "            uint raw_val = 0u;\n"
    "            raw_val |= uint(round(word.r * 31.0));\n"
    "            raw_val |= uint(round(word.g * 31.0)) << 5;\n"
    "            raw_val |= uint(round(word.b * 31.0)) << 10;\n"
    "            if (word.a > 0.5) raw_val |= 0x8000u;\n"
    "\n"
    "            uint shift = (u & 3u) * 4u;\n"
    "            uint index = (raw_val >> shift) & 0xFu;\n"
    "\n"
    "            uint clut_pos_x = clut_x + index;\n"
    "            uint clut_pos_y = clut_y;\n"
    "            tex_col = texelFetch(vram_texture, ivec2(clut_pos_x, clut_pos_y), 0);\n"
    "\n"
    "            // For paletted textures, if the color is fully black (0x0000), it's transparent\n"
    "            if (tex_col.r == 0.0 && tex_col.g == 0.0 && tex_col.b == 0.0 && tex_col.a < 0.5) discard;\n"
    "\n"
    "        } else if (depth == 1u) { // 8-bit\n"
    "            uint u = uint(tex_coord.x) & 0xFFu;\n"
    "            uint v = uint(tex_coord.y) & 0xFFu;\n"
    "            uint tex_x = page_x + (u / 2u);\n"
    "            uint tex_y = page_y + v;\n"
    "            \n"
    "            vec4 word = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0);\n"
    "            uint raw_val = 0u;\n"
    "            raw_val |= uint(round(word.r * 31.0));\n"
    "            raw_val |= uint(round(word.g * 31.0)) << 5;\n"
    "            raw_val |= uint(round(word.b * 31.0)) << 10;\n"
    "            if (word.a > 0.5) raw_val |= 0x8000u;\n"
    "\n"
    "            uint shift = (u & 1u) * 8u;\n"
    "            uint index = (raw_val >> shift) & 0xFFu;\n"
    "\n"
    "            uint clut_pos_x = clut_x + index;\n"
    "            uint clut_pos_y = clut_y;\n"
    "            tex_col = texelFetch(vram_texture, ivec2(clut_pos_x, clut_pos_y), 0);\n"
    "\n"
    "            if (tex_col.r == 0.0 && tex_col.g == 0.0 && tex_col.b == 0.0 && tex_col.a < 0.5) discard;\n"
    "\n"
    "        } else { // 15-bit direct\n"
    "            uint u = uint(tex_coord.x) & 0xFFu;\n"
    "            uint v = uint(tex_coord.y) & 0xFFu;\n"
    "            uint tex_x = page_x + u;\n"
    "            uint tex_y = page_y + v;\n"
    "            tex_col = texelFetch(vram_texture, ivec2(tex_x, tex_y), 0);\n"
    "            if (tex_col.r == 0.0 && tex_col.g == 0.0 && tex_col.b == 0.0 && tex_col.a < 0.5) discard;\n"
    "        }\n"
    "\n"
    "        final_color = vec4(tex_col.rgb * color * 2.0, 1.0);\n"
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
            LOG_RENDERER_ERROR("Shader Compilation Error (%s):\n%s\n",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment",
                log_buffer);
            free(log_buffer);
        } else {
            LOG_RENDERER_ERROR("Shader Compilation Error (%s) - Failed to allocate log buffer\n",
                (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
        }
        glDeleteShader(shader); // Delete the failed shader object
        check_gl_error("compile_shader (error path)");
        return 0; // Return 0 on failure
    }
    LOG_RENDERER_INFO("Shader compiled successfully (Type: %s)\n", (shader_type == GL_VERTEX_SHADER) ? "Vertex" : "Fragment");
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
            LOG_RENDERER_ERROR("Shader Program Linking Error:\n%s\n", log_buffer);
            free(log_buffer);
        } else {
            LOG_RENDERER_ERROR("Shader Program Linking Error - Failed to allocate log buffer\n");
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

    LOG_RENDERER_INFO("Shader program linked successfully (ID: %u)\n", program);
    check_gl_error("link_program (success path)");
    return program;
}


// --- Renderer Implementation ---

bool renderer_init(Renderer* renderer) {
    if (log_get_level() >= LOG_LEVEL_INFO) {
        LOG_RENDERER_INFO("[RENDERER] Initializing renderer");
    }
    LOG_RENDERER_INFO("Initializing Renderer...\n");
    renderer->initialized = false;
    renderer->vertex_count = 0;
    // Clear CPU-side buffers initially (optional but good practice)
    memset(renderer->positions_data, 0, sizeof(renderer->positions_data));
    memset(renderer->colors_data, 0, sizeof(renderer->colors_data));


    // Compile Shaders
    LOG_RENDERER_INFO("Compiling vertex shader...\n");
    GLuint vs = compile_shader(vertex_shader_source, GL_VERTEX_SHADER);
    LOG_RENDERER_INFO("Compiling fragment shader...\n");
    GLuint fs = compile_shader(fragment_shader_source, GL_FRAGMENT_SHADER);
    if (vs == 0 || fs == 0) {
        LOG_RENDERER_ERROR("Renderer Init Failed: Shader compilation error.\n");
        if (vs != 0) glDeleteShader(vs); // Clean up if one succeeded
        if (fs != 0) glDeleteShader(fs);
        return false;
    }

    // Link Program
    LOG_RENDERER_INFO("Linking shader program...\n");
    renderer->shader_program = link_program(vs, fs);
    // Delete individual shaders now that they are linked into the program
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (renderer->shader_program == 0) {
        LOG_RENDERER_ERROR("Renderer Init Failed: Shader linking error.\n");
        return false;
    }
    check_gl_error("After linking program");


    // Get Uniform Location for the drawing offset
    renderer->uniform_offset_loc = glGetUniformLocation(renderer->shader_program, "offset");
    if (renderer->uniform_offset_loc < 0) {
        // This isn't fatal, but offset won't work. Check for GL errors too.
        LOG_RENDERER_WARN("Warning: Could not find uniform 'offset'. Draw offset will not work.\n");
        check_gl_error("glGetUniformLocation offset"); // Check if there was an error other than not found
    } else {
        LOG_RENDERER_INFO("Found uniform 'offset' at location: %d\n", renderer->uniform_offset_loc);
        // Set initial offset to 0,0
        glUseProgram(renderer->shader_program); // Need to bind program to set uniform
        glUniform2i(renderer->uniform_offset_loc, 0, 0);
        glUseProgram(0); // Unbind program
    }
    check_gl_error("After getting/setting offset uniform");


    // --- Create Vertex Array Object (VAO) ---
    // VAO stores the links between VBOs and shader attributes.
    // Based on Guide Section 5.6
    LOG_RENDERER_INFO("Creating VAO...\n");
    glGenVertexArrays(1, &renderer->vao);
    glBindVertexArray(renderer->vao); // Bind the VAO to make it active
    LOG_RENDERER_INFO("VAO created (ID: %u) and bound.\n", renderer->vao);
    check_gl_error("After creating/binding VAO");


    // --- Create and Configure Position Vertex Buffer Object (VBO) ---
    LOG_RENDERER_INFO("Creating Position VBO...\n");
    glGenBuffers(1, &renderer->position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); // Bind the new buffer to the GL_ARRAY_BUFFER target
    LOG_RENDERER_INFO("Position VBO created (ID: %u) and bound.\n", renderer->position_buffer);

    // Allocate buffer storage on the GPU. We'll upload data later using glBufferSubData.
    // GL_DYNAMIC_DRAW is a hint that the data will be modified frequently.
    glBufferData(GL_ARRAY_BUFFER,               // Target buffer type
                 VERTEX_BUFFER_LEN * sizeof(RendererPosition), // Total buffer size in bytes
                 NULL,                         // Initial data (none)
                 GL_DYNAMIC_DRAW);             // Usage hint
    LOG_RENDERER_INFO("Position VBO allocated %lu bytes.\n", VERTEX_BUFFER_LEN * sizeof(RendererPosition));
    check_gl_error("After position VBO glBufferData");

    // --- Link Position VBO to Shader Attribute ---
    // Get the location of the 'vertex_position' attribute in the shader (should be 0 as per layout qualifier)
    GLint pos_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_position");
     if (pos_attrib_loc < 0) { LOG_RENDERER_WARN("Warning: Could not find attribute 'vertex_position'.\n"); }
     else { LOG_RENDERER_INFO("Attribute 'vertex_position' found at location %d.\n", pos_attrib_loc); }

    // Enable this vertex attribute array
    glEnableVertexAttribArray(pos_attrib_loc); // Use the obtained location

    // Specify how OpenGL should interpret the data in the VBO for this attribute
    glVertexAttribIPointer(pos_attrib_loc,       // Attribute location in the shader
                           2,                  // Number of components per vertex (x, y)
                           GL_SHORT,           // Data type of each component (signed 16-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererPosition)? Set 0 for now.
                           (void*)0);          // Offset of the first component in the buffer
    LOG_RENDERER_INFO("Position VBO linked to vertex shader attribute location %d.\n", pos_attrib_loc);
    check_gl_error("After setting position attribute pointer");


    // --- Create and Configure Color Vertex Buffer Object (VBO) ---
    LOG_RENDERER_INFO("Creating Color VBO...\n");
    glGenBuffers(1, &renderer->color_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer);
    LOG_RENDERER_INFO("Color VBO created (ID: %u) and bound.\n", renderer->color_buffer);

    // Allocate storage
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererColor), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_INFO("Color VBO allocated %lu bytes.\n", VERTEX_BUFFER_LEN * sizeof(RendererColor));
    check_gl_error("After color VBO glBufferData");

    // --- Link Color VBO to Shader Attribute ---
    GLint col_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_color");
     if (col_attrib_loc < 0) { LOG_RENDERER_WARN("Warning: Could not find attribute 'vertex_color'.\n"); }
     else { LOG_RENDERER_INFO("Attribute 'vertex_color' found at location %d.\n", col_attrib_loc); }

    glEnableVertexAttribArray(col_attrib_loc);

    // Specify data format for the color attribute
    glVertexAttribIPointer(col_attrib_loc,       // Attribute location
                           3,                  // Number of components (r, g, b)
                           GL_UNSIGNED_BYTE,   // Data type (unsigned 8-bit int)
                           0, // Stride (0 = tightly packed) --> Or sizeof(RendererColor)? Set 0 for now.
                           (void*)0);          // Offset
    LOG_RENDERER_INFO("Color VBO linked to vertex shader attribute location %d.\n", col_attrib_loc);
    check_gl_error("After setting color attribute pointer");

    // --- Create and Configure Texture Coordinate VBO ---
    LOG_RENDERER_INFO("Creating TexCoord VBO...\n");
    glGenBuffers(1, &renderer->texcoord_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererTexCoord), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_INFO("TexCoord VBO created (ID: %u) and bound.\n", renderer->texcoord_buffer);

    GLint tex_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_texcoord");
    if (tex_attrib_loc >= 0) {
        glEnableVertexAttribArray(tex_attrib_loc);
        glVertexAttribIPointer(tex_attrib_loc, 2, GL_SHORT, 0, (void*)0);
        LOG_RENDERER_INFO("Attribute 'vertex_texcoord' found at location %d.\n", tex_attrib_loc);
    } else {
        LOG_RENDERER_WARN("Warning: Could not find attribute 'vertex_texcoord'.\n");
    }

    // --- Create and Configure TPage/CLUT VBO ---
    LOG_RENDERER_INFO("Creating TPage VBO...\n");
    glGenBuffers(1, &renderer->tpage_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_LEN * sizeof(RendererTPage), NULL, GL_DYNAMIC_DRAW);
    LOG_RENDERER_INFO("TPage VBO created (ID: %u) and bound.\n", renderer->tpage_buffer);

    GLint tpage_attrib_loc = glGetAttribLocation(renderer->shader_program, "vertex_tpage");
    if (tpage_attrib_loc >= 0) {
        glEnableVertexAttribArray(tpage_attrib_loc);
        glVertexAttribIPointer(tpage_attrib_loc, 2, GL_UNSIGNED_SHORT, 0, (void*)0);
        LOG_RENDERER_INFO("Attribute 'vertex_tpage' found at location %d.\n", tpage_attrib_loc);
    } else {
        LOG_RENDERER_WARN("Warning: Could not find attribute 'vertex_tpage'.\n");
    }

    // --- Create VRAM Texture ---
    glGenTextures(1, &renderer->vram_texture);
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Allocate texture storage (1024x512, 16-bit RGBA)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
    LOG_RENDERER_INFO("VRAM Texture created (ID: %u).\n", renderer->vram_texture);

    renderer->uniform_use_texture_loc = glGetUniformLocation(renderer->shader_program, "use_texture");
    renderer->uniform_vram_texture_loc = glGetUniformLocation(renderer->shader_program, "vram_texture");

    // --- Unbind ---
    glBindVertexArray(0); // Unbind the VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind the VBO from the target
    LOG_RENDERER_INFO("VAO and VBO unbound.\n");


    // --- Initial GL State ---
    // Set the default clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    check_gl_error("After glClearColor");

    // Potentially enable depth testing if needed later
    // glEnable(GL_DEPTH_TEST);

    renderer->initialized = true;
    renderer->texture_enabled = false;
    LOG_RENDERER_INFO("Renderer Initialized Successfully.\n");
    return true;
}

// Buffers a triangle's vertex data
void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3], RendererTexCoord tex[3], uint16_t clut, uint16_t tpage) {
    if (!renderer->initialized) {
        LOG_RENDERER_ERROR("Renderer Error: push_triangle called before initialization.\n");
        return;
    }

    if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_DEBUG("Renderer: Vertex buffer full (%u verts), forcing draw before push_triangle.", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 3 > VERTEX_BUFFER_LEN) {
             LOG_RENDERER_ERROR("Renderer Error: Cannot push triangle, buffer still full after draw.\n");
             return;
        }
    }

    // Copy data to CPU-side buffers
    LOG_TRACE("Renderer: Buffering Triangle (Start Index: %u)", renderer->vertex_count);
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
        LOG_RENDERER_ERROR("Renderer Error: push_quad called before initialization.\n");
        return;
     }

     if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
        LOG_RENDERER_INFO("Renderer Info: Vertex buffer full (%u verts), forcing draw before push_quad.\n", renderer->vertex_count);
        renderer_draw(renderer);
        if (renderer->vertex_count + 6 > VERTEX_BUFFER_LEN) {
            LOG_RENDERER_ERROR("Renderer Error: Cannot push quad, buffer still full after draw.\n");
            return;
        }
     }

    LOG_RENDERER_DEBUG("Renderer: Buffering Quad (Start Index: %u)\n", renderer->vertex_count);
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

void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data) {
    if (!renderer->initialized) return;
    glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 512, GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, vram_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Uploads buffered data and performs the OpenGL draw call.
void renderer_draw(Renderer* renderer) {
     if (!renderer->initialized) {
         LOG_RENDERER_ERROR("Renderer Error: Draw called before initialization.\n");
         return;
     }
     if (renderer->vertex_count == 0) {
        // LOG_RENDERER_DEBUG("Renderer: Draw called with 0 vertices, skipping.\n"); // Optional debug
        return; // Nothing to draw
     }

    LOG_RENDERER_DEBUG("Renderer: Drawing %u vertices...", renderer->vertex_count);

    glUseProgram(renderer->shader_program); check_gl_error("draw - glUseProgram");
    glBindVertexArray(renderer->vao); check_gl_error("draw - glBindVertexArray");

    // Set texture uniforms
    glUniform1i(renderer->uniform_use_texture_loc, renderer->texture_enabled ? 1 : 0);
    glUniform1i(renderer->uniform_vram_texture_loc, 0); // Texture unit 0

    if (renderer->texture_enabled) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, renderer->vram_texture);
    }

    // --- Upload Buffered Vertex Data via glBufferSubData ---
    LOG_TRACE("  Uploading position data (%lu bytes)...", renderer->vertex_count * sizeof(RendererPosition));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->position_buffer); check_gl_error("draw - glBindBuffer pos");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererPosition), renderer->positions_data);
    check_gl_error("draw - glBufferSubData pos");

    LOG_TRACE("  Uploading color data (%lu bytes)...", renderer->vertex_count * sizeof(RendererColor));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->color_buffer); check_gl_error("draw - glBindBuffer col");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererColor), renderer->colors_data);
    check_gl_error("draw - glBufferSubData col");

    LOG_TRACE("  Uploading texcoord data (%lu bytes)...", renderer->vertex_count * sizeof(RendererTexCoord));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->texcoord_buffer); check_gl_error("draw - glBindBuffer tex");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererTexCoord), renderer->texcoords_data);
    check_gl_error("draw - glBufferSubData tex");

    LOG_TRACE("  Uploading tpage data (%lu bytes)...", renderer->vertex_count * sizeof(RendererTPage));
    glBindBuffer(GL_ARRAY_BUFFER, renderer->tpage_buffer); check_gl_error("draw - glBindBuffer tpage");
    glBufferSubData(GL_ARRAY_BUFFER, 0, renderer->vertex_count * sizeof(RendererTPage), renderer->tpage_data);
    check_gl_error("draw - glBufferSubData tpage");

    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind GL_ARRAY_BUFFER target
    // ------------------------------------------------------

    // Draw the buffered primitives (interpreted as triangles)
    LOG_TRACE("  Issuing glDrawArrays...");
    glDrawArrays(GL_TRIANGLES,      // Mode: interpret vertices as triangles
                 0,                 // Starting index in the enabled arrays
                 renderer->vertex_count); // Number of vertices to render
    check_gl_error("draw - glDrawArrays");

    // --- Unbind ---
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Reset the CPU buffer count for the next batch
    renderer->vertex_count = 0;
    LOG_RENDERER_DEBUG("Renderer: Draw finished, vertex count reset.\n");
}

// Draws buffered primitives and requests buffer swap (swap happens in main loop)
void renderer_display(Renderer* renderer) {
    if (!renderer->initialized) return;
    LOG_RENDERER_INFO("Renderer: Display requested.\n");
    // Draw any remaining buffered vertices
    renderer_draw(renderer);
    // Actual swap (SDL_GL_SwapWindow) happens in main.c/main loop
    // LOG_RENDERER_INFO("Renderer: Display finished (swap should happen in main loop).\n");
}

// Sets the drawing offset uniform. Forces a draw first.
// Based on Guide Section 5.10
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y) {
     if (!renderer->initialized) return;

     // Draw primitives with the *old* offset before changing it
     LOG_RENDERER_INFO("Renderer: Setting Draw Offset (%d, %d), forcing draw first.\n", x, y);
     renderer_draw(renderer);

     // Bind the shader program to set the uniform
     glUseProgram(renderer->shader_program); check_gl_error("set_draw_offset - glUseProgram");
     // Update the uniform value
     glUniform2i(renderer->uniform_offset_loc, (GLint)x, (GLint)y);
     check_gl_error("set_draw_offset - glUniform2i");
     // Unbind the program
     glUseProgram(0);
}

// Cleans up OpenGL resources
void renderer_destroy(Renderer* renderer) {
    if (!renderer->initialized) return;
    LOG_RENDERER_INFO("Destroying Renderer...\n");

    // Delete OpenGL objects
    LOG_RENDERER_INFO("  Deleting shader program (ID: %u)\n", renderer->shader_program);
    glDeleteProgram(renderer->shader_program); check_gl_error("destroy - glDeleteProgram");

    LOG_RENDERER_INFO("  Deleting VBOs (Pos: %u, Col: %u)\n", renderer->position_buffer, renderer->color_buffer);
    glDeleteBuffers(1, &renderer->position_buffer); check_gl_error("destroy - glDeleteBuffers pos");
    glDeleteBuffers(1, &renderer->color_buffer); check_gl_error("destroy - glDeleteBuffers col");
    // Add texcoord buffer deletion later if implemented

    LOG_RENDERER_INFO("  Deleting VAO (ID: %u)\n", renderer->vao);
    glDeleteVertexArrays(1, &renderer->vao); check_gl_error("destroy - glDeleteVertexArrays");

    renderer->initialized = false;
    LOG_RENDERER_INFO("Renderer Destroyed.\n");
}