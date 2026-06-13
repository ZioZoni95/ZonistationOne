#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>
#include <stdbool.h>

// --- OpenGL Includes ---
// Make sure you have GLEW (or GLAD) headers included correctly in your project setup
#define GLEW_STATIC // Or define dynamically if preferred
#include <GL/glew.h> // Or your GLAD/other GL header

// --- Renderer-Specific Data Types ---

// Represents a 2D vertex position in PSX VRAM coordinates (signed 16-bit)
typedef struct {
    GLshort x, y; // OpenGL types (GLshort is int16_t)
} RendererPosition;

// Represents an RGB color (unsigned 8-bit per component)
typedef struct {
    GLubyte r, g, b; // OpenGL types (GLubyte is uint8_t)
} RendererColor;

// Represents texture coordinates (absolute VRAM coordinates)
typedef struct {
    GLshort u, v;
} RendererTexCoord;

// Represents CLUT and Texture Page information
typedef struct {
    GLushort clut;    // CLUT ID (contains X,Y of palette)
    GLushort tpage;   // Texture Page ID (contains BaseX, BaseY, Depth)
} RendererTPage;

// --- Renderer State ---

// Maximum number of vertices the renderer can buffer before forcing a draw call.
// Adjust as needed for performance/memory trade-offs. (Guide uses 64*1024)
#define VERTEX_BUFFER_LEN (64 * 1024)

// Structure holding the state of the OpenGL renderer
typedef struct {
    // OpenGL Object IDs
    GLuint vao;             // Vertex Array Object: Groups VBO bindings and attribute pointers
    GLuint position_buffer; // Vertex Buffer Object (VBO) storing vertex positions
    GLuint color_buffer;    // Vertex Buffer Object (VBO) storing vertex colors
    GLuint texcoord_buffer; // VBO for texture coordinates
    GLuint tpage_buffer;    // VBO for CLUT/TPage info
    GLuint shader_program;  // ID of the compiled and linked GLSL shader program
    GLuint vram_texture;    // Texture object for VRAM

    // Off-screen rendering (PCSX-Redux pattern)
    GLuint display_fbo;     // Framebuffer Object for the main display
    GLuint display_texture; // Texture attached to the display FBO

    // Shader Uniform Location
    GLint uniform_offset_loc; // Location ID of the 'offset' uniform in the vertex shader
    GLint uniform_use_texture_loc;
    GLint uniform_raw_texture_loc; // 1 = use raw texture color (no modulation)
    GLint uniform_vram_texture_loc;
    GLint uniform_screen_scale_loc; // Location for screen scaling uniform
    GLint uniform_tex_window_loc;   // Location for ivec4 u_texWindow (and_x,and_y,or_x,or_y)
    GLint uniform_dither_loc;       // Location for u_dither_enable (1=on, 0=off)
    GLint uniform_stp_mode_loc;     /* -1=off, 0=opaque pass (discard STP=1), 1=blend pass (discard STP=0) */

    // CPU-Side Buffers (Temporary storage before uploading to GPU)
    // These hold the data pushed by the GPU command handlers.
    RendererPosition positions_data[VERTEX_BUFFER_LEN]; // CPU buffer for vertex positions
    RendererColor colors_data[VERTEX_BUFFER_LEN];       // CPU buffer for vertex colors
    RendererTexCoord texcoords_data[VERTEX_BUFFER_LEN]; // CPU buffer for texture coordinates
    RendererTPage tpage_data[VERTEX_BUFFER_LEN];        // CPU buffer for TPage/CLUT

    // State Tracking
    uint32_t vertex_count;      // Number of vertices currently buffered in the CPU-side arrays
    bool initialized;           // Flag indicating if the renderer has been successfully initialized
    bool texture_enabled;       // Current texture mode
    bool raw_texture_enabled;   // Current raw-texture mode (skip modulation)
    float screen_width;         // Target display width (in PSX pixels)
    float screen_height;        // Target display height (in PSX pixels)
    bool semi_trans_enabled;    // Whether semi-transparency blending is active
    uint8_t semi_trans_mode;    // 0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4
    bool dither_enabled;        // Whether 4x4 PSX dithering is active for current primitive
} Renderer;

