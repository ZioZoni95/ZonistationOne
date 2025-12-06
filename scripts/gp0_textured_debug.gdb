# Load this script in gdb with:  source scripts/gp0_textured_debug.gdb
# It will hook textured quad calls, then renderer_push_quad & renderer_draw
# to trace the full pipeline from GPU command to OpenGL draw.

set pagination off

# ---- Dump GP0 textured quad command (after locals are initialized) ----
define dump_gp0_textured
    printf "\n=== [GPU] gp0_quad_texture_blend_opaque() ===\n"
    printf "  gp0 command words (9):\n"
    set $i = 0
    while $i < 9
        printf "    [%d] = 0x%08x\n", $i, gpu->gp0_command_buffer.buffer[$i]
        set $i = $i + 1
    end
    printf "  CLUT: 0x%04x  TexPage: 0x%04x\n", clut, texpage
    printf "  Vertices:\n"
    printf "    V0: (%d, %d)\n", (int)p[0].x, (int)p[0].y
    printf "    V1: (%d, %d)\n", (int)p[1].x, (int)p[1].y
    printf "    V2: (%d, %d)\n", (int)p[2].x, (int)p[2].y
    printf "    V3: (%d, %d)\n", (int)p[3].x, (int)p[3].y
    printf "  UVs:\n"
    printf "    T0: (%d, %d)\n", (int)t[0].u, (int)t[0].v
    printf "    T1: (%d, %d)\n", (int)t[1].u, (int)t[1].v
    printf "    T2: (%d, %d)\n", (int)t[2].u, (int)t[2].v
    printf "    T3: (%d, %d)\n", (int)t[3].u, (int)t[3].v
end

# ---- Dump renderer_push_quad state ----
define dump_push_quad
    printf "\n--- [RENDERER] renderer_push_quad() ---\n"
    printf "  texture_enabled: %d\n", renderer->texture_enabled
    printf "  vertex_count before: %u\n", renderer->vertex_count
end

# ---- Dump renderer_draw state ----
define dump_draw
    printf "\n--- [RENDERER] renderer_draw() ---\n"
    printf "  vertex_count: %u\n", renderer->vertex_count
    printf "  texture_enabled: %d\n", renderer->texture_enabled
    printf "  screen_scale: (%f, %f)\n", renderer->screen_width, renderer->screen_height
    printf "  uniform_screen_scale_loc: %d\n", renderer->uniform_screen_scale_loc
    printf "  uniform_use_texture_loc: %d\n", renderer->uniform_use_texture_loc
end

# ---- Breakpoints ----

# BP1: Break at line 328 (renderer_push_quad call) AFTER locals are set
break src/gpu.c:328
commands
    silent
    dump_gp0_textured
    continue
end

# BP2: renderer_push_quad (textured path)
break renderer_push_quad if tex != 0
commands
    silent
    dump_push_quad
    continue
end

# BP3: renderer_draw when there are textured vertices
break renderer_draw if renderer->texture_enabled == 1 && renderer->vertex_count > 0
commands
    silent
    dump_draw
    continue
end

printf "Breakpoints set: gpu.c:328 (textured quad), renderer_push_quad, renderer_draw\n"
printf "Run with: run\n"