// --- Function Prototypes ---

/**
 * @brief Initializes the OpenGL renderer.
 * Compiles shaders, links program, creates VAO and VBOs, sets initial GL state.
 * Must be called after an OpenGL context is created.
 * @param renderer Pointer to the Renderer struct to initialize.
 * @return True if initialization was successful, false otherwise.
 */
bool renderer_init(Renderer* renderer);

/**
 * @brief Gets the OpenGL texture ID used for the off-screen display.
 * @param renderer Pointer to the Renderer.
 * @return OpenGL texture ID.
 */
GLuint renderer_get_display_texture(Renderer* renderer);

/**
 * @brief Buffers a triangle's vertex data for later drawing.
 * Copies position and color data into the renderer's CPU-side buffers.
 * If the buffer is full, it forces a draw call before adding the new triangle.
 * @param renderer Pointer to the Renderer instance.
 * @param pos Array of 3 vertex positions.
 * @param col Array of 3 vertex colors.
 * @param tex Array of 3 texture coordinates (can be NULL if untextured).
 * @param clut CLUT ID (only used if tex is not NULL).
 * @param tpage Texture Page ID (only used if tex is not NULL).
 */
void renderer_push_triangle(Renderer* renderer, RendererPosition pos[3], RendererColor col[3], RendererTexCoord tex[3], uint16_t clut, uint16_t tpage);

/**
 * @brief Buffers a quadrilateral's vertex data (as two triangles) for later drawing.
 * Decomposes the quad into two triangles and copies their vertex data.
 * If the buffer is full, it forces a draw call before adding the new quad.
 * @param renderer Pointer to the Renderer instance.
 * @param pos Array of 4 vertex positions (in PSX order).
 * @param col Array of 4 vertex colors (corresponding to positions).
 * @param tex Array of 4 texture coordinates (can be NULL if untextured).
 * @param clut CLUT ID (only used if tex is not NULL).
 * @param tpage Texture Page ID (only used if tex is not NULL).
 */
void renderer_push_quad(Renderer* renderer, RendererPosition pos[4], RendererColor col[4], RendererTexCoord tex[4], uint16_t clut, uint16_t tpage);

/**
 * @brief Sets the texture mode. Flushes the renderer if the mode changes.
 * @param renderer Pointer to the Renderer instance.
 * @param enabled True to enable texturing, false to disable.
 */
void renderer_set_texture_mode(Renderer* renderer, bool enabled);

/**
 * @brief Toggles raw-texture mode. When enabled, textures are used without color modulation.
 */
void renderer_set_raw_texture_mode(Renderer* renderer, bool enabled);

/**
 * @brief Adjusts how PSX coordinates map to the OpenGL screen by setting the
 *        effective display width/height (in PSX pixels).
 * @param renderer Pointer to the Renderer.
 * @param width Horizontal resolution in PSX pixels (e.g., 320, 512, 640).
 * @param height Vertical resolution in PSX pixels (e.g., 240, 480).
 */
void renderer_set_screen_scale(Renderer* renderer, uint16_t width, uint16_t height);

/**
 * @brief Sets the texture window mask and offset.
 * @param renderer Pointer to the Renderer.
 * @param mask_x Texture window X mask (5 bits).
 * @param mask_y Texture window Y mask (5 bits).
 * @param offset_x Texture window X offset (5 bits).
 * @param offset_y Texture window Y offset (5 bits).
 */
void renderer_set_texture_window(Renderer* renderer, uint8_t mask_x, uint8_t mask_y, uint8_t offset_x, uint8_t offset_y);

/**
 * @brief Uploads VRAM data to the GPU texture (full 1024x512).
 * @param renderer Pointer to the Renderer instance.
 * @param vram_data Pointer to the VRAM data (1024x512 uint16_t).
 */
void renderer_upload_vram(Renderer* renderer, const uint16_t* vram_data);

/**
 * @brief Uploads a rectangular sub-region of VRAM to the GPU texture.
 * Uses glTexSubImage2D with GL_UNPACK_ROW_LENGTH for efficiency.
 * @param renderer  Pointer to the Renderer instance.
 * @param vram_data Pointer to full 1024x512 VRAM buffer.
 * @param x, y      Top-left of the dirty region (VRAM coords).
 * @param w, h      Width and height of the dirty region.
 */
void renderer_upload_vram_rect(Renderer* renderer, const uint16_t* vram_data,
                                uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief Uploads buffered vertex data to the GPU and performs the OpenGL draw call.
 * Uses glBufferSubData to update VBOs with data from CPU buffers.
 * Issues a glDrawArrays call to render the buffered primitives (as triangles).
 * Resets the vertex count after drawing.
 * @param renderer Pointer to the Renderer instance.
 */
void renderer_draw(Renderer* renderer);

/**
 * @brief Helper function to draw buffered primitives and swap the window buffers.
 * Typically called once per frame from the main loop.
 * @param renderer Pointer to the Renderer instance.
 * // Removed SDL_Window* - swap happens in main loop
 */
void renderer_display(Renderer* renderer);

/**
 * @brief Sets the drawing offset uniform in the vertex shader.
 * Forces a draw of currently buffered primitives before updating the offset.
 * @param renderer Pointer to the Renderer instance.
 * @param x The signed horizontal drawing offset.
 * @param y The signed vertical drawing offset.
 */
void renderer_set_draw_offset(Renderer* renderer, int16_t x, int16_t y);

/**
 * @brief Destroys OpenGL resources (VBOs, VAO, Shader Program).
 * Should be called before the OpenGL context is destroyed.
 * @param renderer Pointer to the Renderer instance to destroy.
 */
void renderer_destroy(Renderer* renderer);

/**
 * @brief Blits the VRAM texture to the screen as a full-screen quad.
 * This displays the actual VRAM contents (used for BIOS logo, etc.).
 * @param renderer Pointer to the Renderer instance.
 * @param vram_x X start coordinate in VRAM.
 * @param vram_y Y start coordinate in VRAM.
 * @param width Display width.
 * @param height Display height.
 */
void renderer_blit_vram(Renderer* renderer, uint16_t vram_x, uint16_t vram_y, uint16_t width, uint16_t height);

/**
 * @brief Checks for OpenGL errors using glGetError() and prints them.
 * Useful for debugging OpenGL calls.
 * @param location A string indicating where the check is being performed.
 */
void check_gl_error(const char* location);

/**
 * @brief Renders a line segment (2 vertices) immediately.
 * Flushes any pending triangle batch first, then draws the line.
 * @param renderer Pointer to the Renderer instance.
 * @param pos Array of 2 vertex positions.
 * @param col Array of 2 vertex colors.
 */
void renderer_push_line(Renderer* renderer, RendererPosition pos[2], RendererColor col[2]);

/**
 * @brief Sets the OpenGL scissor test rectangle from the PSX drawing area.
 * Enables GL_SCISSOR_TEST and calls glScissor with the given coordinates.
 * Forces a draw of any pending primitives before updating the scissor.
 * @param renderer Pointer to the Renderer.
 * @param left   Drawing area left boundary (inclusive).
 * @param top    Drawing area top boundary (inclusive).
 * @param right  Drawing area right boundary (inclusive).
 * @param bottom Drawing area bottom boundary (inclusive).
 */
void renderer_set_drawing_area(Renderer* renderer, uint16_t left, uint16_t top,
                                uint16_t right, uint16_t bottom);

/**
 * @brief Enables or disables semi-transparency blending and sets the blend mode.
 * Flushes any pending primitives before changing blend state.
 * @param renderer  Pointer to the Renderer.
 * @param enabled   True to enable blending for semi-transparent primitives.
 * @param mode      0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4
 */
void renderer_set_semi_trans_mode(Renderer* renderer, bool enabled, uint8_t mode);

/**
 * @brief Enables or disables PSX 4x4 dithering in the fragment shader.
 * Per PSX spec: applies to gouraud-shaded/textured polygons and lines only.
 * Flush is forced before mode change.
 * @param renderer  Pointer to the Renderer.
 * @param enabled   True to enable dithering.
 */
void renderer_set_dither_mode(Renderer* renderer, bool enabled);


#endif // RENDERER_H