/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "debug_ui.h"
#include "log.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <map>
#include <deque>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "cpu.h"
#include "interconnect.h"
#include "renderer.h"
#include "gpu_backend.h"
#include "debugger.h"
#include "spu.h"
#include "lua_debug.h"
#include "frame_events.h"
#include "controller.h"
#include "host_info.h"
#include "mdec.h"
}

static bool g_disasm_follow_pc = true;
static bool g_step_pending     = false;
static bool g_show_controller_mapping = false;

extern "C" const char* disassemble_mips(uint32_t instruction, uint32_t pc);

// ---------------------------------------------------------------------------
// Visual identity — the design tokens from docs/ui/ui_direction.html.
// The two accents ENCODE the data path: cyan is the video chain
// (CD → MDEC → DMA → VRAM), rose is the audio chain (XA → SPU → device).
// Severity (ok/warn/crit) is separate and reserved — never decoration.
// ---------------------------------------------------------------------------

#define COL(r,g,b)  ImVec4((r)/255.0f, (g)/255.0f, (b)/255.0f, 1.0f)
#define COLA(r,g,b,a) ImVec4((r)/255.0f, (g)/255.0f, (b)/255.0f, (a))

static ImVec4 ZS_GROUND    = COL(0x0A, 0x0D, 0x14);   // Deep Obsidian
static ImVec4 ZS_PANEL     = COL(0x13, 0x19, 0x26);   // Sleek Translucent Slate
static ImVec4 ZS_PANEL2    = COL(0x1A, 0x23, 0x34);   // Cyber Slate
static ImVec4 ZS_LINE      = COL(0x28, 0x36, 0x4F);   // High-contrast neon slate border
static ImVec4 ZS_LINE_SOFT = COL(0x1E, 0x28, 0x3C);
static ImVec4 ZS_TEXT      = COL(0xF1, 0xF5, 0xF9);   // Ultra-crisp White
static ImVec4 ZS_MUTED     = COL(0x94, 0xA3, 0xB8);   // Cool slate gray
static ImVec4 ZS_FAINT     = COL(0x64, 0x74, 0x8B);
static ImVec4 ZS_DATA      = COL(0x00, 0xF0, 0xFF);   // Neon Cyan
static ImVec4 ZS_AUDIO     = COL(0xFF, 0x2E, 0x7E);   // Neon Rose
static const ImVec4 ZS_OK        = COL(0x10, 0xB9, 0x81);   // Emerald Green
static const ImVec4 ZS_WARN      = COL(0xF5, 0x9E, 0x0B);   // Amber Gold
static const ImVec4 ZS_CRIT      = COL(0xEF, 0x44, 0x44);   // Vibrant Red
static ImVec4 ZS_RAIL_BG   = COL(0x0F, 0x14, 0x20);
static ImVec4 ZS_DOCK_BG   = COL(0x0D, 0x11, 0x1C);
static ImVec4 ZS_MODE_ON   = COL(0x1E, 0x2A, 0x3E);

/* Which of the two shells owns the window this frame. Declared up here because
 * debug_ui_init picks it from ZS1_UI before the shell's own code appears. */
enum ZsShell { SHELL_GAMEPLAY = 0, SHELL_DEBUG = 1 };
static int g_shell = SHELL_GAMEPLAY;

/* Which backend the UI was brought up against. Decides which ImGui renderer
 * half this file owns and which one the graphics backend owns. */
static int s_gfx_backend = GFX_BACKEND_GL33;

/* Faces and the display scale, set once in debug_ui_init. The panels reach for
 * these rather than Fonts[0]: a "monospace" log window that pushes the
 * proportional face is not monospaced, which is what the log dock did. */
static ImFont* g_font_ui   = nullptr;
static ImFont* g_font_mono = nullptr;
static ImFont* g_font_h1   = nullptr;
static float   g_ui_scale  = 1.0f;

/* --- themes ---------------------------------------------------------------
 *
 * A theme is the thirteen neutral and accent tokens above. Severity colours
 * (ok / warn / crit) and the four PlayStation-logo hues are deliberately not
 * part of it: severity must read the same in every theme, and the logo hues
 * identify the view modes. The ImGui style colours are derived from the tokens
 * (zs_mix), so a theme is a handful of numbers, not a second style sheet. */
struct ZsTheme {
    const char* name;
    const char* blurb;
    ImVec4 ground, panel, panel2, line, line_soft, text, muted, faint, data, audio, rail_bg, dock_bg, mode_on;
};

static const ZsTheme g_themes[] = {
    { "Neon", "cyan and rose on obsidian - the original",
      COL(0x0A,0x0D,0x14), COL(0x13,0x19,0x26), COL(0x1A,0x23,0x34), COL(0x28,0x36,0x4F), COL(0x1E,0x28,0x3C),
      COL(0xF1,0xF5,0xF9), COL(0x94,0xA3,0xB8), COL(0x64,0x74,0x8B), COL(0x00,0xF0,0xFF), COL(0xFF,0x2E,0x7E),
      COL(0x0F,0x14,0x20), COL(0x0D,0x11,0x1C), COL(0x1E,0x2A,0x3E) },
    { "Amber", "the Sony boot diamond: warm gold on brown-black",
      COL(0x0E,0x0A,0x07), COL(0x1B,0x14,0x0E), COL(0x26,0x1C,0x13), COL(0x4A,0x36,0x20), COL(0x33,0x25,0x16),
      COL(0xF7,0xEC,0xDC), COL(0xB3,0x9A,0x7A), COL(0x7D,0x68,0x50), COL(0xFF,0xB0,0x20), COL(0xFF,0x5A,0x3C),
      COL(0x14,0x0F,0x0A), COL(0x11,0x0C,0x08), COL(0x2C,0x20,0x14) },
    { "Terminal", "phosphor green, for long debugging sessions",
      COL(0x06,0x0C,0x08), COL(0x0D,0x17,0x11), COL(0x13,0x21,0x18), COL(0x24,0x40,0x2F), COL(0x19,0x2D,0x21),
      COL(0xE4,0xF6,0xE9), COL(0x8F,0xB5,0x9B), COL(0x5E,0x7F,0x6B), COL(0x39,0xFF,0x88), COL(0xFF,0xB4,0x54),
      COL(0x0A,0x12,0x0D), COL(0x08,0x0F,0x0B), COL(0x1A,0x2E,0x22) },
    { "Violet", "soft indigo with a lilac accent",
      COL(0x0C,0x0A,0x14), COL(0x16,0x13,0x25), COL(0x1F,0x1A,0x33), COL(0x3A,0x31,0x59), COL(0x28,0x22,0x40),
      COL(0xF1,0xEE,0xFA), COL(0xA5,0x9D,0xC4), COL(0x71,0x69,0x8F), COL(0xB7,0x9C,0xFF), COL(0xFF,0x6F,0xB5),
      COL(0x11,0x0E,0x1C), COL(0x0E,0x0C,0x18), COL(0x26,0x20,0x3D) },
    { "Graphite", "neutral grey, almost no colour of its own",
      COL(0x0B,0x0B,0x0D), COL(0x17,0x17,0x1B), COL(0x20,0x20,0x26), COL(0x38,0x38,0x42), COL(0x27,0x27,0x2F),
      COL(0xF2,0xF2,0xF4), COL(0xA0,0xA0,0xAC), COL(0x6E,0x6E,0x7A), COL(0x7D,0xD3,0xFC), COL(0xF4,0x72,0xB6),
      COL(0x12,0x12,0x16), COL(0x0F,0x0F,0x12), COL(0x2A,0x2A,0x32) },
};
static const int ZS_THEME_COUNT = (int)(sizeof(g_themes) / sizeof(g_themes[0]));
static int g_theme = 0;

static ImVec4 zs_mix(const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha);
}

static void apply_theme_colors();

/* Where the choice is kept between runs: a one-word file next to imgui.ini,
 * which is also written to the working directory. ZS1_THEME overrides it. */
static const char* const ZS_THEME_FILE = "zs1_theme.txt";

static void theme_save(void) {
    FILE* f = fopen(ZS_THEME_FILE, "w");
    if (!f) return;
    fprintf(f, "%s\n", g_themes[g_theme].name);
    fclose(f);
}

static int theme_find(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < ZS_THEME_COUNT; i++)
        if (strcasecmp(name, g_themes[i].name) == 0) return i;
    char* end = nullptr;
    long v = strtol(name, &end, 10);
    if (end && *end == '\0' && v >= 0 && v < ZS_THEME_COUNT) return (int)v;
    return -1;
}

static void theme_set(int idx, bool persist) {
    if (idx < 0 || idx >= ZS_THEME_COUNT) return;
    g_theme = idx;
    const ZsTheme& t = g_themes[idx];
    ZS_GROUND = t.ground; ZS_PANEL = t.panel; ZS_PANEL2 = t.panel2; ZS_LINE = t.line;
    ZS_LINE_SOFT = t.line_soft; ZS_TEXT = t.text; ZS_MUTED = t.muted; ZS_FAINT = t.faint;
    ZS_DATA = t.data; ZS_AUDIO = t.audio; ZS_RAIL_BG = t.rail_bg; ZS_DOCK_BG = t.dock_bg;
    ZS_MODE_ON = t.mode_on;
    if (ImGui::GetCurrentContext()) apply_theme_colors();
    if (persist) theme_save();
}

static void theme_load_initial(void) {
    int idx = -1;
    if (const char* e = getenv("ZS1_THEME")) idx = theme_find(e);
    if (idx < 0) {
        char buf[64] = {0};
        if (FILE* f = fopen(ZS_THEME_FILE, "r")) {
            if (fgets(buf, sizeof(buf), f)) buf[strcspn(buf, "\r\n")] = '\0';
            fclose(f);
            idx = theme_find(buf);
        }
    }
    theme_set(idx < 0 ? 0 : idx, false);
}

static void apply_zonistation_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 8.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 8.0f;
    s.GrabRounding      = 4.0f;
    s.TabRounding       = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 1.0f;
    s.TabBorderSize     = 1.0f;
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(10, 5);
    s.ItemSpacing       = ImVec2(10, 7);
    s.ItemInnerSpacing  = ImVec2(7, 5);
    s.CellPadding       = ImVec2(8, 4);
    s.GrabMinSize       = 10.0f;
    s.ScrollbarSize     = 12.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    apply_theme_colors();
}

/* Everything colour-related in ImGui's style, derived from the theme's tokens.
 * Kept apart from the sizes so a theme can change at runtime without undoing
 * the display scale that ScaleAllSizes applied after startup. */
static void apply_theme_colors() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ZS_TEXT;
    c[ImGuiCol_TextDisabled]         = ZS_FAINT;
    c[ImGuiCol_WindowBg]             = ZS_PANEL;
    c[ImGuiCol_ChildBg]              = zs_mix(ZS_GROUND, ZS_PANEL, 0.90f, 0.95f);
    c[ImGuiCol_PopupBg]              = ZS_PANEL2;
    c[ImGuiCol_Border]               = ZS_LINE;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = zs_mix(ZS_GROUND, ZS_PANEL, 0.70f);
    c[ImGuiCol_FrameBgHovered]       = zs_mix(ZS_PANEL2, ZS_LINE, 0.70f);
    c[ImGuiCol_FrameBgActive]        = ZS_LINE;
    c[ImGuiCol_TitleBg]              = ZS_GROUND;
    c[ImGuiCol_TitleBgActive]        = zs_mix(ZS_PANEL2, ZS_LINE, 0.30f);
    c[ImGuiCol_TitleBgCollapsed]     = ZS_GROUND;
    c[ImGuiCol_MenuBarBg]            = ZS_PANEL;
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ZS_LINE;
    c[ImGuiCol_ScrollbarGrabHovered] = ZS_MUTED;
    c[ImGuiCol_ScrollbarGrabActive]  = ZS_DATA;
    c[ImGuiCol_CheckMark]            = ZS_DATA;
    c[ImGuiCol_SliderGrab]           = ZS_DATA;
    c[ImGuiCol_SliderGrabActive]     = ZS_DATA;
    c[ImGuiCol_Button]               = zs_mix(ZS_PANEL2, ZS_LINE, 0.50f);
    c[ImGuiCol_ButtonHovered]        = zs_mix(ZS_LINE, ZS_MUTED, 0.15f);
    c[ImGuiCol_ButtonActive]         = ZS_LINE;
    c[ImGuiCol_Header]               = zs_mix(ZS_PANEL2, ZS_LINE, 0.55f);
    c[ImGuiCol_HeaderHovered]        = zs_mix(ZS_LINE, ZS_MUTED, 0.20f);
    c[ImGuiCol_HeaderActive]         = ZS_LINE;
    c[ImGuiCol_Separator]            = ZS_LINE;
    c[ImGuiCol_SeparatorHovered]     = ZS_DATA;
    c[ImGuiCol_SeparatorActive]      = ZS_DATA;
    c[ImGuiCol_ResizeGrip]           = ZS_LINE;
    c[ImGuiCol_ResizeGripHovered]    = ZS_MUTED;
    c[ImGuiCol_ResizeGripActive]     = ZS_DATA;
    c[ImGuiCol_Tab]                  = zs_mix(ZS_GROUND, ZS_PANEL, 0.90f);
    c[ImGuiCol_TabHovered]           = zs_mix(ZS_LINE, ZS_MUTED, 0.10f);
    c[ImGuiCol_TabActive]            = zs_mix(ZS_PANEL2, ZS_LINE, 0.60f);
    c[ImGuiCol_TabUnfocused]         = zs_mix(ZS_GROUND, ZS_PANEL, 0.50f);
    c[ImGuiCol_TabUnfocusedActive]   = zs_mix(ZS_PANEL, ZS_PANEL2, 0.70f);
    c[ImGuiCol_DockingPreview]       = ImVec4(ZS_DATA.x, ZS_DATA.y, ZS_DATA.z, 0.35f);
    c[ImGuiCol_DockingEmptyBg]       = ZS_GROUND;
    c[ImGuiCol_TableHeaderBg]        = zs_mix(ZS_PANEL, ZS_PANEL2, 0.50f);
    c[ImGuiCol_TableBorderStrong]    = ZS_LINE;
    c[ImGuiCol_TableBorderLight]     = ZS_LINE_SOFT;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = COLA(0xFF, 0xFF, 0xFF, 0.02f);
    c[ImGuiCol_NavHighlight]         = ZS_DATA;
}

// A tight uppercase letter-spaced micro-label — identity carried by density,
// the way the mockup does it (no shipped webfont).
static void micro_label(const char* text) {
    /* Uppercase, and letter-spaced only while it stays a label. The spacing is
     * what carries the identity without a shipped font, but doubling every
     * character turns a sentence-length header into a line that no longer fits
     * its panel — so past ~30 characters the label is set plain. */
    char up[256];
    size_t len = strlen(text);
    bool  spaced = len <= 30;
    size_t n = 0;
    for (const char* p = text; *p && n < sizeof(up) - 2; ++p) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        up[n++] = ch;
        if (spaced && p[1]) up[n++] = ' ';
    }
    up[n] = '\0';
    if (g_font_h1) ImGui::PushFont(g_font_h1);
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextUnformatted(up);
    ImGui::PopStyleColor();
    if (g_font_h1) ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Resolution-Independent Vector Icon Primitives (OpenGL ImDrawList)
// Render sharp 2D vector icons directly in the OpenGL pipeline
// ---------------------------------------------------------------------------

static void draw_icon_play(ImDrawList* dl, ImVec2 p, float size, ImU32 col) {
    ImVec2 a = ImVec2(p.x, p.y);
    ImVec2 b = ImVec2(p.x + size, p.y + size * 0.5f);
    ImVec2 c = ImVec2(p.x, p.y + size);
    dl->AddTriangleFilled(a, b, c, col);
}

static void draw_icon_pause(ImDrawList* dl, ImVec2 p, float size, ImU32 col) {
    float w = size * 0.35f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + size), col, 1.0f);
    dl->AddRectFilled(ImVec2(p.x + size - w, p.y), ImVec2(p.x + size, p.y + size), col, 1.0f);
}

static void draw_icon_step(ImDrawList* dl, ImVec2 p, float size, ImU32 col) {
    float w = size * 0.6f;
    draw_icon_play(dl, p, w, col);
    dl->AddRectFilled(ImVec2(p.x + w + 2.0f, p.y), ImVec2(p.x + size, p.y + size), col, 1.0f);
}

static void draw_icon_gamepad(ImDrawList* dl, ImVec2 p, float size, ImU32 col) {
    float h = size * 0.65f;
    dl->AddRect(p, ImVec2(p.x + size, p.y + h), col, 3.0f, 0, 1.5f);
    float r = size * 0.08f;
    dl->AddCircleFilled(ImVec2(p.x + size * 0.25f, p.y + h * 0.5f), r, col);
    dl->AddCircleFilled(ImVec2(p.x + size * 0.75f, p.y + h * 0.5f), r, col);
}

static void draw_ps_triangle(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    ImVec2 a = ImVec2(center.x, center.y - radius);
    ImVec2 b = ImVec2(center.x + radius * 0.866f, center.y + radius * 0.5f);
    ImVec2 c = ImVec2(center.x - radius * 0.866f, center.y + radius * 0.5f);
    dl->AddTriangle(a, b, c, col, thickness);
}

static void draw_ps_square(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    float r = radius * 0.7f;
    dl->AddRect(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), col, 0.0f, 0, thickness);
}

static void draw_ps_cross(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    float r = radius * 0.65f;
    dl->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), col, thickness);
    dl->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), col, thickness);
}

static void draw_ps_circle(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    dl->AddCircle(center, radius * 0.65f, col, 16, thickness);
}

static void draw_ps_chip(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    float r = radius * 0.6f;
    dl->AddRect(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), col, 2.0f, 0, thickness);
    dl->AddLine(ImVec2(center.x - r * 0.5f, center.y - r - 2.0f), ImVec2(center.x - r * 0.5f, center.y - r), col, thickness);
    dl->AddLine(ImVec2(center.x + r * 0.5f, center.y - r - 2.0f), ImVec2(center.x + r * 0.5f, center.y - r), col, thickness);
    dl->AddLine(ImVec2(center.x - r * 0.5f, center.y + r), ImVec2(center.x - r * 0.5f, center.y + r + 2.0f), col, thickness);
    dl->AddLine(ImVec2(center.x + r * 0.5f, center.y + r), ImVec2(center.x + r * 0.5f, center.y + r + 2.0f), col, thickness);
}

static void draw_ps_terminal(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thickness) {
    float r = radius * 0.55f;
    dl->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x, center.y), col, thickness);
    dl->AddLine(ImVec2(center.x, center.y), ImVec2(center.x - r, center.y + r), col, thickness);
    dl->AddLine(ImVec2(center.x + 2.0f, center.y + r), ImVec2(center.x + r + 2.0f, center.y + r), col, thickness);
}

// --- Panel icons -----------------------------------------------------------
// Drawn rather than fonted: an icon font would be another file to ship and
// another licence line in THIRD-PARTY.md for nine glyphs. All of them take the
// same (centre, radius) shape as the PS symbols above so they are
// interchangeable in a header.

static void draw_icon_disc(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    dl->AddCircle(c, r, col, 20, th);
    dl->AddCircle(c, r * 0.28f, col, 12, th);
}

static void draw_icon_cpu(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    float a = r * 0.62f;
    dl->AddRect(ImVec2(c.x - a, c.y - a), ImVec2(c.x + a, c.y + a), col, 1.5f, 0, th);
    dl->AddRect(ImVec2(c.x - a * 0.4f, c.y - a * 0.4f), ImVec2(c.x + a * 0.4f, c.y + a * 0.4f), col, 0.0f, 0, th);
    for (int i = -1; i <= 1; i++) {
        float o = a * 0.55f * (float)i;
        dl->AddLine(ImVec2(c.x + o, c.y - a - r * 0.3f), ImVec2(c.x + o, c.y - a), col, th);
        dl->AddLine(ImVec2(c.x + o, c.y + a), ImVec2(c.x + o, c.y + a + r * 0.3f), col, th);
        dl->AddLine(ImVec2(c.x - a - r * 0.3f, c.y + o), ImVec2(c.x - a, c.y + o), col, th);
        dl->AddLine(ImVec2(c.x + a, c.y + o), ImVec2(c.x + a + r * 0.3f, c.y + o), col, th);
    }
}

static void draw_icon_gpu(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    float w = r * 0.95f, h = r * 0.62f;
    dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), col, 1.5f, 0, th);
    dl->AddCircle(ImVec2(c.x - w * 0.35f, c.y), h * 0.55f, col, 12, th);
    dl->AddLine(ImVec2(c.x + w * 0.2f, c.y - h * 0.4f), ImVec2(c.x + w * 0.7f, c.y - h * 0.4f), col, th);
    dl->AddLine(ImVec2(c.x + w * 0.2f, c.y + h * 0.15f), ImVec2(c.x + w * 0.7f, c.y + h * 0.15f), col, th);
}

static void draw_icon_wave(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    ImVec2 pts[9];
    for (int i = 0; i < 9; i++) {
        float x = c.x - r + (2.0f * r) * (float)i / 8.0f;
        float y = c.y - sinf((float)i * 0.9f) * r * 0.62f;
        pts[i] = ImVec2(x, y);
    }
    dl->AddPolyline(pts, 9, col, 0, th);
}

static void draw_icon_ram(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    float w = r * 0.95f, h = r * 0.55f;
    dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), col, 1.0f, 0, th);
    for (int i = -2; i <= 2; i++)
        dl->AddLine(ImVec2(c.x + w * 0.33f * (float)i, c.y - h * 0.45f),
                    ImVec2(c.x + w * 0.33f * (float)i, c.y + h * 0.45f), col, th);
}

static void draw_icon_threads(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    for (int i = 0; i < 3; i++) {
        float y = c.y - r * 0.55f + r * 0.55f * (float)i;
        float w = r * (0.95f - 0.22f * (float)i);
        dl->AddLine(ImVec2(c.x - r, y), ImVec2(c.x - r + 2.0f * w, y), col, th * 1.4f);
    }
}

static void draw_icon_pin(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    dl->AddCircle(ImVec2(c.x, c.y - r * 0.25f), r * 0.5f, col, 14, th);
    dl->AddLine(ImVec2(c.x, c.y + r * 0.25f), ImVec2(c.x, c.y + r), col, th);
}

static void draw_icon_screen(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    float w = r * 0.9f, h = r * 0.68f;
    dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h * 0.55f), col, 1.5f, 0, th);
    dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r), ImVec2(c.x + r * 0.45f, c.y + r), col, th);
    dl->AddLine(ImVec2(c.x, c.y + h * 0.55f), ImVec2(c.x, c.y + r), col, th);
}

static void draw_icon_clock(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    dl->AddCircle(c, r * 0.85f, col, 18, th);
    dl->AddLine(c, ImVec2(c.x, c.y - r * 0.5f), col, th);
    dl->AddLine(c, ImVec2(c.x + r * 0.42f, c.y), col, th);
}

static void draw_icon_flow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r * 0.5f, c.y), col, th);
    dl->AddLine(ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x, c.y - r * 0.5f), col, th);
    dl->AddLine(ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x, c.y + r * 0.5f), col, th);
}

static void draw_icon_pad(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    float w = r * 0.95f, h = r * 0.55f;
    dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), col, h, 0, th);
    dl->AddLine(ImVec2(c.x - w * 0.55f, c.y), ImVec2(c.x - w * 0.15f, c.y), col, th);
    dl->AddLine(ImVec2(c.x - w * 0.35f, c.y - h * 0.45f), ImVec2(c.x - w * 0.35f, c.y + h * 0.45f), col, th);
    dl->AddCircleFilled(ImVec2(c.x + w * 0.35f, c.y - h * 0.2f), th * 0.9f, col);
    dl->AddCircleFilled(ImVec2(c.x + w * 0.6f,  c.y + h * 0.2f), th * 0.9f, col);
}

typedef void (*ZsIconFn)(ImDrawList*, ImVec2, float, ImU32, float);

// ---------------------------------------------------------------------------
// Machine bar identity + live vitals (set from the main loop each frame)
// ---------------------------------------------------------------------------

static char   g_bios_name[64] = "n/a";
static char   g_disc_name[96] = "n/a";
static double g_vit_frame_ms  = 0.0;   /* raw, this loop iteration */
static double g_vit_frame_ema = 0.0;   /* smoothed, what the panels show */
static double g_vit_budget_ms = 0.0;
static int    g_vit_aq        = 0;
static int    g_vit_aq_target = 2048;
static double g_vit_drift     = 0.0;

/* The last ZS_FT_SAMPLES raw loop times, oldest at g_ft_head, for the gameplay
 * HUD's frame-time graph. Raw on purpose: the smoothed figure hides exactly
 * the hitches a graph is there to show. */
#define ZS_FT_SAMPLES 120
static float g_ft[ZS_FT_SAMPLES];
static int   g_ft_head = 0;

/* Fields the machine actually presented per real second.
 *
 * The rate was being read as 1000 / frame_ms of a single loop iteration, and
 * that number is not a frame rate: with an audio device open the pacing loop
 * waits in SDL_Delay(1) steps until the SPU ring drains, so one iteration lands
 * at 16 ms and the next at 24 ms around the same 20 ms mean. The instantaneous
 * reciprocal then swings between 42 and 62 while the machine is keeping perfect
 * PAL time — which is what "60 fps on a PAL BIOS" was.
 *
 * Counting VBlanks over half a second answers the question that was actually
 * being asked: how many fields reached the screen in the last real second. */
static double field_rate_hz(Interconnect* inter) {
    static uint32_t prev_fields = 0;
    static double   prev_t      = 0.0;
    static double   rate        = 0.0;
    static bool     init        = false;
    if (!inter) return 0.0;

    double t = ImGui::GetTime();
    uint32_t f = inter->field_count;
    if (!init) { prev_fields = f; prev_t = t; init = true; return 0.0; }
    double dt = t - prev_t;
    if (dt >= 0.5) {
        rate = (double)(uint32_t)(f - prev_fields) / dt;
        prev_fields = f;
        prev_t = t;
    }
    return rate;
}

/* The machine's own nominal refresh, from the CRTC's current video mode. */
static double nominal_hz(Interconnect* inter) {
    if (!inter) return 0.0;
    uint32_t cpf = gpu_cycles_per_frame(&inter->gpu);
    return cpf ? (double)PSX_SYSCLK_HZ / (double)cpf : 0.0;
}

extern "C" void debug_ui_set_machine_info(const char* bios_name, const char* disc_name) {
    snprintf(g_bios_name, sizeof(g_bios_name), "%s", bios_name ? bios_name : "n/a");
    snprintf(g_disc_name, sizeof(g_disc_name), "%s", disc_name ? disc_name : "n/a");
}

extern "C" void debug_ui_set_vitals(double frame_ms, double budget_ms,
                                    int audio_queue, int audio_target, double drift_pct) {
    g_vit_frame_ms  = frame_ms;
    g_ft[g_ft_head] = (float)frame_ms;
    g_ft_head = (g_ft_head + 1) % ZS_FT_SAMPLES;
    /* Smoothed for display: the pacing wait quantises a single iteration to the
     * millisecond, so the raw value is unreadable even when the mean is right. */
    g_vit_frame_ema = (g_vit_frame_ema <= 0.0) ? frame_ms : g_vit_frame_ema * 0.9 + frame_ms * 0.1;
    g_vit_budget_ms = budget_ms;
    g_vit_aq        = audio_queue;
    g_vit_aq_target = audio_target > 0 ? audio_target : 2048;
    g_vit_drift     = drift_pct;
} static void card_header(const char* title, ImVec4 dot); /* fwd */

// ---------------------------------------------------------------------------
// View modes — the mode rail replaces the scattered floating windows
// ---------------------------------------------------------------------------

static const ImVec4 ZS_PS_GREEN  = COL(0x00, 0xA8, 0x96); // PS Logo Teal/Green
static const ImVec4 ZS_PS_BLUE   = COL(0x29, 0x79, 0xFF); // PS Logo Royal Blue
static const ImVec4 ZS_PS_RED    = COL(0xE5, 0x25, 0x21); // PS Logo Red
static const ImVec4 ZS_PS_PINK   = COL(0xFF, 0x2D, 0x55); // PS Circle Magenta
static const ImVec4 ZS_PS_YELLOW = COL(0xFA, 0xBC, 0x05); // PS Logo Gold/Yellow
static const ImVec4 ZS_SONY_AMBER = COL(0xE8, 0x60, 0x00); // Sony Computer Entertainment Diamond Orange

enum ViewMode {
    MODE_PIPELINE = 0, MODE_DISPLAY, MODE_FRAME, MODE_CODE,
    MODE_MEMORY, MODE_AUDIO, MODE_VRAM, MODE_HOST, MODE_SCRIPT, MODE_COUNT
};

struct ModeDef { const char* name; const char* key; bool audio; const char* symbol; ImVec4 symbol_col; };
static const ModeDef g_modes[MODE_COUNT] = {
    { "Pipeline", "F1", false, "TRI", ZS_PS_GREEN },
    { "Display",  "F2", false, "SQR", ZS_PS_BLUE  },
    { "Frame",    "F3", false, "CRS", ZS_PS_RED   },
    { "Code",     "F4", false, "CIR", ZS_PS_PINK  },
    { "Memory",   "F5", false, "SQR", ZS_PS_BLUE  },
    { "Audio",    "F6", true,  "CIR", ZS_PS_PINK  },
    { "VRAM",     "F7", false, "TRI", ZS_PS_GREEN },
    { "Host HW",  "F8", false, "SYS", ZS_PS_YELLOW},
    { "Script",   "F9", false, "CMD", COL(0xF1, 0xF5, 0xF9) },
};

static int  g_mode        = MODE_PIPELINE;
static bool g_layout_dirty = true;   // rebuild dock layout on next frame

// ---------------------------------------------------------------------------
// Log subsystem
// ---------------------------------------------------------------------------

struct LogEntry {
    int level;
    std::string message;
};

/* Lines kept per category. Entries beyond this are dropped oldest-first. */
#define LOG_RING_CAP 5000u

/* One category's window and its backlog.
 *
 * The backlog is a ring, not a vector that gets trimmed. It used to be
 * `buffer.push_back(...)` followed by `buffer.erase(buffer.begin())` once past
 * the cap, and erasing at the front of a vector shifts every remaining element
 * — 4999 std::string moves per line, forever, once the cap is reached. Log
 * ingestion was therefore O(n) per line rather than O(1), and a DEBUG run emits
 * ~1.4M lines. That is the likely reason the debug interface could not hold real
 * time while the emulation core could with the panels closed.
 *
 * Entries are addressed by a monotonic sequence number rather than a position,
 * so the filtered view below can keep referring to them as the ring wraps:
 * seq/CAP gives the slot, first_seq..next_seq is what is still live.
 *
 * `shown` is the filtered view, maintained incrementally. Rebuilding it meant
 * testing every held line against the level and the text filter on every frame,
 * for every open window — and all seventeen windows open at startup, so ~85000
 * string tests per frame that ImGuiListClipper does not save, because it only
 * skips the drawing. Now only lines that arrived since the last frame are
 * tested, and the whole list is rebuilt only when the filter or level changes. */
struct LogComponent {
    const char* name;
    LogCategory category;
    bool is_open;
    bool auto_scroll;
    bool monospace;
    int  display_level;          /* min level shown in ImGui (default INFO) */
    std::vector<LogEntry> ring;  /* LOG_RING_CAP slots, indexed by seq % CAP */
    uint64_t first_seq;          /* oldest live entry */
    uint64_t next_seq;           /* sequence the next entry will take */
    ImGuiTextFilter filter;
    FILE* file;                  /* always-open file in logs/<Name>.log */
    uint32_t writes_since_flush;
    std::deque<uint64_t> shown;  /* seqs passing level+filter, ascending */
    uint64_t shown_scanned;      /* next_seq as of the last incremental scan */
    int shown_level;             /* level the view was built for */
    std::string shown_filter;    /* filter text the view was built for */
};

static inline LogEntry& log_at(LogComponent& c, uint64_t seq) {
    return c.ring[(size_t)(seq % LOG_RING_CAP)];
}
static inline const LogEntry& log_at(const LogComponent& c, uint64_t seq) {
    return c.ring[(size_t)(seq % LOG_RING_CAP)];
}

/* Indexed by LogCategory, not keyed by it.
 *
 * This was a std::map<LogCategory, LogComponent>, so every log line paid a
 * red-black tree descent to reach a slot addressed by a dense enum of 0..16.
 * An array is one index, contiguous, and the sink runs on every line at every
 * level. Order must follow the enum in log.h; the ordering is asserted at
 * startup rather than trusted. */
static std::vector<LogComponent> make_log_components() {
    static const char* const names[LOG_CAT_COUNT] = {
        "System", "CPU", "IRQ", "DMA", "GPU", "CDROM", "Timer", "BIOS",
        "Interconnect", "Renderer", "Event", "GTE", "VRAM", "RAM", "Debug",
        "MDEC", "SPU"
    };
    std::vector<LogComponent> v(LOG_CAT_COUNT);
    for (int i = 0; i < LOG_CAT_COUNT; i++) {
        LogComponent& c = v[(size_t)i];
        c.name          = names[i];
        c.category      = (LogCategory)i;
        c.is_open       = true;
        c.auto_scroll   = true;
        c.monospace     = true;
        c.display_level = LOG_LEVEL_DEBUG;
        c.ring.resize(LOG_RING_CAP);
        c.first_seq     = 0;
        c.next_seq      = 0;
        c.file          = nullptr;
        c.writes_since_flush = 0;
        c.shown_scanned = 0;
        c.shown_level   = -1;        /* forces the first build */
    }
    return v;
}

static std::vector<LogComponent> g_log_components = make_log_components();

static std::mutex g_log_mutex;

static const char* level_name(int level);  /* fwd */

static void log_sink_callback(int category, int level, const char* msg, void* udata) {
    (void)udata;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (category < 0 || category >= LOG_CAT_COUNT) return;
    LogComponent& comp = g_log_components[(size_t)category];

    /* O(1): overwrite the slot the sequence maps to and let the oldest fall off
     * the back of the window. No element ever moves. */
    LogEntry& slot = log_at(comp, comp.next_seq);
    slot.level = level;
    slot.message = msg;
    comp.next_seq++;
    if (comp.next_seq - comp.first_seq > LOG_RING_CAP)
        comp.first_seq = comp.next_seq - LOG_RING_CAP;

    /* No flush here. The file is fully buffered (see debug_ui_init) and
     * debug_ui_flush_logs() empties every buffer once per field, so a burst of
     * DEBUG lines costs one write() per 64 KB instead of one per line. The
     * file used to be line-buffered, which made every emitted line a syscall
     * on the emulation thread. writes_since_flush only tells the per-field
     * flush which files have anything to write. */
    if (comp.file) {
        fprintf(comp.file, "[%s] %s\n", level_name(level), msg);
        comp.writes_since_flush++;
    }
}

/* Called once per field from the host loop (main.c), after the frame has been
 * submitted: the logs on disk are at most one field behind the machine, which
 * is the granularity anyone reading them can use, and the crash handlers in
 * main.c flush whatever is still buffered on the way out. Takes the same lock
 * the sink does, so a line the GPU thread is writing is never cut in half. */
extern "C" void debug_ui_flush_logs(void) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    for (auto& comp : g_log_components) {
        if (comp.file && comp.writes_since_flush) {
            fflush(comp.file);
            comp.writes_since_flush = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Window visibility flags
// ---------------------------------------------------------------------------

static bool g_show_display     = true;
static bool g_show_disasm      = true;
static bool g_show_registers   = true;
static bool g_show_breakpoints = false;
static bool g_show_spu         = false;
static bool g_show_vram_viewer = true;
static bool g_show_lua_console = false;

// ---------------------------------------------------------------------------
// Disasm state
// ---------------------------------------------------------------------------

static uint32_t g_disasm_view_addr    = 0xBFC00000;
static char     g_goto_addr_buf[12]   = "";
static bool     g_scroll_to_pc        = false;

// Step edge: set by UI, consumed once by main loop via debug_ui_step_requested()

extern "C" bool debug_ui_step_requested(void) {
    bool v = g_step_pending;
    g_step_pending = false;
    return v;
}

// ---------------------------------------------------------------------------
// MIPS register names
// ---------------------------------------------------------------------------

static const char* s_gpr_names[32] = {
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0",   "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8",   "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool dbg_has_bp(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; i++)
        if (dbg->breakpoints[i] == addr) return true;
    return false;
}

static bool dbg_bp_enabled(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; i++)
        if (dbg->breakpoints[i] == addr) return dbg->bp_enabled[i];
    return false;
}

static void dbg_toggle_bp(Debugger* dbg, uint32_t addr) {
    for (uint32_t i = 0; i < dbg->breakpoint_count; i++) {
        if (dbg->breakpoints[i] == addr) {
            dbg->bp_enabled[i] = !dbg->bp_enabled[i];
            return;
        }
    }
    debugger_add_breakpoint(dbg, addr);
}

// ---------------------------------------------------------------------------
// Log export
// ---------------------------------------------------------------------------

static const char* level_name(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_TRACE: return "TRACE";
        default:              return "?????";
    }
}

/* Snapshot the visible ring buffer to its OWN file.
 *
 * This used to write `logs/<Name>.log` — the exact path debug_ui_init() keeps
 * open for the whole session and streams every line into. One name, two
 * independent FILE objects: the snapshot opened it with "w" and truncated it
 * while the streaming handle carried on writing at its old offset, so the file
 * ended up as a block of NULs followed by interleaved text, and the snapshot
 * itself was overwritten by the next streamed line. The streamed file is the
 * log; the snapshot is a separate artefact and now says so in its name.
 *
 * The handle is flushed first, so the two files line up at the moment of the
 * snapshot instead of the streamed one trailing by up to a field's lines. */
static void export_component_log(LogComponent& comp) {
    mkdir("logs", 0755);
    char path[160];
    snprintf(path, sizeof(path), "logs/%s.snapshot.log", comp.name);
    FILE* f = fopen(path, "w");
    if (!f) return;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (comp.file) { fflush(comp.file); comp.writes_since_flush = 0; }
    for (uint64_t s = comp.first_seq; s < comp.next_seq; s++) {
        const LogEntry& e = log_at(comp, s);
        fprintf(f, "[%s] %s\n", level_name(e.level), e.message.c_str());
    }
    fclose(f);
}

static void export_all_logs() {
    for (auto& comp : g_log_components)
        export_component_log(comp);
}

// Log window
// ---------------------------------------------------------------------------

static void draw_component_log_window(LogComponent& comp) {
    if (!comp.is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(comp.name, &comp.is_open)) { ImGui::End(); return; }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        comp.first_seq = comp.next_seq;   /* drop the backlog without touching the ring */
        comp.shown.clear();
        comp.shown_scanned = comp.next_seq;
    }
    ImGui::SameLine();
    if (ImGui::Button("Flush")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (comp.file) { fflush(comp.file); comp.writes_since_flush = 0; }
    }
    ImGui::SameLine();
    if (ImGui::Button("Snapshot")) export_component_log(comp);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &comp.auto_scroll);
    ImGui::SameLine();
    ImGui::Checkbox("Mono", &comp.monospace);
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    const char* lvl_items[] = { "Silent", "Error", "Warn", "Info", "Debug", "Trace" };
    ImGui::Combo("UI Lvl", &comp.display_level, lvl_items, IM_ARRAYSIZE(lvl_items));
    ImGui::SameLine();
    comp.filter.Draw("Filter", -100.0f);

    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (comp.monospace && g_font_mono) ImGui::PushFont(g_font_mono);
    if (copy) ImGui::LogToClipboard();

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);

        /* Maintain the filtered view instead of rebuilding it.
         *
         * The old code tested every held line against the level and the text
         * filter on every frame and allocated a fresh index vector — for each of
         * the seventeen windows, all of which are open by default. The clipper
         * below skips the *drawing* of off-screen lines but not that scan.
         * Now a change of filter or level rebuilds, and otherwise only the lines
         * that arrived since the last frame are examined. */
        const char* ftext = comp.filter.InputBuf;
        if (comp.shown_level != comp.display_level || comp.shown_filter != ftext) {
            comp.shown_level  = comp.display_level;
            comp.shown_filter = ftext;
            comp.shown.clear();
            comp.shown_scanned = comp.first_seq;
        }
        /* Lines that fell out of the ring since last frame. */
        if (comp.shown_scanned < comp.first_seq) comp.shown_scanned = comp.first_seq;
        while (!comp.shown.empty() && comp.shown.front() < comp.first_seq)
            comp.shown.pop_front();

        for (uint64_t s = comp.shown_scanned; s < comp.next_seq; s++) {
            const LogEntry& e = log_at(comp, s);
            if (e.level > comp.display_level) continue;
            if (!comp.filter.PassFilter(e.message.c_str())) continue;
            comp.shown.push_back(s);
        }
        comp.shown_scanned = comp.next_seq;

        ImGuiListClipper clipper;
        clipper.Begin((int)comp.shown.size());
        while (clipper.Step()) {
            for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++) {
                const auto& e = log_at(comp, comp.shown[(size_t)n]);
                ImVec4 col = {1,1,1,1};
                if      (e.level == LOG_LEVEL_ERROR) col = {1.0f, 0.4f, 0.4f, 1.0f};
                else if (e.level == LOG_LEVEL_WARN)  col = {1.0f, 0.8f, 0.4f, 1.0f};
                else if (e.level == LOG_LEVEL_DEBUG) col = {0.4f, 0.8f, 1.0f, 1.0f};
                else if (e.level == LOG_LEVEL_TRACE) col = {0.6f, 0.6f, 0.6f, 1.0f};
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::TextUnformatted(e.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        clipper.End();
    }

    if (copy) ImGui::LogFinish();
    if (comp.monospace && g_font_mono) ImGui::PopFont();
    if (comp.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// CPU Registers window
// ---------------------------------------------------------------------------

static void draw_registers_window(Cpu* cpu) {
    if (!g_show_registers) return;

    ImGui::SetNextWindowSize(ImVec2(280, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CPU Registers", &g_show_registers)) { ImGui::End(); return; }

    if (!cpu) { ImGui::TextDisabled("No CPU instance."); ImGui::End(); return; }

    card_header("MIPS R3000A CPU State", ZS_PS_YELLOW);

    // PC / COP0 Special Registers
    if (ImGui::BeginTable("cop0regs", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto reg_kv = [](const char* name, uint32_t val, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(name); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::Text("%08X", val); ImGui::PopStyleColor();
        };
        reg_kv("PC  Program Counter", cpu->current_pc, ZS_WARN);
        reg_kv("SR  Status Register", cpu->sr, ZS_TEXT);
        reg_kv("Cause Exception", cpu->cause, ZS_CRIT);
        reg_kv("EPC Exception PC", cpu->epc, ZS_DATA);
        reg_kv("HI  Mult High", cpu->hi, ZS_TEXT);
        reg_kv("LO  Mult Low", cpu->lo, ZS_TEXT);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 6));
    card_header("32 General Purpose Registers", ZS_DATA);

    // GPR table: 2 columns
    if (ImGui::BeginTable("gpr", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        for (int i = 0; i < 32; i++) {
            ImGui::TableNextColumn();
            uint32_t val = cpu->regs[i];
            if (val != 0)
                ImGui::TextColored(ZS_DATA, "$%-4s %08X", s_gpr_names[i], val);
            else
                ImGui::TextDisabled("$%-4s %08X", s_gpr_names[i], val);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Breakpoints window (PCSX-Redux style)
// ---------------------------------------------------------------------------

static char g_add_bp_buf[12] = "";

static void draw_breakpoints_window(Interconnect* inter) {
    if (!g_show_breakpoints) return;

    ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Breakpoints", &g_show_breakpoints)) { ImGui::End(); return; }

    Debugger* dbg = &inter->debugger;

    if (dbg->breakpoint_count > 0) {
        static ImGuiTableFlags tflags =
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_BordersV | ImGuiTableFlags_RowBg;

        if (ImGui::BeginTable("bptable", 4, tflags)) {
            ImGui::TableSetupColumn("#",      ImGuiTableColumnFlags_WidthFixed, 24.0f);
            ImGui::TableSetupColumn("Address",ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int to_remove = -1;
            for (uint32_t i = 0; i < dbg->breakpoint_count; i++) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", (int)i);

                ImGui::TableNextColumn();
                // Clicking the address jumps disasm there
                char label[32];
                snprintf(label, sizeof(label), "%08X##bp%d", dbg->breakpoints[i], (int)i);
                if (ImGui::Button(label)) {
                    g_disasm_view_addr = dbg->breakpoints[i];
                    g_disasm_follow_pc = false;
                    g_show_disasm = true;
                }

                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                ImGui::Checkbox("##en", &dbg->bp_enabled[i]);
                ImGui::PopID();

                ImGui::TableNextColumn();
                ImGui::PushID((int)(i + 1000));
                if (ImGui::SmallButton("Delete")) to_remove = (int)i;
                ImGui::PopID();
            }
            ImGui::EndTable();

            if (to_remove >= 0)
                debugger_remove_breakpoint(dbg, dbg->breakpoints[to_remove]);
        }
    } else {
        ImGui::TextDisabled("No breakpoints.");
    }

    ImGui::Separator();
    ImGui::Text("Add:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputText("##bpaddr", g_add_bp_buf, sizeof(g_add_bp_buf),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("Add BP")) {
        char* end;
        uint32_t addr = (uint32_t)strtoul(g_add_bp_buf, &end, 16);
        if (end != g_add_bp_buf && *end == '\0')
            debugger_add_breakpoint(dbg, addr);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Disassembly window
// ---------------------------------------------------------------------------

static void draw_disasm_window(Cpu* cpu, Interconnect* inter) {
    if (!g_show_disasm) return;

    ImGui::SetNextWindowSize(ImVec2(480, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Disassembly", &g_show_disasm)) { ImGui::End(); return; }

    if (!cpu || !inter) { ImGui::TextDisabled("Not initialized"); ImGui::End(); return; }

    Debugger* dbg = &inter->debugger;

    // --- Toolbar ---
    bool paused = dbg->paused;

    if (!paused) {
        if (ImGui::Button("Pause")) dbg->paused = true;
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(" Run  ")) { dbg->paused = false; g_disasm_follow_pc = true; }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Step")) { g_step_pending = true; }
        ImGui::SameLine();
        if (ImGui::Button("Go to PC")) {
            g_disasm_view_addr = cpu->current_pc;
            g_scroll_to_pc = true;
        }
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
    ImGui::Text("  PC: %08X", cpu->current_pc);
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 20);
    ImGui::Checkbox("Follow PC", &g_disasm_follow_pc);

    ImGui::Separator();

    // Tab bar: Live Disasm | Exec Trace
    if (ImGui::BeginTabBar("##disasm_tabs")) {

    if (ImGui::BeginTabItem("Live Disasm")) {

    // Auto-follow PC when running
    if (!dbg->paused && g_disasm_follow_pc)
        g_disasm_view_addr = (cpu->current_pc > 32 * 4) ? (cpu->current_pc - 32 * 4) : 0;

    // Disasm list — show 128 instructions starting from g_disasm_view_addr
    const int ROWS = 128;
    const float footer_h = ImGui::GetFrameHeightWithSpacing() + 8.0f;
    ImGui::BeginChild("##disasmlist", ImVec2(0.0f, -footer_h), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(ROWS);

    // Scroll to PC row when requested
    if (g_scroll_to_pc || (!dbg->paused && g_disasm_follow_pc)) {
        uint32_t pc_row = (cpu->current_pc >= g_disasm_view_addr)
                          ? (cpu->current_pc - g_disasm_view_addr) / 4 : 0;
        if (pc_row < (uint32_t)ROWS) {
            float row_height = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(pc_row * row_height - ImGui::GetWindowHeight() * 0.4f);
        }
        g_scroll_to_pc = false;
    }

    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            uint32_t addr = g_disasm_view_addr + (uint32_t)(row * 4);
            uint32_t raw  = interconnect_load32(inter, addr);
            const char* dis = disassemble_mips(raw, addr);

            bool is_pc = (addr == cpu->current_pc);
            bool has_bp = dbg_has_bp(dbg, addr);
            bool bp_en  = has_bp && dbg_bp_enabled(dbg, addr);

            // Row highlight: draw background rect directly into window draw list
            if (is_pc) {
                ImVec2 rmin = ImGui::GetCursorScreenPos();
                rmin.x = ImGui::GetWindowPos().x + ImGui::GetScrollX();
                ImVec2 rmax = { rmin.x + ImGui::GetWindowWidth(),
                                rmin.y + ImGui::GetTextLineHeightWithSpacing() };
                ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, IM_COL32(70, 70, 0, 130));
            } else if (has_bp && bp_en) {
                ImVec2 rmin = ImGui::GetCursorScreenPos();
                rmin.x = ImGui::GetWindowPos().x + ImGui::GetScrollX();
                ImVec2 rmax = { rmin.x + ImGui::GetWindowWidth(),
                                rmin.y + ImGui::GetTextLineHeightWithSpacing() };
                ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, IM_COL32(70, 0, 0, 100));
            }

            // BP marker (clickable dot)
            ImGui::PushID(row);
            if (has_bp) {
                ImVec4 dot_col = bp_en ? ImVec4(1.0f,0.25f,0.25f,1.0f)
                                       : ImVec4(0.5f,0.25f,0.25f,1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, dot_col);
                if (ImGui::SmallButton("●")) dbg_toggle_bp(dbg, addr);
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f,0.3f,0.3f,1.0f));
                if (ImGui::SmallButton("·")) debugger_add_breakpoint(dbg, addr);
                ImGui::PopStyleColor();
            }
            ImGui::PopID();

            ImGui::SameLine();

            // Arrow + address + disasm
            if (is_pc) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,1.0f,0.2f,1.0f));
                ImGui::Text("▶ %08X  %08X  %s", addr, raw, dis);
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("  %08X  %08X  %s", addr, raw, dis);
            }
        }
    }
    clipper.End();
    ImGui::EndChild();

    // Footer: Go-to address
    ImGui::Separator();
    ImGui::Text("Go to:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputText("##goto", g_goto_addr_buf, sizeof(g_goto_addr_buf),
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        char* end;
        uint32_t addr = (uint32_t)strtoul(g_goto_addr_buf, &end, 16);
        if (end != g_goto_addr_buf) {
            g_disasm_view_addr = addr & ~3u;
            g_disasm_follow_pc = false;
            g_scroll_to_pc = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        char* end;
        uint32_t addr = (uint32_t)strtoul(g_goto_addr_buf, &end, 16);
        if (end != g_goto_addr_buf) {
            g_disasm_view_addr = addr & ~3u;
            g_disasm_follow_pc = false;
            g_scroll_to_pc = true;
        }
    }

    ImGui::EndTabItem(); // Live Disasm

    } // BeginTabItem "Live Disasm"

    // -------------------------------------------------------------------------
    // Exec Trace tab — ring buffer of last EXEC_TRACE_SIZE executed instructions
    // -------------------------------------------------------------------------
    if (ImGui::BeginTabItem("Exec Trace")) {
        uint32_t count = cpu->exec_trace_count;
        uint32_t head  = cpu->exec_trace_head;
        uint32_t start = (head - count) & (EXEC_TRACE_SIZE - 1);

        // Buttons row
        if (ImGui::Button("Dump to file")) {
            cpu_dump_exec_trace(cpu, "logs/exec_trace.log");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%u entries)", count);
        ImGui::Separator();

        ImGui::BeginChild("##tracelist", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        ImGuiListClipper tc;
        tc.Begin((int)count);
        while (tc.Step()) {
            for (int row = tc.DisplayStart; row < tc.DisplayEnd; row++) {
                uint32_t idx = (start + (uint32_t)row) & (EXEC_TRACE_SIZE - 1);
                uint32_t pc  = cpu->exec_trace_pc[idx];
                uint32_t ins = cpu->exec_trace_instr[idx];
                const char* dis = disassemble_mips(ins, pc);
                bool is_last = ((uint32_t)row == count - 1);

                if (is_last) {
                    ImVec2 rmin = ImGui::GetCursorScreenPos();
                    rmin.x = ImGui::GetWindowPos().x;
                    ImVec2 rmax = { rmin.x + ImGui::GetWindowWidth(),
                                    rmin.y + ImGui::GetTextLineHeightWithSpacing() };
                    ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, IM_COL32(80, 20, 0, 180));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                    ImGui::Text(">>> [%4u] %08X  %08X  %s", row, pc, ins, dis ? dis : "?");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TextDisabled("    [%4u] %08X  %08X  %s", row, pc, ins, dis ? dis : "?");
                }
            }
        }
        tc.End();

        // Auto-scroll to bottom (most recent instruction)
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::EndTabItem(); // Exec Trace
    }

    ImGui::EndTabBar();
    } // BeginTabBar

    ImGui::End();
}

// ---------------------------------------------------------------------------
// SPU Debug window
// ---------------------------------------------------------------------------

static const char* s_adsr_state_names[] = {
    "Attack", "Decay", "Sustain", "Release", "Stopped"
};

static void draw_spu_debug_window(Spu* spu) {
    if (!g_show_spu || !spu) return;

    ImGui::SetNextWindowSize(ImVec2(540, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SPU Debug", &g_show_spu)) { ImGui::End(); return; }

    card_header("SPU 24-Voice Synthesizer Engine", ZS_AUDIO);

    if (ImGui::CollapsingHeader("Audio Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Peak L: %d  Peak R: %d", spu->peak_level_left, spu->peak_level_right);

        const float meter_w = 200.0f;
        const float meter_h = 16.0f;
        float max_peak = 32767.0f;

        ImGui::Text("L: "); ImGui::SameLine();
        float level_l = (float)spu->peak_level_left / max_peak;
        ImGui::ProgressBar(level_l, ImVec2(meter_w, meter_h), "");

        ImGui::Text("R: "); ImGui::SameLine();
        float level_r = (float)spu->peak_level_right / max_peak;
        ImGui::ProgressBar(level_r, ImVec2(meter_w, meter_h), "");

        ImGui::Separator();
        ImGui::Text("Buffer: %d / %d samples", spu->sample_buf_count, SPU_SAMPLE_BUFFER_SIZE);
        ImGui::Text("Total generated: %u", spu->total_samples_generated);
        ImGui::Text("Key-On events: %u", spu->total_key_on_events);
        ImGui::Text("Control: 0x%04X  Status: 0x%04X", spu->control, spu->status);
        ImGui::Text("Main Vol: L=%d R=%d", spu->main_vol_left, spu->main_vol_right);
        ImGui::Text("Muted: %s", spu->muted ? "Yes" : "No");
        ImGui::Text("SPU Enabled: %s", (spu->control & SPU_CTRL_ENABLE) ? "Yes" : "No");
    }

    if (ImGui::CollapsingHeader("Voices (24)", ImGuiTreeNodeFlags_DefaultOpen)) {
        static ImGuiTableFlags flags =
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("spu_voices", 7, flags, ImVec2(0, 250))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("Vol L", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Vol R", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Env", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableHeadersRow();

            for (int v = 0; v < NUM_VOICES; v++) {
                SpuVoice* voice = &spu->voices[v];
                bool active = voice->on;

                if (!active && !ImGui::GetIO().KeyShift) continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", v);

                ImGui::TableNextColumn();
                if (active)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_adsr_state_names[voice->adsr_state & 0x7]);
                else
                    ImGui::TextDisabled("Off");

                ImGui::TableNextColumn();
                ImGui::Text("%u", voice->pitch);

                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", voice->volume_left);

                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", voice->volume_right);

                ImGui::TableNextColumn();
                ImGui::Text("%d", voice->adsr_volume);

                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", voice->start_address);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Hold Shift to show inactive voices");
    }

    if (ImGui::CollapsingHeader("Transfer / DMA")) {
        const char* mode_names[] = { "Stopped", "Manual Write", "DMA Write", "DMA Read" };
        int mode = (spu->control >> 4) & 0x03;
        ImGui::Text("Transfer mode: %s", mode_names[mode]);
        ImGui::Text("Transfer addr: 0x%04X (reg)  0x%06X (byte)", spu->transfer_addr_reg, spu->transfer_addr);
        ImGui::Text("IRQ addr: 0x%04X  IRQ flag: %s", spu->irq_addr, spu->irq9_flag ? "Yes" : "No");
    }

    if (ImGui::CollapsingHeader("Reverb")) {
        ImGui::Text("Base: 0x%04X  Enabled: %s", spu->reverb_base,
                    (spu->control & SPU_CTRL_REVERB_ENABLE) ? "Yes" : "No");
        ImGui::Text("Vol L: %d  Vol R: %d", spu->reverb_vol_left, spu->reverb_vol_right);
        ImGui::Text("Current addr: %u", spu->reverb_current_addr);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Lua Console window
// ---------------------------------------------------------------------------

static void draw_lua_console_window(void) {
    if (!g_show_lua_console) return;

    ImGui::SetNextWindowSize(ImVec2(640, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Lua Console", &g_show_lua_console)) { ImGui::End(); return; }

    static char path_buf[256] = "scripts/mdec_handoff.lua";
    ImGui::SetNextItemWidth(-140.0f);
    ImGui::InputText("##script_path", path_buf, sizeof(path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Load & Run", ImVec2(120, 0))) {
        lua_debug_run_file(path_buf);
    }

    ImGui::Separator();

    static char scratch[4096] = "print(\"hello from lua\")";
    ImGui::TextUnformatted("Script (inline):");
    ImGui::InputTextMultiline("##scratch", scratch, sizeof(scratch), ImVec2(-1, 140));
    if (ImGui::Button("Run", ImVec2(80, 0))) {
        lua_debug_run_string(scratch);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Console")) {
        lua_debug_console_clear();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Console:");
    ImGui::BeginChild("##lua_console_out", ImVec2(0, 0), true,
                       ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(lua_debug_console_text());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// VRAM Viewer window
// ---------------------------------------------------------------------------

// VRAM viewer state — mirrors PCSX-Redux's vram-viewer widget controls.
static VramViewParams g_vram_view = { VRAM_VIEW_16BPP, 0, false, false, 0, 0, false };
static float  g_vram_zoom     = 1.0f;
static ImVec2 g_vram_pan      = ImVec2(0, 0);   // in VRAM pixels, top-left of view
static bool   g_vram_grid     = false;          // 16x16 pixel grid
static bool   g_vram_tpage    = true;           // 64x256 texture-page grid
static bool   g_vram_magnify  = false;
static float  g_vram_mag_amt  = 6.0f;
static float  g_vram_mag_rad  = 120.0f;
static bool   g_vram_show_disp = true;          // outline the active display area

bool debug_ui_vram_viewer_open(void) { return g_show_vram_viewer; }

/* Whether the viewer was drawn this frame. The renderer runs its decode pass (a
 * 1024x512 fullscreen draw on the GPU thread) only while the params it is
 * handed say enabled, so this frame's answer travels with the params: true from
 * draw_vram_viewer_window(), false from vram_view_publish_hidden() on every
 * frame that did not draw it, the gameplay shell included. Pushed every frame
 * rather than on change, so a backend that has just been switched in cannot
 * keep a stale "enabled" from before the switch. */
static bool g_vram_viewer_drawn = false;

static void vram_view_publish_hidden(Interconnect* inter) {
    if (!inter || g_vram_viewer_drawn) return;
    g_vram_view.enabled = false;
    renderer_set_vram_view_params(&inter->gpu.renderer, &g_vram_view);
}

static void draw_vram_viewer_window(Renderer* renderer, Interconnect* inter) {
    if (!g_show_vram_viewer || !renderer) return;

    g_vram_view.enabled = true;
    g_vram_viewer_drawn = true;
    renderer_set_vram_view_params(renderer, &g_vram_view);
    GfxTexHandle tex = renderer_get_vram_viewer_texture(renderer);

    ImGui::SetNextWindowSize(ImVec2(1060, 620), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("VRAM Viewer", &g_show_vram_viewer)) {
        card_header("PSX VRAM 1MB Framebuffer (1024x512)", ZS_DATA);
        // --- Toolbar -------------------------------------------------------
        const char* modes[] = { "4 bpp (CLUT)", "8 bpp (CLUT)", "16 bpp", "24 bpp" };
        int mode = (int)g_vram_view.mode;
        ImGui::SetNextItemWidth(130);
        if (ImGui::Combo("##mode", &mode, modes, IM_ARRAYSIZE(modes)))
            g_vram_view.mode = (VramViewMode)mode;

        if (g_vram_view.mode == VRAM_VIEW_24BPP) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(90);
            ImGui::SliderInt("shift", &g_vram_view.shift24, 0, 3);
        }
        if (g_vram_view.mode == VRAM_VIEW_4BPP || g_vram_view.mode == VRAM_VIEW_8BPP) {
            int cx = g_vram_view.clut_x, cy = g_vram_view.clut_y;
            ImGui::SameLine(); ImGui::SetNextItemWidth(70);
            if (ImGui::DragInt("clut x", &cx, 1, 0, 1023)) g_vram_view.clut_x = (uint16_t)cx;
            ImGui::SameLine(); ImGui::SetNextItemWidth(70);
            if (ImGui::DragInt("clut y", &cy, 1, 0, 511))  g_vram_view.clut_y = (uint16_t)cy;
        }

        ImGui::SameLine(); ImGui::Checkbox("Grey", &g_vram_view.greyscale);
        ImGui::SameLine(); ImGui::Checkbox("Mask", &g_vram_view.show_alpha);
        ImGui::SameLine(); ImGui::Checkbox("Grid", &g_vram_grid);
        ImGui::SameLine(); ImGui::Checkbox("TPage", &g_vram_tpage);
        ImGui::SameLine(); ImGui::Checkbox("Display", &g_vram_show_disp);
        ImGui::SameLine(); ImGui::Checkbox("Magnify", &g_vram_magnify);

        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("zoom", &g_vram_zoom, 0.25f, 8.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button("Fit")) { g_vram_zoom = 0.0f; g_vram_pan = ImVec2(0, 0); }
        if (g_vram_magnify) {
            ImGui::SameLine(); ImGui::SetNextItemWidth(90);
            ImGui::SliderFloat("lens", &g_vram_mag_amt, 2.0f, 16.0f, "%.0fx");
        }

        // --- Image ---------------------------------------------------------
        // Everything below is guarded rather than early-returned: this runs
        // inside the DockHost's Begin/End pair, so bailing out of the function
        // here would leave ImGui's window stack unbalanced (which hangs the
        // frame instead of failing loudly).
        ImVec2 avail = ImGui::GetContentRegionAvail();
        bool drawable = (avail.x >= 16.0f && avail.y >= 16.0f && tex != 0);
        if (!tex) ImGui::TextDisabled("VRAM not ready");
        if (drawable) {
        if (g_vram_zoom <= 0.0f) g_vram_zoom = avail.x / 1024.0f;   // "Fit" request

        // Visible VRAM rect, derived from zoom/pan and clamped inside VRAM.
        float view_w = avail.x / g_vram_zoom;
        float view_h = avail.y / g_vram_zoom;
        if (view_w > 1024.0f) view_w = 1024.0f;
        if (view_h > 512.0f)  view_h = 512.0f;
        if (g_vram_pan.x > 1024.0f - view_w) g_vram_pan.x = 1024.0f - view_w;
        if (g_vram_pan.y > 512.0f  - view_h) g_vram_pan.y = 512.0f  - view_h;
        if (g_vram_pan.x < 0) g_vram_pan.x = 0;
        if (g_vram_pan.y < 0) g_vram_pan.y = 0;

        ImVec2 img_size = ImVec2(view_w * g_vram_zoom, view_h * g_vram_zoom);
        ImVec2 origin   = ImGui::GetCursorScreenPos();

        // No v flip: unlike display_texture (an FBO the GL rasterizer writes in
        // y-up clip space), the viewer texture is a straight CPU upload of VRAM
        // rows 0..511 in order, so v=0 already is VRAM y=0. The old viewer
        // passed (0,1)-(1,0) here and showed all of VRAM upside down.
        ImVec2 uv0 = ImVec2(g_vram_pan.x / 1024.0f, g_vram_pan.y / 512.0f);
        ImVec2 uv1 = ImVec2((g_vram_pan.x + view_w) / 1024.0f,
                            (g_vram_pan.y + view_h) / 512.0f);
        ImGui::Image((void*)(intptr_t)tex, img_size, uv0, uv1);

        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(origin, ImVec2(origin.x + img_size.x, origin.y + img_size.y), true);

        // Texture-page grid (64 halfwords wide, 256 tall — the GP0 tpage unit).
        if (g_vram_tpage) {
            const ImU32 col = IM_COL32(230, 230, 230, 90);
            for (float x = 0; x <= 1024.0f; x += 64.0f) {
                float sx = origin.x + (x - g_vram_pan.x) * g_vram_zoom;
                if (sx >= origin.x && sx <= origin.x + img_size.x)
                    dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + img_size.y), col);
            }
            for (float y = 0; y <= 512.0f; y += 256.0f) {
                float sy = origin.y + (y - g_vram_pan.y) * g_vram_zoom;
                if (sy >= origin.y && sy <= origin.y + img_size.y)
                    dl->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + img_size.x, sy), col);
            }
        }
        // Fine pixel grid — only legible once a VRAM pixel is several screen px.
        if (g_vram_grid && g_vram_zoom >= 3.0f) {
            const ImU32 col = IM_COL32(128, 128, 128, 70);
            for (float x = floorf(g_vram_pan.x); x <= g_vram_pan.x + view_w; x += 1.0f) {
                float sx = origin.x + (x - g_vram_pan.x) * g_vram_zoom;
                dl->AddLine(ImVec2(sx, origin.y), ImVec2(sx, origin.y + img_size.y), col);
            }
            for (float y = floorf(g_vram_pan.y); y <= g_vram_pan.y + view_h; y += 1.0f) {
                float sy = origin.y + (y - g_vram_pan.y) * g_vram_zoom;
                dl->AddLine(ImVec2(origin.x, sy), ImVec2(origin.x + img_size.x, sy), col);
            }
        }
        // Active display area, straight from CRTC state.
        if (g_vram_show_disp && inter) {
            float dx = (float)inter->gpu.crtc.display_vram_x;
            float dy = (float)inter->gpu.crtc.display_vram_y;
            float dw = (float)inter->gpu.crtc.display_width;
            float dh = (float)inter->gpu.crtc.display_height;
            /* 24bpp packs 3 bytes per pixel, so the area covers fewer VRAM
             * halfword columns than it has pixels. */
            if (inter->gpu.display_depth == D24Bits) dw = dw * 3.0f / 2.0f;
            ImVec2 p0 = ImVec2(origin.x + (dx - g_vram_pan.x) * g_vram_zoom,
                               origin.y + (dy - g_vram_pan.y) * g_vram_zoom);
            ImVec2 p1 = ImVec2(p0.x + dw * g_vram_zoom, p0.y + dh * g_vram_zoom);
            dl->AddRect(p0, p1, IM_COL32(255, 200, 0, 200), 0.0f, 0, 2.0f);
        }
        dl->PopClipRect();

        // --- Interaction ---------------------------------------------------
        if (hovered) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 m = io.MousePos;
            float vx = g_vram_pan.x + (m.x - origin.x) / g_vram_zoom;
            float vy = g_vram_pan.y + (m.y - origin.y) / g_vram_zoom;
            int ix = (int)vx, iy = (int)vy;

            // Wheel zooms about the cursor so the pixel under it stays put.
            if (io.MouseWheel != 0.0f) {
                float old = g_vram_zoom;
                g_vram_zoom *= (io.MouseWheel > 0) ? 1.25f : 0.8f;
                if (g_vram_zoom < 0.25f) g_vram_zoom = 0.25f;
                if (g_vram_zoom > 32.0f) g_vram_zoom = 32.0f;
                if (g_vram_zoom != old) {
                    g_vram_pan.x = vx - (m.x - origin.x) / g_vram_zoom;
                    g_vram_pan.y = vy - (m.y - origin.y) / g_vram_zoom;
                }
            }
            // Drag pans.
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                g_vram_pan.x -= d.x / g_vram_zoom;
                g_vram_pan.y -= d.y / g_vram_zoom;
            }
            // Right-click picks the CLUT position for the indexed modes.
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                ix >= 0 && ix < 1024 && iy >= 0 && iy < 512) {
                g_vram_view.clut_x = (uint16_t)ix;
                g_vram_view.clut_y = (uint16_t)iy;
            }

            if (g_vram_magnify) {
                ImGui::BeginTooltip();
                float r = g_vram_mag_rad;
                float span = r / (g_vram_zoom * g_vram_mag_amt);  // VRAM px shown
                ImVec2 c0 = ImVec2((vx - span) / 1024.0f, (vy - span) / 512.0f);
                ImVec2 c1 = ImVec2((vx + span) / 1024.0f, (vy + span) / 512.0f);
                ImGui::Image((void*)(intptr_t)tex, ImVec2(r * 2, r * 2), c0, c1);
                ImGui::EndTooltip();
            }

            // Exact pixel readout, straight from the CPU-side VRAM buffer —
            // no GL readback, so it is the real emulated value.
            if (inter && ix >= 0 && ix < 1024 && iy >= 0 && iy < 512) {
                const uint16_t* v = (const uint16_t*)inter->gpu.vram.data;
                uint16_t raw = v[(size_t)iy * 1024 + ix];
                const uint8_t* bytes = (const uint8_t*)v;
                size_t boff = ((size_t)iy * 1024 + ix) * 2;
                ImGui::Text("(%4d,%3d)  raw=0x%04X  555=(%2u,%2u,%2u) mask=%u  bytes=%02X %02X"
                            "  24bpp=(%3u,%3u,%3u)  tpage=(%d,%d)",
                            ix, iy, raw,
                            (unsigned)(raw & 0x1F), (unsigned)((raw >> 5) & 0x1F),
                            (unsigned)((raw >> 10) & 0x1F), (unsigned)((raw >> 15) & 1),
                            bytes[boff], bytes[boff + 1],
                            bytes[boff], bytes[boff + 1],
                            (boff + 2 < 1024u * 512u * 2u) ? bytes[boff + 2] : 0,
                            ix / 64, iy / 256);
            }
        } else {
            ImGui::Text("view=(%.0f,%.0f) %.0fx%.0f @ %.2fx   —   wheel: zoom, drag: pan, "
                        "right-click: set CLUT",
                        g_vram_pan.x, g_vram_pan.y, view_w, view_h, g_vram_zoom);
        }
        }  // if (drawable)
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// PS1 Display window
// ---------------------------------------------------------------------------

// Draw the emulated screen into `avail`, letterboxed at 4:3 and placed by the
// GP1(07) scanline range. Both shells call this: the debug stage puts it in a
// dock node, the gameplay shell gives it the whole window.
static void draw_scanout(GfxTexHandle texture_id, Interconnect* inter, ImVec2 avail) {
    {
        if (texture_id && inter) {
            uint16_t vw = inter->gpu.crtc.display_width  > 0 ? inter->gpu.crtc.display_width  : 320;
            uint16_t vh = inter->gpu.crtc.display_height > 0 ? inter->gpu.crtc.display_height : 240;

            texture_id = renderer_get_scanout_texture(&inter->gpu.renderer);
            float u0 = 0.0f, u1 = (float)vw / 1024.0f;
            float v0 = 0.0f, v1 = (float)vh / 512.0f;

            // Letterbox at 4:3 — PSX always outputs to a 4:3 TV regardless of pixel count.
            const float psx_aspect = 4.0f / 3.0f;
            float disp_w, disp_h;
            if (avail.x / avail.y > psx_aspect) {
                disp_h = avail.y;
                disp_w = disp_h * psx_aspect;
            } else {
                disp_w = avail.x;
                disp_h = disp_w / psx_aspect;
            }
            float pad_x = (avail.x - disp_w) * 0.5f;
            float pad_y = (avail.y - disp_h) * 0.5f;

            /* The scale is a property of the TV, not of the current range.
             *
             * GP1(07) gives Y1/Y2 as scanline numbers relative to VSYNC, around a
             * middle scanline that DOCS/graphicsprocessingunitgpu.md:705 fixes at
             * 88h (NTSC) / A3h (PAL); :717-719 gives what a set actually shows
             * around it — 224 of the 240 NTSC lines, and 256 for PAL, the rest
             * being overscan or border. Filling the window with whatever Y2-Y1
             * happens to be instead made the scale follow the range, so every
             * change resized the whole picture: this game walks 240 -> 254 -> 236
             * -> 239 -> 240 lines during boot and each step looked like the image
             * jumping. Mapping the visible window to the window instead keeps one
             * scanline the same size always; a narrower range then shows a border
             * and a moved range moves, which is what the hardware does — and is
             * what makes GP1(07) screen shake (:720-722) come out as a shake. */
            const bool pal        = (inter->gpu.vmode == Pal);
            const int  visible_h  = pal ? 256  : 224;
            const int  raster_mid = pal ? 0xA3 : 0x88;
            const int  visible_top = raster_mid - visible_h / 2;

            const int line_start = (int)inter->gpu.display_line_start;
            /* How tall the picture is *on screen* is the GP1(07) scanline range;
             * how many texture rows carry it is vh. The two are not the same
             * number — 480i puts 480 rows into the same 240 scanlines — so the
             * placement is done in scanlines and the texture is sampled by the
             * matching fraction. Using vh for both drew a 480-row frame at twice
             * the height and ran it off the bottom of the window. */
            int range = (int)inter->gpu.display_line_end - line_start;
            if (range <= 0) range = (int)vh > 0 ? (int)vh : 240;

            /* Lines above the visible window are lost to overscan, as on a set. */
            int skip = visible_top - line_start;
            if (skip < 0) skip = 0;
            if (skip > range) skip = range;

            const float line_h  = disp_h / (float)visible_h;
            const float top_off = (float)(line_start + skip - visible_top) * line_h;
            const float img_h   = (float)(range - skip) * line_h;

            if (img_h > 0.0f) {
                v0 = (float)vh * ((float)skip / (float)range) / 512.0f;
                ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad_x,
                                           ImGui::GetCursorPosY() + pad_y + top_off));
                ImGui::Image((void*)(intptr_t)texture_id, ImVec2(disp_w, img_h),
                             ImVec2(u0, v0), ImVec2(u1, v1));
            }
        } else if (texture_id) {
            // Fallback: show entire FBO Y-flipped
            ImGui::Image((void*)(intptr_t)texture_id, avail, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::TextDisabled("Display not ready");
        }
    }
}

static void gp_row(const char* k, const char* v, ImVec4 col);

/* The scanout is 4:3 and the panel is wide, so most of its width was black
 * letterbox. When there is room for it the spare width carries the numbers
 * that describe what is being scanned out, which the Display mode used to make
 * you read off the Pipeline cards. */
static void draw_display_info(Interconnect* inter, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLA(0x0F, 0x14, 0x1F, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * g_ui_scale, 10.0f * g_ui_scale));
    ImGui::BeginChild("##dispinfo", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    micro_label("Scanout");
    if (inter && ImGui::BeginTable("dispinfo_t", 2, ImGuiTableFlags_SizingStretchProp)) {
        char b[64];
        const Gpu& g = inter->gpu;
        snprintf(b, sizeof(b), "%ux%u%s", g.crtc.display_width, g.crtc.display_height,
                 g.interlaced ? " i" : "");
        gp_row("Size", b, ZS_TEXT);
        snprintf(b, sizeof(b), "%s", g.display_depth == D24Bits ? "24 bpp" : "15 bpp");
        gp_row("Depth", b, ZS_TEXT);
        snprintf(b, sizeof(b), "%s  %.2f Hz", g.vmode == Pal ? "PAL" : "NTSC", nominal_hz(inter));
        gp_row("Mode", b, ZS_TEXT);
        snprintf(b, sizeof(b), "%u, %u", g.display_vram_x_start, g.display_vram_y_start);
        gp_row("VRAM origin", b, ZS_DATA);
        snprintf(b, sizeof(b), "%u - %u", g.display_line_start, g.display_line_end);
        gp_row("Lines", b, ZS_DATA);
        snprintf(b, sizeof(b), "%u", inter->field_count);
        gp_row("Field", b, ZS_FAINT);
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

static void draw_ps1_display(GfxTexHandle texture_id, Interconnect* inter) {
    if (!g_show_display) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("PS1 Display", &g_show_display,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        const float side_w = 260.0f * g_ui_scale;
        const float need_w = avail.y * (4.0f / 3.0f) + side_w + 24.0f * g_ui_scale;
        if (avail.x >= need_w && avail.y >= 230.0f * g_ui_scale) {   /* six rows need ~230 px */
            /* The picture takes the left part at its 4:3 width; the info
             * column takes the rest, capped so it does not stretch. */
            float pic_w = avail.y * (4.0f / 3.0f);
            float info_w = avail.x - pic_w;
            if (info_w > side_w * 1.6f) info_w = side_w * 1.6f;
            ImVec2 origin = ImGui::GetCursorPos();
            draw_scanout(texture_id, inter, ImVec2(avail.x - info_w, avail.y));
            ImGui::SetCursorPos(ImVec2(origin.x + avail.x - info_w, origin.y));
            draw_display_info(inter, ImVec2(info_w, avail.y));
        } else {
            draw_scanout(texture_id, inter, avail);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Machine bar — BIOS / disc / PC and the live vitals, always on screen.
// The numbers previously obtained by running a Lua script.
// ---------------------------------------------------------------------------

// A vital: uppercase micro-label, a tabular value, a thin fill bar underneath.
static void vital(const char* label, const char* value, ImVec4 vcol,
                  float frac, ImVec4 barcol) {
    ImGui::BeginGroup();
    micro_label(label);
    ImGui::PushStyleColor(ImGuiCol_Text, vcol);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = 84.0f, h = 3.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ZS_LINE_SOFT), 2.0f);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    dl->AddRectFilled(p, ImVec2(p.x + w * frac, p.y + h), ImGui::GetColorU32(barcol), 2.0f);
    ImGui::Dummy(ImVec2(w, h + 2.0f));
    ImGui::EndGroup();
}

/* Width a chip will take, so the machine bar can decide what fits before it
 * draws instead of running off the right edge of the window. */
static float chip_width(const char* label, const char* value) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s  %s", label, value);
    return ImGui::CalcTextSize(buf).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

static void chip(const char* label, const char* value) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s  %s", label, value);
    ImGui::PushStyleColor(ImGuiCol_Button, ZS_DOCK_BG);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ZS_DOCK_BG);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ZS_DOCK_BG);
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::Button(buf);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

/* The machine bar has room for a pill, not for a marketing string. These trim
 * the kernel's CPU model and the GL renderer down to what identifies the part:
 * "Intel(R) Core(TM) i9-14900HX" -> "Core i9-14900HX", and
 * "NVIDIA GeForce RTX 4060 Laptop GPU/PCIe/SSE2" -> "GeForce RTX 4060 Laptop GPU". */
static const char* host_short_cpu(void) {
    static char out[96];
    const HostInfo* h = host_info_get();
    const char* src = h->cpu_model;
    char tmp[96]; size_t o = 0;
    for (size_t i = 0; src[i] && o < sizeof(tmp) - 1; i++) {
        if (strncmp(src + i, "(R)", 3) == 0 || strncmp(src + i, "(TM)", 4) == 0) {
            i += (src[i + 1] == 'R') ? 2 : 3;
            continue;
        }
        if (strncmp(src + i, " CPU @", 6) == 0) break;
        tmp[o++] = src[i];
    }
    tmp[o] = '\0';
    const char* p = tmp;
    if (strncmp(p, "Intel ", 6) == 0) p += 6;
    else if (strncmp(p, "AMD ", 4) == 0) p += 4;
    snprintf(out, sizeof(out), "%s", p[0] ? p : "unknown CPU");
    return out;
}

static const char* host_short_gpu(void) {
    static char out[128];
    const HostInfo* h = host_info_get();
    const char* src = h->gl_renderer;
    if (strncmp(src, "NVIDIA ", 7) == 0) src += 7;
    snprintf(out, sizeof(out), "%s", src);
    char* slash = strchr(out, '/');
    if (slash) *slash = '\0';
    char* paren = strchr(out, '(');
    if (paren && paren > out) *(paren - 1) = '\0';
    return out;
}

static void draw_machine_bar(Cpu* cpu, Interconnect* inter) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, zs_mix(ZS_GROUND, ZS_PANEL, 0.85f));
    ImGui::BeginChild("##MachineBar", ImVec2(0, 52), false, ImGuiWindowFlags_NoScrollbar);

    // Multi-color linear gradient background across top header
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + 52.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImU32 hi = ImGui::GetColorU32(zs_mix(ZS_PANEL2, ZS_LINE, 0.45f));
        ImU32 lo = ImGui::GetColorU32(zs_mix(ZS_GROUND, ZS_PANEL, 0.60f));
        dl->AddRectFilledMultiColor(p0, p1, hi, lo, lo, hi);
    }
    dl->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f), ImGui::GetColorU32(ZS_LINE), 1.0f);

    // Brand + video-mode subtitle
    bool pal = inter && inter->gpu.vmode == Pal;
    double hz = 0.0;
    if (inter) {
        uint32_t cpf = gpu_cycles_per_frame(&inter->gpu);
        if (cpf) hz = (double)PSX_SYSCLK_HZ / (double)cpf;
    }
    /* The dock host has zero window padding, so without this the brand sat flush
     * against the window's left edge, and the chips against its top. */
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 12.0f * g_ui_scale,
                               ImGui::GetCursorPosY() + 9.0f * g_ui_scale));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::TextUnformatted("ZoniStation One [PSX]");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::Text("%s  %.2f Hz", pal ? "PAL" : "NTSC", hz);
    ImGui::PopStyleColor();

    /* What fits is decided from measurements, in priority order: the controls
     * and the vitals are what the bar is for, then BIOS/disc/PC, and the host
     * pills go first when the window is narrow. Before this the row was drawn
     * unconditionally and the vitals were pushed past the right edge. */
    const float sp      = ImGui::GetStyle().ItemSpacing.x;
    const float win_w   = ImGui::GetWindowWidth();
    const float ctl_w   = 4.0f * 120.0f * g_ui_scale;     /* pause/run, menu, controls, gameplay */
    const float vit_w   = 5.0f * 96.0f * g_ui_scale;
    char pcbuf[16];
    snprintf(pcbuf, sizeof(pcbuf), "0x%08X", cpu ? cpu->current_pc : 0);
    float used  = ImGui::GetCursorPosX() + ImGui::CalcTextSize("ZoniStation One [PSX]").x +
                  ImGui::CalcTextSize("PAL  50.00 Hz").x + sp + 18.0f;
    float room  = win_w - used - ctl_w - vit_w - 16.0f;
    float w_bios = chip_width("BIOS", g_bios_name) + sp;
    float w_disc = chip_width("DISC", g_disc_name) + sp;
    float w_pc   = chip_width("PC", pcbuf) + sp;
    float w_hcpu = chip_width("HOST CPU", host_short_cpu()) + sp;
    float w_hgpu = chip_width("HOST GPU", host_short_gpu()) + sp;
    bool show_bios = room >= w_bios;                          if (show_bios) room -= w_bios;
    bool show_pc   = room >= w_pc;                            if (show_pc)   room -= w_pc;
    bool show_disc = room >= w_disc;                          if (show_disc) room -= w_disc;
    bool show_hcpu = room >= w_hcpu + 14.0f;                  if (show_hcpu) room -= w_hcpu + 14.0f;
    bool show_hgpu = show_hcpu && room >= w_hgpu;             if (show_hgpu) room -= w_hgpu;

    bool first = true;
    auto next = [&](float gap) { if (first) ImGui::SameLine(0, 18); else ImGui::SameLine(0, gap); first = false; };
    if (show_bios) { next(sp); chip("BIOS", g_bios_name); }
    if (show_disc) { next(sp); chip("DISC", g_disc_name); }
    if (show_pc)   { next(sp); chip("PC", pcbuf); }
    // Host HW pills — read from the kernel and the GL context, not typed in.
    if (show_hcpu) { next(14); chip("HOST CPU", host_short_cpu()); }
    if (show_hgpu) { next(sp); chip("HOST GPU", host_short_gpu()); }

    // Controls: direct Play / Pause / Step buttons + popup
    Debugger* dbg = inter ? &inter->debugger : nullptr;
    if (first) ImGui::SameLine(0, 18); else ImGui::SameLine(0, 12);

    auto draw_icon_btn = [](const char* label, const char* btn_id, void(*icon_fn)(ImDrawList*, ImVec2, float, ImU32), ImVec4 btn_col) -> bool {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        float text_w = ImGui::CalcTextSize(label).x;
        float total_w = text_w + 30.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, btn_col);
        bool res = ImGui::Button(btn_id, ImVec2(total_w, h));
        ImGui::PopStyleColor();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (icon_fn) {
            icon_fn(dl, ImVec2(p0.x + 8.0f, p0.y + (h - 10.0f) * 0.5f), 10.0f, IM_COL32(255, 255, 255, 255));
        }
        float ty = p0.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p0.x + 22.0f, ty), IM_COL32(241, 245, 249, 255), label);
        return res;
    };

    if (dbg) {
        if (!dbg->paused) {
            if (draw_icon_btn("Pause F10", "##pause_btn", draw_icon_pause, COLA(0x38, 0xB0, 0x00, 0.25f))) {
                dbg->paused = true;
            }
        } else {
            if (draw_icon_btn("Run F10", "##run_btn", draw_icon_play, COLA(0x00, 0x82, 0x98, 0.35f))) {
                dbg->paused = false;
                g_disasm_follow_pc = true;
            }
            ImGui::SameLine();
            if (draw_icon_btn("Step F11", "##step_btn", draw_icon_step, COLA(0xE4, 0x3B, 0x44, 0.35f))) {
                g_step_pending = true;
            }
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("Menu")) ImGui::OpenPopup("##ctrl");
    ImGui::SameLine();
    if (draw_icon_btn("Controls", "##ctrl_btn", draw_icon_gamepad, COLA(0x16, 0x20, 0x30, 0.6f))) {
        g_show_controller_mapping = true;
    }
    ImGui::SameLine();
    if (draw_icon_btn("Gameplay  `", "##shell_btn", draw_icon_play, COLA(0x00, 0x82, 0x98, 0.35f))) {
        g_shell = SHELL_GAMEPLAY;
    }

    if (dbg && dbg->paused) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
        ImGui::TextUnformatted("[PAUSED]");
        ImGui::PopStyleColor();
    }
    if (ImGui::BeginPopup("##ctrl")) {
        if (dbg) {
            if (!dbg->paused) {
                if (ImGui::MenuItem("Pause", "F10")) dbg->paused = true;
            } else {
                if (ImGui::MenuItem("Run",  "F10")) { dbg->paused = false; g_disasm_follow_pc = true; }
                if (ImGui::MenuItem("Step", "F11"))   g_step_pending = true;
            }
            ImGui::Separator();
        }
        extern LogLevel current_log_level;
        int level = (int)current_log_level;
        micro_label("Log level");
        if (ImGui::RadioButton("Silent", level == LOG_LEVEL_SILENT)) log_set_level(LOG_LEVEL_SILENT);
        if (ImGui::RadioButton("Error",  level == LOG_LEVEL_ERROR))  log_set_level(LOG_LEVEL_ERROR);
        if (ImGui::RadioButton("Warn",   level == LOG_LEVEL_WARN))   log_set_level(LOG_LEVEL_WARN);
        if (ImGui::RadioButton("Info",   level == LOG_LEVEL_INFO))   log_set_level(LOG_LEVEL_INFO);
        if (ImGui::RadioButton("Debug",  level == LOG_LEVEL_DEBUG))  log_set_level(LOG_LEVEL_DEBUG);
        if (ImGui::RadioButton("Trace",  level == LOG_LEVEL_TRACE))  log_set_level(LOG_LEVEL_TRACE);
        ImGui::Separator();
        micro_label("Theme");
        for (int i = 0; i < ZS_THEME_COUNT; i++)
            if (ImGui::RadioButton(g_themes[i].name, g_theme == i)) theme_set(i, true);
        ImGui::Separator();
        micro_label("Logs");
        if (ImGui::MenuItem("Open all"))  for (auto& c : g_log_components) c.is_open = true;
        if (ImGui::MenuItem("Close all")) for (auto& c : g_log_components) c.is_open = false;
        if (ImGui::MenuItem("Snapshot all to logs/")) export_all_logs();
        ImGui::EndPopup();
    }

    // Vitals, right-aligned
    float vitals_w = 5 * 96.0f * g_ui_scale;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > vitals_w) ImGui::SameLine(0, avail - vitals_w);
    else ImGui::SameLine();

    // Speed: fields presented per real second against the mode's nominal rate.
    // Not 1000/frame_ms — see field_rate_hz(); one iteration's reciprocal swings
    // by ±20% around a mean the machine is hitting exactly.
    double hz_now  = field_rate_hz(inter);
    double hz_nom  = nominal_hz(inter);
    double speed   = (hz_nom > 0.1) ? (hz_now / hz_nom) * 100.0 : 0.0;
    char sbuf[16]; snprintf(sbuf, sizeof(sbuf), "%.0f%%", speed);
    ImVec4 scol = speed >= 98.0 ? ZS_OK : (speed >= 90.0 ? ZS_WARN : ZS_CRIT);
    vital("Speed", sbuf, scol, (float)(speed / 100.0), scol);

    ImGui::SameLine();
    char rbuf[24]; snprintf(rbuf, sizeof(rbuf), "%.1f / %.1f", hz_now, hz_nom);
    vital("Fields/s", rbuf, ZS_DATA, (float)(hz_nom > 0.1 ? hz_now / hz_nom : 0.0), ZS_DATA);

    ImGui::SameLine();
    char fbuf[24]; snprintf(fbuf, sizeof(fbuf), "%.1f ms", g_vit_frame_ema);
    float ffrac = g_vit_budget_ms > 0.001 ? (float)(g_vit_frame_ema / g_vit_budget_ms) : 0.0f;
    ImVec4 fcol = ffrac <= 1.0f ? ZS_OK : ZS_WARN;
    vital("Frame", fbuf, ZS_TEXT, ffrac, fcol);

    ImGui::SameLine();
    char abuf[16]; snprintf(abuf, sizeof(abuf), "%d", g_vit_aq);
    vital("Audio queue", abuf, ZS_AUDIO,
          (float)g_vit_aq / (float)(g_vit_aq_target ? g_vit_aq_target : 2048), ZS_AUDIO);

    ImGui::SameLine();
    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%+.2f%%", g_vit_drift);
    ImVec4 dcol = (g_vit_drift > -0.5 && g_vit_drift < 0.5) ? ZS_OK : ZS_WARN;
    vital("Drift", dbuf, dcol, 0.5f + (float)(g_vit_drift / 4.0), dcol);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Mode rail — Pipeline / Display / Frame / Code / Memory / Audio / VRAM / Script
// ---------------------------------------------------------------------------

static void draw_mode_rail() {
    /* The rail is the only window in its dock node, so its tab was an empty
     * stub above "VIEW MODES". Let the node hide the tab bar for it. */
    ImGuiWindowClass rail_class;
    rail_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar | ImGuiDockNodeFlags_NoUndocking;
    ImGui::SetNextWindowClass(&rail_class);
    if (!ImGui::Begin("##Rail", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse)) { ImGui::End(); return; }

    micro_label("View Modes");
    ImGui::Dummy(ImVec2(0, 4.0f));

    for (int i = 0; i < MODE_COUNT; i++) {
        bool on = (g_mode == i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float row_w = ImGui::GetContentRegionAvail().x;
        float row_h = 32.0f;
        ImVec2 p1 = ImVec2(p0.x + row_w, p0.y + row_h);

        ImGui::PushID(i);
        ImGuiID id = ImGui::GetID("mode_item");
        ImRect bb(p0, p1);
        ImGui::ItemSize(bb);
        bool hovered = false, held = false;
        if (ImGui::ItemAdd(bb, id)) {
            bool clicked = ImGui::ButtonBehavior(bb, id, &hovered, &held);
            if (clicked && g_mode != i) {
                g_mode = i;
                g_layout_dirty = true;
            }
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();

        if (on) {
            ImU32 hi = ImGui::GetColorU32(zs_mix(ZS_PANEL2, ZS_LINE, 0.55f));
            ImU32 lo = ImGui::GetColorU32(zs_mix(ZS_GROUND, ZS_PANEL2, 0.60f));
            dl->AddRectFilledMultiColor(p0, p1, hi, lo, lo, hi);
            dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(ZS_DATA.x, ZS_DATA.y, ZS_DATA.z, 0.70f)), 4.0f, 0, 1.0f);
            dl->AddRectFilled(ImVec2(p0.x + 2.0f, p0.y + 4.0f),
                              ImVec2(p0.x + 5.0f, p0.y + row_h - 4.0f),
                              ImGui::GetColorU32(g_modes[i].symbol_col), 2.0f);
        } else if (hovered) {
            dl->AddRectFilled(p0, p1, IM_COL32(30, 42, 60, 180), 4.0f);
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(14, 19, 28, 120), 4.0f);
        }

        // Draw crisp PlayStation Vector Icon at x = p0.x + 20
        ImVec2 icon_center = ImVec2(p0.x + 20.0f, p0.y + row_h * 0.5f);
        ImU32 icon_col = ImGui::GetColorU32(on ? g_modes[i].symbol_col : ZS_MUTED);
        switch (i) {
            case MODE_PIPELINE: draw_icon_flow(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_DISPLAY:  draw_icon_screen(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_FRAME:    draw_icon_clock(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_CODE:     draw_icon_cpu(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_MEMORY:   draw_icon_ram(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_AUDIO:    draw_icon_wave(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_VRAM:     draw_icon_gpu(dl, icon_center, 5.5f, icon_col, 1.6f); break;
            case MODE_HOST:     draw_ps_chip(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_SCRIPT:   draw_ps_terminal(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            default: break;
        }

        // Mode Title
        float text_y = p0.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p0.x + 36.0f, text_y), ImGui::GetColorU32(on ? ZS_TEXT : ZS_MUTED), g_modes[i].name);

        // Key Shortcut (F1..F9)
        dl->AddText(ImVec2(p0.x + row_w - 24.0f, text_y), ImGui::GetColorU32(ZS_FAINT), g_modes[i].key);

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 3.0f));
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Stage helpers: a card shell and the pipeline / frame / audio views
// ---------------------------------------------------------------------------

// A header with a drawn icon in place of the dot. Same vertical rhythm as
// card_header, so the two can sit in the same panel without the labels
// stepping out of line.
static void card_header_icon(const char* title, ImVec4 col, ZsIconFn icon) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (icon) {
        icon(dl, ImVec2(p.x + 6.0f, p.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f),
             5.5f, ImGui::GetColorU32(col), 1.4f);
        ImGui::Dummy(ImVec2(16, 0)); ImGui::SameLine();
    }
    micro_label(title);
    ImGui::Separator();
}

static void card_header(const char* title, ImVec4 dot) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (dot.w > 0.0f) {
        dl->AddCircleFilled(ImVec2(p.x + 4, p.y + ImGui::GetTextLineHeight() * 0.5f + 1),
                            3.0f, ImGui::GetColorU32(dot));
        ImGui::Dummy(ImVec2(12, 0)); ImGui::SameLine();
    }
    micro_label(title);
    ImGui::Separator();
}

// A per-second rate from a cumulative counter, sampled over a ~0.5 s window so
// the number is readable rather than flickering every frame.
struct RateProbe { uint32_t prev; double prev_t; double rate; bool init; };
static double probe_rate(RateProbe* p, uint32_t cur, double t) {
    if (!p->init) { p->prev = cur; p->prev_t = t; p->init = true; return 0.0; }
    double dt = t - p->prev_t;
    if (dt >= 0.5) {
        p->rate = (double)(uint32_t)(cur - p->prev) / dt;
        p->prev = cur; p->prev_t = t;
    }
    return p->rate;
}

// Pipeline view: CD -> XA -> MDEC -> DMA -> VRAM/scanout on one row, live rates.
// Rates come from real counters where they exist; a stage not yet instrumented
// shows "n/a" rather than a fabricated number.
static void pipe_node_card(const char* title, ImVec4 dot, const char* status, ImVec4 status_col,
                           const char* val_str, const char* unit_str,
                           const char* sub1, const char* sub2) {
    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 104.0f * g_ui_scale;
    ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    // Multi-color linear gradient card background
    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(24, 34, 52, 240), IM_COL32(14, 19, 30, 240),
        IM_COL32(14, 19, 30, 240), IM_COL32(24, 34, 52, 240));
    dl->AddRect(p0, p1, IM_COL32(42, 60, 90, 255), 6.0f);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + 3.0f), ImGui::GetColorU32(dot), 3.0f);

    ImGui::Dummy(ImVec2(0, 4.0f));
    ImGui::Indent(8.0f);

    // Header: Title + Status Badge
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, status_col);
    ImGui::Text("[%s]", status);
    ImGui::PopStyleColor();

    // Primary metric — the one number the card exists for, set larger than the
    // rest of the card so a row of five reads at a glance.
    if (g_font_h1) ImGui::PushFont(g_font_h1);
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::Text("%s %s", val_str, unit_str);
    ImGui::PopStyleColor();
    if (g_font_h1) ImGui::PopFont();

    // Subtitle Metadata
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    if (sub1 && sub1[0]) ImGui::TextUnformatted(sub1);
    if (sub2 && sub2[0]) ImGui::TextUnformatted(sub2);
    ImGui::PopStyleColor();

    ImGui::Unindent(8.0f);
    ImGui::Dummy(ImVec2(0, 4.0f));
    ImGui::EndGroup();
}

static const char* drive_state_name(DriveState st) {
    switch (st) {
        case DRIVE_IDLE:     return "IDLE";
        case DRIVE_SPINUP:   return "SPINUP";
        case DRIVE_SEEKING:  return "SEEK";
        case DRIVE_READING:  return "READ";
        case DRIVE_PLAYING:  return "PLAY";
        case DRIVE_PAUSING:  return "PAUSE";
        case DRIVE_STOPPING: return "STOP";
        default:             return "?";
    }
}

static const char* mdec_state_name(MdecDecodeState st) {
    switch (st) {
        case MDEC_ST_IDLE:       return "IDLE";
        case MDEC_ST_DECODING:   return "DECODE";
        case MDEC_ST_WRITING:    return "WRITE";
        case MDEC_ST_SET_QTABLE: return "QTABLE";
        case MDEC_ST_SET_SCALE:  return "SCALE";
        case MDEC_ST_NOCOMMAND:  return "NOCMD";
        default:                 return "?";
    }
}

static const char* dma_sync_name(DmaSync s) {
    switch (s) {
        case MANUAL:      return "manual";
        case REQUEST:     return "request";
        case LINKED_LIST: return "linked list";
        default:         return "?";
    }
}

// Pipeline view: CD -> XA -> MDEC -> DMA -> VRAM/scanout on one row, live rates.
//
// Every status word on this view is read from the machine. They used to be
// literals — "OK", "READY", "RUNNING", "24 Voices 44.1 kHz" — which made the
// view answer the same thing whether the drive was seeking, idle or absent, and
// that is the one question the view exists to answer.
//
// The per-frame numbers come from the frame event ring, which is enabled here
// for the same reason the Frame view enables it: only while the view is on
// screen.
static void draw_pipeline_view(Interconnect* inter) {
    Spu* spu = inter ? &inter->spu : nullptr;
    Mdec* mdec = inter ? &inter->mdec : nullptr;
    Cdrom* cd = inter ? &inter->cdrom : nullptr;
    Gpu* gpu = inter ? &inter->gpu : nullptr;
    double t = ImGui::GetTime();

    frame_events_set_enabled(true);
    const FrameEventFrame* fr = frame_events_last();

    static RateProbe pr_cd = {0,0,0,false}, pr_xa = {0,0,0,false},
                     pr_dma = {0,0,0,false}, pr_mb = {0,0,0,false};
    double cd_rate  = cd ? probe_rate(&pr_cd,  cd->sectors_read_total,      t) : 0.0;
    double xa_rate  = cd ? probe_rate(&pr_xa,  cd->audio_fifo.total_pushed, t) : 0.0;
    double dma_rate = inter ? probe_rate(&pr_dma, inter->dma.stat_ch2_uploads, t) : 0.0;
    double mb_rate  = probe_rate(&pr_mb, mdec_stat_macroblocks(), t);

    char cdr[24], cd_s1[48], cd_s2[48];
    char mdr[24], md_s1[48], md_s2[48];
    char dmr[24], dm_s1[48], dm_s2[48];
    char gpv[24], gp_s1[48], gp_s2[48];
    char vrv[24], vr_s1[48], vr_s2[48];
    char xar[24], xa_s1[48], xa_s2[48];
    char spv[24], sp_s1[48], sp_s2[48];
    char mxv[24], mx_s1[48], mx_s2[48];

    /* --- 1. CD-ROM drive --- */
    const char* cd_status = cd ? drive_state_name(cd->drive_state) : "ABSENT";
    ImVec4 cd_col = ZS_FAINT;
    if (cd) {
        cd_col = (cd->drive_state == DRIVE_READING || cd->drive_state == DRIVE_PLAYING) ? ZS_OK
               : (cd->drive_state == DRIVE_SEEKING || cd->drive_state == DRIVE_SPINUP)  ? ZS_WARN
               : ZS_FAINT;
    }
    snprintf(cdr, sizeof(cdr), "%.1f", cd_rate);
    snprintf(cd_s1, sizeof(cd_s1), "%s, motor %s, mode 0x%02X",
             cd && cd->double_speed ? "2x" : "1x",
             cd && cd->motor_on ? "on" : "off",
             cd ? cd->mode : 0);
    snprintf(cd_s2, sizeof(cd_s2), "%u sectors read", cd ? cd->sectors_read_total : 0);

    /* --- 2. MDEC --- */
    const char* md_status = mdec ? mdec_state_name(mdec->decode_state) : "ABSENT";
    ImVec4 md_col = (mdec && mdec->decode_state != MDEC_ST_IDLE && mdec->decode_state != MDEC_ST_NOCOMMAND)
                    ? ZS_OK : ZS_FAINT;
    snprintf(mdr, sizeof(mdr), "%.0f", mb_rate);
    snprintf(md_s1, sizeof(md_s1), "in %u hw / out %u w",
             mdec ? mdec->in_count : 0, mdec ? mdec->out_count : 0);
    static const char* depth_name[4] = { "4bpp", "8bpp", "24bpp", "15bpp" };
    snprintf(md_s2, sizeof(md_s2), "%s out, %u total",
             mdec ? depth_name[mdec->output_depth & 3] : "-", mdec_stat_macroblocks());

    /* --- 3. DMA channel 2 (GPU) --- */
    const DmaChannel* ch2 = inter ? &inter->dma.channels[2] : nullptr;
    bool ch2_running = inter && (inter->dma.gpu_ll_active || inter->dma.gpu_req_active);
    const char* dm_status = !ch2 ? "ABSENT" : ch2_running ? "RUNNING" : (ch2->enable ? "ARMED" : "IDLE");
    ImVec4 dm_col = ch2_running ? ZS_OK : (ch2 && ch2->enable ? ZS_WARN : ZS_FAINT);
    snprintf(dmr, sizeof(dmr), "%.1f", dma_rate);
    snprintf(dm_s1, sizeof(dm_s1), "%s, MADR 0x%08X",
             ch2 ? dma_sync_name(ch2->sync) : "-", ch2 ? ch2->base_addr : 0);
    snprintf(dm_s2, sizeof(dm_s2), "%u words last frame", fr->type_count[FEV_DMA_GPU]);

    /* --- 4. GPU --- */
    uint32_t stat = gpu ? gpu_read_status(gpu) : 0;
    bool gpu_ready_cmd = (stat >> 26) & 1;
    const char* gp_status = !gpu ? "ABSENT" : (gpu_ready_cmd ? "READY" : "BUSY");
    ImVec4 gp_col = gpu ? (gpu_ready_cmd ? ZS_OK : ZS_WARN) : ZS_FAINT;
    snprintf(gpv, sizeof(gpv), "%u", fr->type_count[FEV_DRAW_BATCH]);
    snprintf(gp_s1, sizeof(gp_s1), "batches last frame");
    snprintf(gp_s2, sizeof(gp_s2), "GPUSTAT 0x%08X", stat);

    /* --- 5. VRAM and scanout --- */
    const char* vr_status = !gpu ? "ABSENT" : (gpu->display_disabled ? "BLANK" : "SCANOUT");
    ImVec4 vr_col = gpu ? (gpu->display_disabled ? ZS_WARN : ZS_OK) : ZS_FAINT;
    snprintf(vrv, sizeof(vrv), "%ux%u", gpu ? gpu->crtc.display_width : 0,
             gpu ? gpu->crtc.display_height : 0);
    snprintf(vr_s1, sizeof(vr_s1), "%s, %s%s",
             gpu && gpu->display_depth == D24Bits ? "24bpp" : "15bpp",
             gpu && gpu->vmode == Pal ? "PAL 50Hz" : "NTSC 60Hz",
             gpu && gpu->interlaced ? ", interlaced" : "");
    snprintf(vr_s2, sizeof(vr_s2), "origin %u,%u  %u uploads",
             gpu ? gpu->crtc.display_vram_x : 0, gpu ? gpu->crtc.display_vram_y : 0,
             fr->type_count[FEV_VRAM_UPLOAD]);

    /* --- audio track --- */
    bool xa_on = cd && cd->xa_adpcm_enable && !cd->xa_mute && !cd->muted;
    const char* xa_status = !cd ? "ABSENT" : (cd->muted || cd->xa_mute) ? "MUTED"
                          : cd->xa_adpcm_enable ? "STREAM" : "OFF";
    ImVec4 xa_col = xa_on ? ZS_OK : ZS_FAINT;
    snprintf(xar, sizeof(xar), "%.0f", xa_rate);
    snprintf(xa_s1, sizeof(xa_s1), "FIFO %u samples", cd ? cd->audio_fifo.count : 0);
    snprintf(xa_s2, sizeof(xa_s2), "%u pushed, %u dropped",
             cd ? cd->audio_fifo.total_pushed : 0, cd ? cd->audio_fifo.total_dropped : 0);

    int voices_on = 0;
    if (spu) for (int i = 0; i < 24; i++) if (spu->voices[i].on) voices_on++;
    bool reverb_on = spu && (spu->control & 0x0080);
    const char* sp_status = !spu ? "ABSENT" : (voices_on ? "ACTIVE" : "SILENT");
    ImVec4 sp_col = voices_on ? ZS_OK : ZS_FAINT;
    snprintf(spv, sizeof(spv), "%d / 24", voices_on);
    snprintf(sp_s1, sizeof(sp_s1), "voices keyed on");
    snprintf(sp_s2, sizeof(sp_s2), "reverb %s, SPUCNT 0x%04X",
             reverb_on ? "on" : "off", spu ? spu->control : 0);

    int ring_used = spu ? spu_ring_used(spu) : 0;
    bool starving = ring_used < g_vit_aq_target / 4;
    const char* mx_status = !spu ? "ABSENT" : (spu->underrun_events ? "UNDERRUN" : starving ? "LOW" : "LIVE");
    ImVec4 mx_col = !spu ? ZS_FAINT : (spu->underrun_events ? ZS_CRIT : starving ? ZS_WARN : ZS_OK);
    snprintf(mxv, sizeof(mxv), "%d / %d", ring_used, g_vit_aq_target);
    snprintf(mx_s1, sizeof(mx_s1), "%u underruns, %u dropped",
             spu ? spu->underrun_events : 0, spu ? spu->dropped_samples : 0);
    snprintf(mx_s2, sizeof(mx_s2), "drift %+.2f%%", g_vit_drift);

    card_header_icon("Video path: CD -> MDEC -> DMA -> GPU -> scanout", ZS_DATA, draw_icon_flow);
    if (ImGui::BeginTable("##pipe_video", 5, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        pipe_node_card("1. CD-ROM drive", ZS_DATA, cd_status, cd_col, cdr, "sect/s", cd_s1, cd_s2);
        ImGui::TableNextColumn();
        pipe_node_card("2. MDEC decoder", ZS_DATA, md_status, md_col, mdr, "mblk/s", md_s1, md_s2);
        ImGui::TableNextColumn();
        pipe_node_card("3. DMA ch2 (GPU)", ZS_DATA, dm_status, dm_col, dmr, "up/s", dm_s1, dm_s2);
        ImGui::TableNextColumn();
        pipe_node_card("4. GPU rasterizer", ZS_DATA, gp_status, gp_col, gpv, "", gp_s1, gp_s2);
        ImGui::TableNextColumn();
        pipe_node_card("5. VRAM and display", ZS_OK, vr_status, vr_col, vrv, "", vr_s1, vr_s2);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 10));

    card_header_icon("Audio path: XA -> SPU -> device", ZS_AUDIO, draw_icon_wave);
    if (ImGui::BeginTable("##pipe_audio", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        pipe_node_card("1. XA ADPCM", ZS_AUDIO, xa_status, xa_col, xar, "smp/s", xa_s1, xa_s2);
        ImGui::TableNextColumn();
        pipe_node_card("2. SPU voices", ZS_AUDIO, sp_status, sp_col, spv, "", sp_s1, sp_s2);
        ImGui::TableNextColumn();
        pipe_node_card("3. Mixer and output", ZS_OK, mx_status, mx_col, mxv, "smp", mx_s1, mx_s2);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("Per-frame counts come from the frame event ring, recorded only while this "
                       "view is open. Rates are sampled over half a second.");
    ImGui::PopStyleColor();
}

// Frame view — the per-frame event ring with cycle timestamps does not exist
// yet (Phase 4 in docs/ui/README.md: "the renderer records order today, not
// time"). Draw the track chrome and say plainly what is missing.
// Frame inspector (Phase 4): the frame's events on a time axis against its
// cycle budget. Recording is enabled only while this view is on screen — the
// standing constraint is that the panels cost, not the core.
static void draw_frame_view(Interconnect* inter) {
    frame_events_set_enabled(true);

    /* Holding a frame is what makes the view usable: the interesting frame is
     * over before it can be read, and the next one has already replaced it. The
     * hold is a copy, so recording carries on underneath it. */
    static FrameEventFrame s_held;
    static bool s_hold = false;

    const FrameEventFrame* live = frame_events_last();
    const FrameEventFrame* fr   = s_hold ? &s_held : live;

    const uint32_t budget = inter ? gpu_cycles_per_frame(&inter->gpu) : 566203u;
    uint32_t span = fr->end_cycle - fr->start_cycle;
    if (span == 0) span = budget;
    const double cy_to_ms = 1000.0 / (double)PSX_SYSCLK_HZ;

    /* --- header: the frame against its budget, plus the hold --- */
    if (ImGui::Button(s_hold ? "Resume" : "Hold frame", ImVec2(120.0f * g_ui_scale, 0))) {
        if (!s_hold) s_held = *live;
        s_hold = !s_hold;
    }
    ImGui::SameLine();
    if (s_hold) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
        ImGui::TextUnformatted("held");
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    {
        double pct = 100.0 * (double)span / (double)budget;
        ImGui::PushStyleColor(ImGuiCol_Text, pct > 100.0 ? ZS_WARN : ZS_MUTED);
        ImGui::Text("%.2f ms of %.2f ms budget  (%.0f%%)",
                    (double)span * cy_to_ms, (double)budget * cy_to_ms, pct);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
        ImGui::Text("   %u events", fr->count);
        ImGui::PopStyleColor();
        if (fr->dropped) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_CRIT);
            ImGui::Text("   %u dropped — the tail of this frame is missing", fr->dropped);
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 6));

    struct Row { const char* label; FrameEventType type; ImVec4 col; const char* unit; };
    static const Row ROWS[] = {
        { "VRAM uploads", FEV_VRAM_UPLOAD, ZS_DATA,     "px"    },
        { "VRAM copies",  FEV_VRAM_COPY,   ZS_PS_BLUE,  "px"    },
        { "Draw batches", FEV_DRAW_BATCH,  ZS_OK,       "vtx"   },
        { "DMA ch2",      FEV_DMA_GPU,     ZS_PS_GREEN, "words" },
        { "XA sectors",   FEV_XA_SECTOR,   ZS_AUDIO,    "smp"   },
    };
    const int NROWS = (int)(sizeof(ROWS) / sizeof(ROWS[0]));

    /* The count answers "how many", the payload answers "how much" — a frame
     * that moves twice the pixels in the same number of transfers is the case
     * worth seeing, and the event's detail field already carries it. */
    unsigned long long payload[FEV_TYPE_COUNT] = {0};
    for (uint32_t e = 0; e < fr->count; e++)
        payload[fr->events[e].type] += fr->events[e].detail;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ZS_RAIL_BG);
    ImGui::BeginChild("##frame", ImVec2(0, 0), true);

    const float LABEL_W = 130.0f * g_ui_scale;
    const float COUNT_W = 70.0f  * g_ui_scale;
    const float PAY_W   = 120.0f * g_ui_scale;
    /* The rows give way before the view scrolls: whatever height the dock gives
     * this panel, minus the axis and the two footer lines, is shared among the
     * rows down to a floor. Below the floor it scrolls, as before. */
    float row_room = ImGui::GetContentRegionAvail().y - 24.0f * g_ui_scale - ImGui::GetTextLineHeightWithSpacing() * 4.0f;
    float ROW_H = row_room / (float)NROWS;
    if (ROW_H > 26.0f * g_ui_scale) ROW_H = 26.0f * g_ui_scale;
    if (ROW_H < 18.0f * g_ui_scale) ROW_H = 18.0f * g_ui_scale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool hovering = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    float track_x0 = ImGui::GetCursorScreenPos().x + LABEL_W + COUNT_W + PAY_W;
    float track_w  = ImGui::GetContentRegionAvail().x - (LABEL_W + COUNT_W + PAY_W) - 8.0f * g_ui_scale;
    if (track_w < 64.0f) track_w = 64.0f;
    float first_row_y = ImGui::GetCursorScreenPos().y;

    char tip[192] = {0};

    for (int i = 0; i < NROWS; i++) {
        ImVec2 rp = ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
        ImGui::TextUnformatted(ROWS[i].label);
        ImGui::PopStyleColor();

        uint32_t cnt = fr->type_count[ROWS[i].type];
        ImGui::SameLine(LABEL_W);
        ImGui::PushStyleColor(ImGuiCol_Text, cnt ? ZS_TEXT : ZS_FAINT);
        if (g_font_mono) ImGui::PushFont(g_font_mono);
        ImGui::Text("%6u", cnt);
        ImGui::SameLine(LABEL_W + COUNT_W);
        ImGui::Text("%8llu %s", payload[ROWS[i].type], ROWS[i].unit);
        if (g_font_mono) ImGui::PopFont();
        ImGui::PopStyleColor();

        ImVec2 p = ImVec2(track_x0, rp.y);
        float h = ROW_H - 8.0f * g_ui_scale;
        dl->AddRectFilled(p, ImVec2(p.x + track_w, p.y + h), ImGui::GetColorU32(ZS_DOCK_BG), 3.0f);

        ImU32 col = ImGui::GetColorU32(ROWS[i].col);
        for (uint32_t e = 0; e < fr->count; e++) {
            if (fr->events[e].type != ROWS[i].type) continue;
            float t = (float)(fr->events[e].cycle - fr->start_cycle) / (float)span;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float x = p.x + t * track_w;
            dl->AddRectFilled(ImVec2(x, p.y + 2.0f * g_ui_scale),
                              ImVec2(x + 1.6f * g_ui_scale, p.y + h - 2.0f * g_ui_scale), col);

            if (hovering && mouse.y >= p.y && mouse.y <= p.y + h &&
                fabsf(mouse.x - x) <= 4.0f * g_ui_scale) {
                snprintf(tip, sizeof(tip), "%s   %.3f ms into the frame   cycle %u   %u %s",
                         ROWS[i].label,
                         (double)(fr->events[e].cycle - fr->start_cycle) * cy_to_ms,
                         fr->events[e].cycle, fr->events[e].detail, ROWS[i].unit);
            }
        }
        ImGui::Dummy(ImVec2(track_w, ROW_H));
    }

    /* The display flip crosses every row: it is the anchor the rest is read
     * against, since anything after it belongs to the next field. */
    float rows_bottom = first_row_y + (float)NROWS * ROW_H;
    for (uint32_t e = 0; e < fr->count; e++) {
        if (fr->events[e].type != FEV_FLIP) continue;
        float t = (float)(fr->events[e].cycle - fr->start_cycle) / (float)span;
        if (t < 0.0f || t > 1.0f) continue;
        float x = track_x0 + t * track_w;
        dl->AddLine(ImVec2(x, first_row_y), ImVec2(x, rows_bottom),
                    ImGui::GetColorU32(ZS_WARN), 1.2f * g_ui_scale);
    }

    /* --- the axis, in milliseconds, with the budget marked when overrun --- */
    {
        ImVec2 p = ImVec2(track_x0, ImGui::GetCursorScreenPos().y);
        dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + track_w, p.y), ImGui::GetColorU32(ZS_LINE), 1.0f);
        for (int k = 0; k <= 4; k++) {
            float x = p.x + track_w * (float)k / 4.0f;
            dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + 4.0f * g_ui_scale),
                        ImGui::GetColorU32(ZS_LINE), 1.0f);
            char lab[24];
            snprintf(lab, sizeof(lab), "%.1f ms", (double)span * cy_to_ms * (double)k / 4.0);
            ImVec2 ts = ImGui::CalcTextSize(lab);
            float lx = (k == 0) ? x : (k == 4 ? x - ts.x : x - ts.x * 0.5f);
            dl->AddText(ImVec2(lx, p.y + 5.0f * g_ui_scale), ImGui::GetColorU32(ZS_FAINT), lab);
        }
        if (span > budget) {
            float bx = p.x + track_w * ((float)budget / (float)span);
            dl->AddLine(ImVec2(bx, first_row_y), ImVec2(bx, p.y + 4.0f * g_ui_scale),
                        ImGui::GetColorU32(ZS_CRIT), 1.5f * g_ui_scale);
            /* The label goes on whichever side of the line has room: it used to
             * run off the right edge of the track when the line sat near it. */
            float bw = ImGui::CalcTextSize("budget").x;
            float bx_text = (bx + 4.0f * g_ui_scale + bw <= p.x + track_w) ? bx + 4.0f * g_ui_scale
                                                                            : bx - 4.0f * g_ui_scale - bw;
            dl->AddText(ImVec2(bx_text, first_row_y - 2.0f * g_ui_scale),
                        ImGui::GetColorU32(ZS_CRIT), "budget");
        }
        ImGui::Dummy(ImVec2(track_w, 24.0f * g_ui_scale));
    }

    if (tip[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_DATA);
        if (g_font_mono) ImGui::PushFont(g_font_mono);
        ImGui::TextUnformatted(tip);
        if (g_font_mono) ImGui::PopFont();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
        ImGui::TextUnformatted("Hover a tick for its time and its payload.");
        ImGui::PopStyleColor();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("One tick is one event, placed by CPU cycle inside the frame. The amber "
                       "line is the display flip; the red line is the nominal budget, drawn only "
                       "when the frame overran it. Recording runs only while this view is open.");
    ImGui::PopStyleColor();

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// Read a guest byte for the hex view without disturbing I/O. Only the storage
// regions are decoded (RAM with its mirrors, scratchpad, BIOS ROM); anything
// else returns false so the cell shows "--" instead of triggering a device
// side effect the way interconnect_load8 on a FIFO port would.
static bool mem_peek(Interconnect* inter, uint32_t addr, uint8_t* out) {
    if (!inter) return false;
    uint32_t p = addr & 0x1FFFFFFF;              // strip KUSEG/KSEG0/KSEG1
    if (p < RAM_SIZE) { if (inter->ram) { *out = inter->ram->data[p]; return true; } return false; }
    if (p >= 0x1F800000 && p < 0x1F800000 + SCRATCHPAD_SIZE) {
        *out = inter->scratchpad[p - 0x1F800000]; return true;
    }
    if (p >= 0x1FC00000 && p < 0x1FC00000 + BIOS_SIZE) {
        if (inter->bios) { *out = inter->bios->data[p - 0x1FC00000]; return true; }
        return false;
    }
    return false;
}

// Memory hex view: address gutter + 16 bytes + ASCII, clipped, scrollable.
static uint32_t g_mem_addr = 0x80000000;   // KSEG0 RAM base
static char     g_mem_goto[12] = "80000000";

static void draw_memory_view(Interconnect* inter) {
    // Toolbar: goto + quick region jumps
    ImGui::SetNextItemWidth(110);
    if (ImGui::InputText("##memgoto", g_mem_goto, sizeof(g_mem_goto),
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        char* e; uint32_t a = (uint32_t)strtoul(g_mem_goto, &e, 16);
        if (e != g_mem_goto) g_mem_addr = a & ~0xFu;
    }
    ImGui::SameLine();
    if (ImGui::Button("Go")) {
        char* e; uint32_t a = (uint32_t)strtoul(g_mem_goto, &e, 16);
        if (e != g_mem_goto) g_mem_addr = a & ~0xFu;
    }
    ImGui::SameLine(); if (ImGui::SmallButton("RAM"))   g_mem_addr = 0x80000000;
    ImGui::SameLine(); if (ImGui::SmallButton("Scratch")) g_mem_addr = 0x1F800000;
    ImGui::SameLine(); if (ImGui::SmallButton("BIOS"))  g_mem_addr = 0x1FC00000;
    ImGui::Separator();

    const int   ROWS = 4096;              // rows addressable from g_mem_addr
    ImGui::BeginChild("##memhex", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clip; clip.Begin(ROWS);
    while (clip.Step()) {
        for (int row = clip.DisplayStart; row < clip.DisplayEnd; row++) {
            uint32_t base = g_mem_addr + (uint32_t)row * 16;
            char line[128]; int n = 0;
            n += snprintf(line + n, sizeof(line) - n, "%08X  ", base);
            char ascii[17]; ascii[16] = '\0';
            for (int b = 0; b < 16; b++) {
                uint8_t v; bool ok = mem_peek(inter, base + b, &v);
                if (ok) { n += snprintf(line + n, sizeof(line) - n, "%02X ", v);
                          ascii[b] = (v >= 32 && v < 127) ? (char)v : '.'; }
                else    { n += snprintf(line + n, sizeof(line) - n, "-- ");
                          ascii[b] = ' '; }
                if (b == 7) n += snprintf(line + n, sizeof(line) - n, " ");
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
            ImGui::Text("%s  %s", line, ascii);
            ImGui::PopStyleColor();
        }
    }
    clip.End();
    ImGui::EndChild();
}

// Audio meters (inspector card for Audio mode): produced/consumed, queue, drift.
static void draw_audio_meters(Interconnect* inter) {
    Spu* spu = inter ? &inter->spu : nullptr;
    card_header("Audio", ZS_AUDIO);
    if (!spu) { ImGui::TextDisabled("No SPU"); return; }

    if (ImGui::BeginTable("##aud", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto kv = [](const char* k, const char* v, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
        };
        char q[32], d[24];
        snprintf(q, sizeof(q), "%d / %d", spu_ring_used(spu), g_vit_aq_target);
        snprintf(d, sizeof(d), "%+.2f%%", g_vit_drift);
        kv("Queue", q, ZS_TEXT);
        kv("Drift", d, (g_vit_drift > -0.5 && g_vit_drift < 0.5) ? ZS_OK : ZS_WARN);
        char gen[24]; snprintf(gen, sizeof(gen), "%u", spu->total_samples_generated);
        kv("Generated", gen, ZS_TEXT);
        char pk[24]; snprintf(pk, sizeof(pk), "%d / %d", spu->peak_level_left, spu->peak_level_right);
        kv("Peak L/R", pk, ZS_TEXT);
        char un[40];
        snprintf(un, sizeof(un), "%u ev / %u smp", spu->underrun_events, spu->underrun_samples);
        kv("Underruns", un, spu->underrun_events ? ZS_CRIT : ZS_OK);
        char dr[24]; snprintf(dr, sizeof(dr), "%u", spu->dropped_samples);
        kv("Ring drops", dr, spu->dropped_samples ? ZS_WARN : ZS_FAINT);
        bool rev = (spu->control & SPU_CTRL_REVERB_ENABLE);
        char rv[32]; snprintf(rv, sizeof(rv), "%s (SPUCNT %04X)", rev ? "on" : "off", spu->control);
        kv("Reverb", rv, rev ? ZS_AUDIO : ZS_FAINT);
        ImGui::EndTable();
    }
}

// VRAM CPU-vs-GPU comparison state (Phase 5). The readback is asynchronous:
// we raise a request and pick the result up on a later frame, identified by a
// sequence number, so the panel never blocks the emulation thread.
struct VramDiffState {
    bool     enabled      = false;
    uint32_t last_seq     = 0;
    bool     awaiting     = false;
    int      cooldown     = 0;      // frames until the next automatic request
    uint32_t total_diff   = 0;      // halfwords that differ at all
    uint32_t colour_diff  = 0;      // ... in the 15 colour bits
    uint32_t mask_diff    = 0;      // ... only in bit 15
    uint32_t gpu_only     = 0;      // GPU has a pixel where the CPU model has 0
    int      first_x      = -1;
    int      first_y      = -1;
};
static VramDiffState g_vram_diff;

static void update_vram_diff(Interconnect* inter) {
    if (!inter) return;
    Renderer* r = &inter->gpu.renderer;

    uint32_t seq = 0;
    const uint16_t* gpu_vram = renderer_get_vram_readback(&seq);

    if (g_vram_diff.awaiting && seq != g_vram_diff.last_seq) {
        g_vram_diff.last_seq = seq;
        g_vram_diff.awaiting = false;

        const uint16_t* cpu_vram = (const uint16_t*)inter->gpu.vram.data;
        uint32_t total = 0, colour = 0, mask = 0, gpu_only = 0;
        int fx = -1, fy = -1;
        for (uint32_t i = 0; i < 1024u * 512u; i++) {
            uint16_t a = cpu_vram[i], b = gpu_vram[i];
            if (a == b) continue;
            total++;
            if ((a & 0x7FFF) != (b & 0x7FFF)) colour++; else mask++;
            if (a == 0 && b != 0) gpu_only++;
            if (fx < 0) { fx = (int)(i % 1024u); fy = (int)(i / 1024u); }
        }
        g_vram_diff.total_diff  = total;
        g_vram_diff.colour_diff = colour;
        g_vram_diff.mask_diff   = mask;
        g_vram_diff.gpu_only    = gpu_only;
        g_vram_diff.first_x     = fx;
        g_vram_diff.first_y     = fy;
    }

    // A full 1024x512 compare is 512K halfwords; once every 30 frames keeps it
    // off the profile while still tracking a moving picture.
    if (g_vram_diff.enabled && !g_vram_diff.awaiting && --g_vram_diff.cooldown <= 0) {
        g_vram_diff.cooldown = 30;
        g_vram_diff.awaiting = true;
        renderer_request_vram_readback(r);
    }
}

static const struct {
    int bit;
    const char* name;
    const char* symbol;
    ImVec4 col;
} g_psx_button_info[16] = {
    { 0,  "SELECT",   "[SEL]", ZS_MUTED },
    { 1,  "L3",       "[L3]",  ZS_FAINT },
    { 2,  "R3",       "[R3]",  ZS_FAINT },
    { 3,  "START",    "[STR]", ZS_OK },
    { 4,  "UP",       "[UP]",  ZS_TEXT },
    { 5,  "RIGHT",    "[RGHT]",ZS_TEXT },
    { 6,  "DOWN",     "[DOWN]",ZS_TEXT },
    { 7,  "LEFT",     "[LEFT]",ZS_TEXT },
    { 8,  "L2",       "[L2]",  ZS_DATA },
    { 9,  "R2",       "[R2]",  ZS_DATA },
    { 10, "L1",       "[L1]",  ZS_DATA },
    { 11, "R1",       "[R1]",  ZS_DATA },
    { 12, "TRIANGLE", "[TRI]", ZS_PS_GREEN },
    { 13, "CIRCLE",   "[CIR]", ZS_PS_PINK },
    { 14, "CROSS",    "[CRS]", ZS_PS_RED },
    { 15, "SQUARE",   "[SQR]", ZS_PS_BLUE }
};

/* A label/value row in a two-column table: the label muted, the value in the
 * mono face so columns of numbers line up. */
static void gp_row(const char* k, const char* v, ImVec4 col) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
    ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    if (g_font_mono) ImGui::PushFont(g_font_mono);
    ImGui::TextUnformatted(v);
    if (g_font_mono) ImGui::PopFont();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// The pad, drawn
//
// A table of scancodes answers "what is Circle bound to". It does not answer
// "why does nothing happen when I press this", which is a question about a
// physical control — so the pad is drawn, lit live from the same 16-bit word
// the SIO sends the game, and every control on it is the button that rebinds
// it. The shape is the original DualShock's: two grips, a cross, four symbol
// buttons, four shoulders, and the two sticks with the Analog LED between them.
// ---------------------------------------------------------------------------

/* Rebind state, shared by the drawing and the list below it. */
static int  s_rebind_index      = -1;
static bool s_open_rebind_popup = false;
static bool s_waiting_key_up    = true;

struct PadZone {
    int   idx;            /* PSX button bit, or -1 for a decoration */
    bool  circle;
    ImVec2 a, b;          /* rect corners, or centre + (r,r) for a circle */
};

static bool pad_zone_hit(const PadZone& z, ImVec2 m) {
    if (z.circle) {
        float dx = m.x - z.a.x, dy = m.y - z.a.y;
        return (dx * dx + dy * dy) <= (z.b.x * z.b.x);
    }
    return m.x >= z.a.x && m.x <= z.b.x && m.y >= z.a.y && m.y <= z.b.y;
}

/* Draws the pad and returns the button index under the cursor, or -1. */
static int draw_dualshock(Controller* ctrl, uint16_t state, SioPadMode mode, float scale) {
    const float W = 470.0f * scale, H = 330.0f * scale;
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##padface", ImVec2(W, H));
    bool widget_hovered = ImGui::IsItemHovered();
    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto P = [&](float x, float y) { return ImVec2(o.x + x * scale, o.y + y * scale); };
    auto S = [&](float v) { return v * scale; };

    const ImU32 shell_hi = IM_COL32(52, 62, 84, 255);
    const ImU32 shell_lo = IM_COL32(26, 33, 48, 255);
    const ImU32 edge     = IM_COL32(70, 86, 118, 255);
    const ImU32 well     = IM_COL32(16, 21, 32, 255);
    const ImU32 text_col = ImGui::GetColorU32(ZS_MUTED);

    /* --- shell: two grips under a body, all capsules so the outline reads as
     * one moulded piece rather than three rectangles. --- */
    dl->AddRectFilled(P(96, 168), P(168, 306), shell_lo, S(34));
    dl->AddRectFilled(P(302, 168), P(374, 306), shell_lo, S(34));
    dl->AddRectFilledMultiColor(P(64, 54), P(406, 196), shell_hi, shell_hi, shell_lo, shell_lo);
    dl->AddRect(P(64, 54), P(406, 196), edge, S(28), 0, S(1.5f));
    dl->AddRect(P(96, 168), P(168, 306), edge, S(34), 0, S(1.5f));
    dl->AddRect(P(302, 168), P(374, 306), edge, S(34), 0, S(1.5f));

    /* Shoulders. L2/R2 sit above L1/R1 on the far edge, as on the pad. */
    struct { int idx; float x0, x1, y0, y1; const char* tag; } shoulders[4] = {
        {  8,  92, 168, 20, 40, "L2" },
        { 10,  92, 168, 40, 58, "L1" },
        {  9, 302, 378, 20, 40, "R2" },
        { 11, 302, 378, 40, 58, "R1" },
    };

    std::vector<PadZone> zones;
    zones.reserve(20);

    auto pressed = [&](int idx) { return idx >= 0 && ((state & (1u << idx)) == 0); };

    for (int i = 0; i < 4; i++) {
        ImVec2 a = P(shoulders[i].x0, shoulders[i].y0), b = P(shoulders[i].x1, shoulders[i].y1);
        bool on = pressed(shoulders[i].idx);
        dl->AddRectFilled(a, b, on ? ImGui::GetColorU32(ZS_DATA) : well, S(6));
        dl->AddRect(a, b, edge, S(6));
        ImVec2 ts = ImGui::CalcTextSize(shoulders[i].tag);
        dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, (a.y + b.y) * 0.5f - ts.y * 0.5f),
                    on ? IM_COL32(8, 16, 22, 255) : text_col, shoulders[i].tag);
        zones.push_back({ shoulders[i].idx, false, a, b });
    }

    /* --- D-pad: four arms, each its own control --- */
    const ImVec2 dc = P(140, 116);
    const float arm = S(36), thick = S(12);
    struct { int idx; ImVec2 a, b; } dpad[4] = {
        { 4, ImVec2(dc.x - thick, dc.y - arm),   ImVec2(dc.x + thick, dc.y - thick) },  /* UP    */
        { 6, ImVec2(dc.x - thick, dc.y + thick), ImVec2(dc.x + thick, dc.y + arm)   },  /* DOWN  */
        { 7, ImVec2(dc.x - arm,   dc.y - thick), ImVec2(dc.x - thick, dc.y + thick) },  /* LEFT  */
        { 5, ImVec2(dc.x + thick, dc.y - thick), ImVec2(dc.x + arm,   dc.y + thick) },  /* RIGHT */
    };
    dl->AddRectFilled(ImVec2(dc.x - arm - S(4), dc.y - arm - S(4)),
                      ImVec2(dc.x + arm + S(4), dc.y + arm + S(4)), well, S(10));
    for (int i = 0; i < 4; i++) {
        bool on = pressed(dpad[i].idx);
        dl->AddRectFilled(dpad[i].a, dpad[i].b, on ? ImGui::GetColorU32(ZS_DATA) : IM_COL32(38, 47, 66, 255), S(3));
        dl->AddRect(dpad[i].a, dpad[i].b, edge, S(3));
        zones.push_back({ dpad[i].idx, false, dpad[i].a, dpad[i].b });
    }

    /* --- symbol buttons --- */
    const ImVec2 fc = P(330, 116);
    const float off = S(34), br = S(16);
    struct { int idx; ImVec2 c; ImVec4 col; int glyph; } face[4] = {
        { 12, ImVec2(fc.x, fc.y - off), ZS_PS_GREEN, 0 },   /* triangle */
        { 13, ImVec2(fc.x + off, fc.y), ZS_PS_PINK,  1 },   /* circle   */
        { 14, ImVec2(fc.x, fc.y + off), ZS_PS_BLUE,  2 },   /* cross    */
        { 15, ImVec2(fc.x - off, fc.y), ZS_PS_RED,   3 },   /* square   */
    };
    for (int i = 0; i < 4; i++) {
        bool on = pressed(face[i].idx);
        ImU32 fill = on ? ImGui::GetColorU32(face[i].col) : well;
        dl->AddCircleFilled(face[i].c, br, fill, 24);
        dl->AddCircle(face[i].c, br, edge, 24, S(1.5f));
        ImU32 gl = on ? IM_COL32(10, 14, 20, 255) : ImGui::GetColorU32(face[i].col);
        switch (face[i].glyph) {
            case 0: draw_ps_triangle(dl, face[i].c, br * 0.55f, gl, S(2.0f)); break;
            case 1: draw_ps_circle(dl, face[i].c, br * 0.8f, gl, S(2.0f)); break;
            case 2: draw_ps_cross(dl, face[i].c, br * 0.8f, gl, S(2.0f)); break;
            default: draw_ps_square(dl, face[i].c, br * 0.75f, gl, S(2.0f)); break;
        }
        zones.push_back({ face[i].idx, true, face[i].c, ImVec2(br, br) });
    }

    /* --- SELECT / START --- */
    struct { int idx; float x0, x1; const char* tag; } mid[2] = {
        { 0, 196, 224, "SELECT" },
        { 3, 246, 274, "START"  },
    };
    for (int i = 0; i < 2; i++) {
        ImVec2 a = P(mid[i].x0, 104), b = P(mid[i].x1, 118);
        bool on = pressed(mid[i].idx);
        dl->AddRectFilled(a, b, on ? ImGui::GetColorU32(ZS_OK) : well, S(7));
        dl->AddRect(a, b, edge, S(7));
        ImVec2 ts = ImGui::CalcTextSize(mid[i].tag);
        dl->AddText(ImVec2((a.x + b.x) * 0.5f - ts.x * 0.5f, b.y + S(3)), text_col, mid[i].tag);
        zones.push_back({ mid[i].idx, false, a, b });
    }

    /* --- the Analog LED, in the documented colours --- */
    ImVec4 led = (mode == SIO_PAD_ANALOG) ? ZS_CRIT : (mode == SIO_PAD_STICK) ? ZS_OK : ZS_FAINT;
    ImVec2 lc = P(235, 150);
    dl->AddCircleFilled(lc, S(6), ImGui::GetColorU32(led), 16);
    dl->AddCircle(lc, S(6), edge, 16, S(1.0f));
    {
        const char* tag = "ANALOG";
        ImVec2 ts = ImGui::CalcTextSize(tag);
        dl->AddText(ImVec2(lc.x - ts.x * 0.5f, lc.y + S(9)), text_col, tag);
    }

    /* --- sticks: the well, the cap, and the cap's live offset --- */
    struct { int idx; float cx; int16_t x, y; const char* tag; } sticks[2] = {
        { 1, 196, ctrl->left_x,  ctrl->left_y,  "L3" },
        { 2, 274, ctrl->right_x, ctrl->right_y, "R3" },
    };
    for (int i = 0; i < 2; i++) {
        ImVec2 c = P(sticks[i].cx, 196);
        float r = S(28);
        bool on = pressed(sticks[i].idx);
        dl->AddCircleFilled(c, r, well, 28);
        dl->AddCircle(c, r, edge, 28, S(1.5f));
        float dx = (float)sticks[i].x / 32768.0f * r * 0.55f;
        float dy = (float)sticks[i].y / 32768.0f * r * 0.55f;
        ImVec2 cap = ImVec2(c.x + dx, c.y + dy);
        dl->AddCircleFilled(cap, r * 0.6f, on ? ImGui::GetColorU32(ZS_DATA) : IM_COL32(44, 55, 76, 255), 24);
        dl->AddCircle(cap, r * 0.6f, edge, 24, S(1.0f));
        ImVec2 ts = ImGui::CalcTextSize(sticks[i].tag);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + r + S(3)), text_col, sticks[i].tag);
        zones.push_back({ sticks[i].idx, true, c, ImVec2(r, r) });
    }

    /* --- hover, highlight, and the bound key drawn on the control itself --- */
    int hovered = -1;
    if (widget_hovered) {
        for (size_t i = 0; i < zones.size(); i++)
            if (pad_zone_hit(zones[i], mouse)) { hovered = zones[i].idx; break; }
    }
    for (size_t i = 0; i < zones.size(); i++) {
        const PadZone& z = zones[i];
        bool is_hover  = (z.idx == hovered);
        bool is_rebind = (z.idx == s_rebind_index);
        if (!is_hover && !is_rebind) continue;
        ImU32 col = ImGui::GetColorU32(is_rebind ? ZS_WARN : ZS_DATA);
        if (z.circle) dl->AddCircle(z.a, z.b.x + S(3), col, 28, S(2.0f));
        else dl->AddRect(ImVec2(z.a.x - S(3), z.a.y - S(3)), ImVec2(z.b.x + S(3), z.b.y + S(3)),
                         col, S(5), 0, S(2.0f));
    }

    if (hovered >= 0) {
        int sc = ctrl->key_map[hovered];
        const char* kn = (sc > 0 && sc < SDL_SCANCODE_COUNT) ? SDL_GetScancodeName((SDL_Scancode)sc) : "unbound";
        ImGui::SetTooltip("%s\nkeyboard: %s\nclick to rebind", g_psx_button_info[hovered].name, kn);
    }

    if (widget_hovered && hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        s_rebind_index = hovered;
        s_open_rebind_popup = true;
        s_waiting_key_up = true;
    }

    return hovered;
}

static void draw_controller_mapping_window() {
    if (!g_show_controller_mapping) return;

    ImGui::SetNextWindowSize(ImVec2(980.0f * g_ui_scale, 640.0f * g_ui_scale), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controller", &g_show_controller_mapping, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    Controller* ctrl = controller_get_active();
    if (!ctrl) {
        ImGui::TextColored(ZS_WARN, "Controller subsystem not active.");
        ImGui::End();
        return;
    }

    /* One read per frame, shared by the drawing and the readouts: the word here
     * is the same one the SIO hands the game. */
    uint16_t state = controller_update(ctrl);
    SioPadMode mode = sio_get_pad_mode(nullptr);

    /* --- left column: the pad --- */
    ImGui::BeginGroup();
    card_header_icon("Pad", ZS_PS_BLUE, draw_icon_pad);
    draw_dualshock(ctrl, state, mode, g_ui_scale * 1.2f);

    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("Lit controls are pressed right now. Click one to bind a key to it.");
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::SameLine(0, 18.0f * g_ui_scale);

    /* --- right column: what the game sees, then the bindings --- */
    ImGui::BeginGroup();

    card_header_icon("Device", ZS_OK, draw_icon_gpu);
    const char* dev_name = ctrl->gc ? SDL_GetGamepadName(ctrl->gc) : "keyboard";
    if (ImGui::BeginTable("padinfo", 2, ImGuiTableFlags_SizingStretchProp)) {
        gp_row("Host device", dev_name, ctrl->gc ? ZS_OK : ZS_MUTED);
        gp_row("Light bar", ctrl->gc ? (ctrl->gc_has_led ? "addressable" : "no hidraw access") : "-",
               ctrl->gc && ctrl->gc_has_led ? ZS_OK : ZS_FAINT);
        char b[64];
        snprintf(b, sizeof(b), "0x%04X", state);
        gp_row("Pad word", b, ZS_DATA);
        snprintf(b, sizeof(b), "%6d, %6d", ctrl->left_x, ctrl->left_y);
        gp_row("Left stick", b, ZS_TEXT);
        snprintf(b, sizeof(b), "%6d, %6d", ctrl->right_x, ctrl->right_y);
        gp_row("Right stick", b, ZS_TEXT);
        uint8_t m1 = 0, m2 = 0;
        sio_get_rumble(nullptr, &m1, &m2);
        snprintf(b, sizeof(b), "large %3u, small %s", m1, m2 ? "on" : "off");
        gp_row("Rumble", b, (m1 || m2) ? ZS_WARN : ZS_FAINT);
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 8));
    card_header_icon("Emulated pad mode", ZS_DATA, draw_icon_pad);
    const char* names[3] = { "Digital", "Analog", "Stick" };
    SioPadMode modes[3]  = { SIO_PAD_DIGITAL, SIO_PAD_ANALOG, SIO_PAD_STICK };
    for (int i = 0; i < 3; i++) {
        bool on = (mode == modes[i]);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ZS_MODE_ON : ZS_PANEL2);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ZS_DATA : ZS_MUTED);
        if (ImGui::Button(names[i], ImVec2(96.0f * g_ui_scale, 30.0f * g_ui_scale)))
            sio_set_pad_mode(nullptr, modes[i]);
        ImGui::PopStyleColor(2);
        if (i < 2) ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("Digital is the power-on state, as on hardware. Analog and stick append "
                       "adc0-3 to a 42h read; stick mode is the LED-green flight mode a few "
                       "titles ask for by name.");
    ImGui::PopStyleColor();
    ImGui::Checkbox("Swap Cross / Circle", &ctrl->swap_cross_circle);

    ImGui::Dummy(ImVec2(0, 8));
    card_header_icon("Keyboard bindings", ZS_WARN, draw_icon_threads);
    if (ImGui::SmallButton("WASD + EZXC")) {
        ctrl->key_map[0]  = SDL_SCANCODE_TAB;
        ctrl->key_map[3]  = SDL_SCANCODE_SPACE;
        ctrl->key_map[4]  = SDL_SCANCODE_W;
        ctrl->key_map[5]  = SDL_SCANCODE_D;
        ctrl->key_map[6]  = SDL_SCANCODE_S;
        ctrl->key_map[7]  = SDL_SCANCODE_A;
        ctrl->key_map[8]  = SDL_SCANCODE_LSHIFT;
        ctrl->key_map[9]  = SDL_SCANCODE_LCTRL;
        ctrl->key_map[10] = SDL_SCANCODE_Q;
        ctrl->key_map[11] = SDL_SCANCODE_R;
        ctrl->key_map[12] = SDL_SCANCODE_E;
        ctrl->key_map[13] = SDL_SCANCODE_C;
        ctrl->key_map[14] = SDL_SCANCODE_Z;
        ctrl->key_map[15] = SDL_SCANCODE_X;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Arrows + WASD")) {
        ctrl->key_map[4]  = SDL_SCANCODE_UP;
        ctrl->key_map[5]  = SDL_SCANCODE_RIGHT;
        ctrl->key_map[6]  = SDL_SCANCODE_DOWN;
        ctrl->key_map[7]  = SDL_SCANCODE_LEFT;
        ctrl->key_map[12] = SDL_SCANCODE_W;
        ctrl->key_map[13] = SDL_SCANCODE_D;
        ctrl->key_map[14] = SDL_SCANCODE_S;
        ctrl->key_map[15] = SDL_SCANCODE_A;
    }

    /* The list is the fallback, not the interface: the pad above is where a
     * binding is changed. It stays available for a button the drawing has no
     * room to label. */
    if (ImGui::CollapsingHeader("All bindings") &&
        ImGui::BeginTable("keymaptable", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_ScrollY,
                          ImVec2(0, 210.0f * g_ui_scale))) {
        ImGui::TableSetupColumn("Button");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int i = 0; i < 16; i++) {
            bool down = ((state & (1u << i)) == 0);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, down ? g_psx_button_info[i].col : ZS_MUTED);
            ImGui::TextUnformatted(g_psx_button_info[i].name);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            int sc = ctrl->key_map[i];
            const char* kn = (sc > 0 && sc < SDL_SCANCODE_COUNT) ? SDL_GetScancodeName((SDL_Scancode)sc) : "unbound";
            if (g_font_mono) ImGui::PushFont(g_font_mono);
            ImGui::PushStyleColor(ImGuiCol_Text, sc > 0 ? ZS_TEXT : ZS_FAINT);
            ImGui::TextUnformatted(kn);
            ImGui::PopStyleColor();
            if (g_font_mono) ImGui::PopFont();
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton(s_rebind_index == i ? "press a key" : "rebind")) {
                s_rebind_index = i;
                s_open_rebind_popup = true;
                s_waiting_key_up = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();

    /* --- rebind capture, from either the pad or the list --- */
    if (s_open_rebind_popup) {
        ImGui::OpenPopup("Rebind");
        s_open_rebind_popup = false;
    }
    if (s_rebind_index >= 0) {
        if (ImGui::BeginPopupModal("Rebind", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Press a key for %s", g_psx_button_info[s_rebind_index].name);
            ImGui::TextColored(ZS_FAINT, "Escape cancels");
            ImGui::Dummy(ImVec2(0, 6));

            const bool* keys = SDL_GetKeyboardState(NULL);
            if (s_waiting_key_up) {
                bool any_down = false;
                for (int k = 4; k < SDL_SCANCODE_COUNT; k++) if (keys[k]) { any_down = true; break; }
                if (!any_down) s_waiting_key_up = false;
            } else {
                for (int k = 4; k < SDL_SCANCODE_COUNT; k++) {
                    if (!keys[k]) continue;
                    if (k != SDL_SCANCODE_ESCAPE) ctrl->key_map[s_rebind_index] = k;
                    s_rebind_index = -1;
                    s_waiting_key_up = true;
                    ImGui::CloseCurrentPopup();
                    break;
                }
            }
            if (ImGui::Button("Cancel")) {
                s_rebind_index = -1;
                s_waiting_key_up = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

static void draw_host_hw_window(Interconnect* inter) {
    (void)inter;
    if (!ImGui::Begin("Host HW", nullptr)) { ImGui::End(); return; }

    /* Everything on this panel is read from the kernel every half second.
     * It used to be a list of typed-in strings, which is worse than no panel:
     * the line that says which GPU has the context is the first thing checked
     * before a rendering difference gets blamed on the emulator, and a
     * hard-coded one always agrees with itself. */
    host_info_sample();
    const HostInfo* h = host_info_get();

    auto kv = [](const char* k, const char* v, ImVec4 col) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
    };

    char buf[192];

    card_header_icon("Machine", ZS_OK, draw_icon_cpu);
    if (ImGui::BeginTable("hosthw_machine", 2, ImGuiTableFlags_SizingStretchProp)) {
        kv("System", h->system, ZS_TEXT);
        if (h->cpu_cores > 0)
            snprintf(buf, sizeof(buf), "%s  (%dC/%dT)", h->cpu_model, h->cpu_cores, h->cpu_threads);
        else
            snprintf(buf, sizeof(buf), "%s  (%dT)", h->cpu_model, h->cpu_threads);
        kv("CPU", buf, ZS_OK);
        kv("Kernel", h->kernel, ZS_FAINT);
        kv("Distribution", h->distro, ZS_FAINT);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 8));

    card_header_icon("Graphics context", ZS_DATA, draw_icon_gpu);
    if (ImGui::BeginTable("hosthw_gl", 2, ImGuiTableFlags_SizingStretchProp)) {
        kv("Renderer", h->gl_renderer, ZS_DATA);
        kv("Vendor", h->gl_vendor, ZS_TEXT);
        kv("GL version", h->gl_version, ZS_FAINT);
        kv("Driver", h->gl_driver, ZS_TEXT);
        if (h->gpu_request[0]) {
            snprintf(buf, sizeof(buf), "ZS1_GPU=%s — %s", h->gpu_request,
                     h->gpu_request_honoured ? "honoured" : "NOT honoured");
            kv("Request", buf, h->gpu_request_honoured ? ZS_OK : ZS_CRIT);
        } else {
            kv("Request", "ZS1_GPU unset — whatever the system offered", ZS_FAINT);
        }
        ImGui::EndTable();
    }
    if (h->gpu_request[0] && !h->gpu_request_honoured) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
        ImGui::TextWrapped("The context is not on the requested GPU. Undefined GL behaves "
                           "differently on the two, so a rendering difference seen in this run "
                           "is not evidence about the emulator.");
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 8));

    card_header_icon("Memory", ZS_WARN, draw_icon_ram);
    if (ImGui::BeginTable("hosthw_mem", 2, ImGuiTableFlags_SizingStretchProp)) {
        snprintf(buf, sizeof(buf), "%.1f GB total, %.1f GB available",
                 h->ram_total_mb / 1024.0, h->ram_avail_mb / 1024.0);
        kv("System RAM", buf, ZS_TEXT);
        snprintf(buf, sizeof(buf), "%.0f MB resident", h->rss_mb);
        kv("This process", buf, ZS_TEXT);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 8));

    card_header_icon("Audio device", ZS_AUDIO, draw_icon_wave);
    if (ImGui::BeginTable("hosthw_audio", 2, ImGuiTableFlags_SizingStretchProp)) {
        kv("Driver", h->audio_driver, ZS_AUDIO);
        if (h->audio_freq > 0) {
            snprintf(buf, sizeof(buf), "%d Hz, %d ch", h->audio_freq, h->audio_channels);
            kv("Device format", buf, ZS_TEXT);
            snprintf(buf, sizeof(buf), "%d frames  (%.1f ms)", h->audio_buffer_frames,
                     host_info_audio_latency_ms());
            kv("Device buffer", buf, ZS_TEXT);
        } else {
            kv("Device format", "no device open", ZS_FAINT);
        }
        snprintf(buf, sizeof(buf), "%d / %d samples", g_vit_aq, g_vit_aq_target);
        kv("Emulator ring", buf, ZS_AUDIO);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 8));

    /* Per-thread CPU, as a share of one core, from /proc/self/task. The bars
     * used to be four constants. Threads are listed as the kernel names them:
     * "GPU" is the renderer thread, "cdrom-read" the async disc reader, and the
     * one marked main is emulation plus UI. */
    card_header_icon("Thread load", ZS_DATA, draw_icon_threads);
    snprintf(buf, sizeof(buf), "%.0f%% of one core across %d threads",
             h->process_cpu_pct, h->thread_count);
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));

    float avail = ImGui::GetContentRegionAvail().x;
    float label_w = 190.0f * g_ui_scale;
    float bar_h   = 20.0f * g_ui_scale;
    for (int i = 0; i < h->thread_count; i++) {
        const HostThreadLoad* th = &h->threads[i];
        ImGui::PushStyleColor(ImGuiCol_Text, th->is_main ? ZS_TEXT : ZS_MUTED);
        ImGui::Text("%s%s", th->name, th->is_main ? "  (emu+UI)" : "");
        ImGui::PopStyleColor();
        ImGui::SameLine(label_w);
        char pct[16]; snprintf(pct, sizeof(pct), "%.1f%%", th->cpu_pct);
        ImVec4 col = th->cpu_pct > 90.0 ? ZS_WARN : (th->is_main ? ZS_DATA : ZS_OK);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
        if (g_font_mono) ImGui::PushFont(g_font_mono);
        ImGui::ProgressBar((float)(th->cpu_pct / 100.0), ImVec2(avail - label_w, bar_h), pct);
        if (g_font_mono) ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    if (h->thread_count == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
        ImGui::TextUnformatted("/proc/self/task is not readable here.");
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Pinned Lua watches (docs/ui/README.md phase 6)
//
// A tile is an expression and its value. The expressions go through
// lua_debug_eval_expr(), which is the same interpreter the Script console uses,
// so anything the emu.* surface exposes can be pinned without adding a panel
// for it. Two containment rules, both from the standing constraint that the
// panels cost and the core does not:
//   * evaluation happens only while this panel is being drawn, and
//   * at most WATCH_EVAL_INTERVAL frames apart, not every frame.
// An expression that fails shows its error in place of the value rather than
// logging one line per refresh.
// ---------------------------------------------------------------------------

#define WATCH_MAX 8
#define WATCH_EVAL_INTERVAL 6

struct PinnedWatch {
    char expr[96];
    char val[96];
    bool ok;
};

static PinnedWatch g_watches[WATCH_MAX] = {
    { "emu.pc()",                      "", true },
    { "emu.gpustat()",                 "", true },
    { "emu.cycles()",                  "", true },
    { "select(9, emu.audio_stats())",  "", true },
};
static int g_watch_count = 4;
static int g_watch_frame = 0;

static void draw_pinned_watches(void) {
    card_header_icon("Pinned Watches", ZS_PS_YELLOW, draw_icon_pin);

    if (++g_watch_frame >= WATCH_EVAL_INTERVAL) {
        g_watch_frame = 0;
        for (int i = 0; i < g_watch_count; i++)
            g_watches[i].ok = lua_debug_eval_expr(g_watches[i].expr,
                                                  g_watches[i].val, sizeof(g_watches[i].val));
    }

    const ImVec4 tile_cols[4] = { ZS_DATA, ZS_AUDIO, ZS_OK, ZS_WARN };

    /* Two tiles a row only when each gets room for an expression and a 64-bit
     * value; in a narrow inspector they stack instead of running off the edge. */
    int tile_cols_n = ImGui::GetContentRegionAvail().x >= 520.0f * g_ui_scale ? 2 : 1;
    if (ImGui::BeginTable("watchtiles", tile_cols_n, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < g_watch_count; i++) {
            PinnedWatch& w = g_watches[i];
            ImGui::TableNextColumn();
            ImGui::PushID(i);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float wdt = ImGui::GetContentRegionAvail().x;
            float hgt = 52.0f * g_ui_scale;
            ImVec2 p1 = ImVec2(p0.x + wdt, p0.y + hgt);
            ImVec4 col = w.ok ? tile_cols[i & 3] : ZS_CRIT;

            dl->AddRectFilledMultiColor(p0, p1,
                IM_COL32(22, 30, 44, 255), IM_COL32(14, 19, 28, 255),
                IM_COL32(14, 19, 28, 255), IM_COL32(22, 30, 44, 255));
            dl->AddRect(p0, p1, IM_COL32(40, 56, 82, 255), 6.0f);
            dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), ImGui::GetColorU32(col), 3.0f);

            /* Text is clipped to the tile: a long expression or value is cut at
             * the tile's edge (the tooltip has the whole of it). */
            dl->PushClipRect(ImVec2(p0.x + 4.0f, p0.y), ImVec2(p1.x - 4.0f, p1.y), true);
            dl->AddText(ImVec2(p0.x + 10.0f, p0.y + 5.0f), ImGui::GetColorU32(ZS_MUTED), w.expr);
            ImFont* vf = g_font_mono ? g_font_mono : ImGui::GetFont();
            dl->AddText(vf, vf->LegacySize,
                        ImVec2(p0.x + 10.0f, p0.y + 5.0f + ImGui::GetTextLineHeight()),
                        ImGui::GetColorU32(col), w.val[0] ? w.val : "-");
            dl->PopClipRect();

            /* The whole tile is the hit box: click to edit, right-click to drop. */
            ImGui::InvisibleButton("##tile", ImVec2(wdt, hgt));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\nClick to edit, right-click to remove", w.expr);
                dl->AddRect(p0, p1, ImGui::GetColorU32(col), 6.0f, 0, 1.0f);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) ImGui::OpenPopup("##editwatch");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                for (int k = i; k < g_watch_count - 1; k++) g_watches[k] = g_watches[k + 1];
                g_watch_count--;
                ImGui::PopID();
                continue;
            }

            if (ImGui::BeginPopup("##editwatch")) {
                ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
                ImGui::TextUnformatted("Lua expression — the Script console's emu.* surface");
                ImGui::PopStyleColor();
                ImGui::SetNextItemWidth(340.0f);
                if (ImGui::InputText("##expr", w.expr, sizeof(w.expr),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    w.ok = lua_debug_eval_expr(w.expr, w.val, sizeof(w.val));
                    ImGui::CloseCurrentPopup();
                }
                if (!w.ok) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ZS_CRIT);
                    ImGui::TextWrapped("%s", w.val);
                    ImGui::PopStyleColor();
                }
                ImGui::EndPopup();
            }
            ImGui::Dummy(ImVec2(0, 4.0f));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (g_watch_count < WATCH_MAX) {
        if (ImGui::SmallButton("+ Pin an expression")) {
            snprintf(g_watches[g_watch_count].expr, sizeof(g_watches[0].expr), "emu.pc()");
            g_watches[g_watch_count].val[0] = '\0';
            g_watches[g_watch_count].ok = true;
            g_watch_count++;
            g_watch_frame = WATCH_EVAL_INTERVAL;   /* evaluate on the next draw */
        }
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("evaluated every %d frames, only while visible", WATCH_EVAL_INTERVAL);
    ImGui::PopStyleColor();
}

// Contextual inspector (Pipeline / Script modes): VRAM diff status + audio + watches.
static void draw_inspector_window(Interconnect* inter) {
    if (!ImGui::Begin("Inspector", nullptr)) { ImGui::End(); return; }

    /* The host block is the short form of the Host HW panel: which GPU has the
     * context, what the process is costing, and nothing that has to be kept in
     * step by hand. */
    host_info_sample();
    const HostInfo* host = host_info_get();
    card_header_icon("Host", ZS_OK, draw_icon_cpu);
    if (ImGui::BeginTable("hostinspect", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto kv = [](const char* k, const char* v, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
        };
        char hb[160];
        kv("GPU context", host->gl_renderer, host->gpu_request[0] && !host->gpu_request_honoured ? ZS_CRIT : ZS_DATA);
        kv("Driver", host->gl_driver, ZS_TEXT);
        snprintf(hb, sizeof(hb), "%.0f%% of one core, %.0f MB RSS", host->process_cpu_pct, host->rss_mb);
        kv("Process", hb, ZS_TEXT);
        if (host->audio_freq > 0)
            snprintf(hb, sizeof(hb), "%s %d Hz, %.1f ms buffer", host->audio_driver, host->audio_freq,
                     host_info_audio_latency_ms());
        else
            snprintf(hb, sizeof(hb), "%s (no device)", host->audio_driver);
        kv("Audio", hb, ZS_AUDIO);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6));

    card_header("VRAM: CPU vs GPU", g_vram_diff.total_diff ? ZS_DATA : ZS_FAINT);
    update_vram_diff(inter);

    ImGui::Checkbox("Compare (every 30 frames)", &g_vram_diff.enabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("Sample now") && inter && !g_vram_diff.awaiting) {
        g_vram_diff.awaiting = true;
        renderer_request_vram_readback(&inter->gpu.renderer);
    }

    if (!g_vram_diff.last_seq) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
        ImGui::TextWrapped(g_vram_diff.awaiting ? "Waiting for the GPU thread..."
                                                : "No sample taken yet.");
        ImGui::PopStyleColor();
    } else if (ImGui::BeginTable("vramdiff", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto kv = [](const char* k, const char* v, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
        };
        const uint32_t total_px = 1024u * 512u;
        char buf[48];
        snprintf(buf, sizeof(buf), "%u  (%.2f%%)", g_vram_diff.total_diff,
                 100.0 * (double)g_vram_diff.total_diff / (double)total_px);
        kv("Differing", buf, g_vram_diff.total_diff ? ZS_DATA : ZS_TEXT);
        snprintf(buf, sizeof(buf), "%u", g_vram_diff.colour_diff);
        kv("Colour bits", buf, g_vram_diff.colour_diff ? ZS_DATA : ZS_FAINT);
        snprintf(buf, sizeof(buf), "%u", g_vram_diff.mask_diff);
        kv("Mask bit only", buf, g_vram_diff.mask_diff ? ZS_DATA : ZS_FAINT);
        snprintf(buf, sizeof(buf), "%u", g_vram_diff.gpu_only);
        kv("GPU-only pixels", buf, g_vram_diff.gpu_only ? ZS_DATA : ZS_FAINT);
        if (g_vram_diff.first_x >= 0)
            snprintf(buf, sizeof(buf), "%d, %d", g_vram_diff.first_x, g_vram_diff.first_y);
        else
            snprintf(buf, sizeof(buf), "none");
        kv("First mismatch", buf, ZS_TEXT);
        snprintf(buf, sizeof(buf), "#%u", g_vram_diff.last_seq);
        kv("Sample", buf, ZS_FAINT);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 6));

    draw_audio_meters(inter);
    ImGui::Dummy(ImVec2(0, 6));

    draw_pinned_watches();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Per-mode dock layout — rebuilt when the mode changes. The emulated screen is
// pinned to the top of the stage in every mode; "Display" gives it the whole
// stage. Rail (left) and logs (bottom) are constant.
// ---------------------------------------------------------------------------

static void rebuild_layout(ImGuiID dockspace_id) {
    if (!g_layout_dirty) return;
    g_layout_dirty = false;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID root = dockspace_id;
    ImGuiID bottom = ImGui::DockBuilderSplitNode(root, ImGuiDir_Down, 0.24f, nullptr, &root);
    ImGuiID rail   = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.11f, nullptr, &root);

    // Windows this mode wants in body (stage) and inspector.
    const char* body = nullptr;   // main mode window under the screen
    const char* insp = nullptr;   // right inspector window
    const char* insp2 = nullptr;
    bool full_display = false;
    /* Event recording follows the mode, not just the window: leaving the Frame
     * mode has to stop it even if the window object still exists in the dock. */
    if (g_mode != MODE_FRAME) frame_events_set_enabled(false);
    switch (g_mode) {
        case MODE_PIPELINE: body = "Pipeline";    insp = "Inspector"; break;
        case MODE_DISPLAY:  full_display = true;  break;
        case MODE_FRAME:    body = "Frame";       break;
        case MODE_CODE:     body = "Disassembly"; insp = "CPU Registers"; insp2 = "Breakpoints"; break;
        case MODE_MEMORY:   body = "Memory";      insp = "CPU Registers"; break;
        case MODE_AUDIO:    body = "SPU Debug";   insp = "Audio Meters"; break;
        case MODE_VRAM:     body = "VRAM Viewer"; break;
        case MODE_HOST:     body = "Host HW";     insp = "Inspector"; break;
        case MODE_SCRIPT:   body = "Lua Console"; insp = "Inspector"; break;
    }

    ImGuiID inspector = 0;
    if (insp) inspector = ImGui::DockBuilderSplitNode(root, ImGuiDir_Right, 0.26f, nullptr, &root);

    ImGui::DockBuilderDockWindow("##Rail", rail);

    if (full_display) {
        ImGui::DockBuilderDockWindow("PS1 Display", root);
    } else {
        ImGuiID stage_bottom;
        /* The picture gets what the mode can spare: the overview modes keep it
         * large, the ones whose body is the work (disassembly, hex, console,
         * VRAM) give most of the height to the body. */
        float top_ratio = 0.40f;
        switch (g_mode) {
            case MODE_CODE: case MODE_MEMORY: case MODE_SCRIPT: case MODE_VRAM: top_ratio = 0.28f; break;
            case MODE_AUDIO: case MODE_HOST: top_ratio = 0.34f; break;
            default: break;
        }
        ImGuiID stage_top = ImGui::DockBuilderSplitNode(root, ImGuiDir_Up, top_ratio, nullptr, &stage_bottom);
        ImGui::DockBuilderDockWindow("PS1 Display", stage_top);
        if (body) ImGui::DockBuilderDockWindow(body, stage_bottom);
    }
    if (insp)  ImGui::DockBuilderDockWindow(insp, inspector);
    if (insp2) ImGui::DockBuilderDockWindow(insp2, inspector);

    // Logs tabbed along the bottom, every mode.
    for (auto& comp : g_log_components)
        ImGui::DockBuilderDockWindow(comp.name, bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/* --- the two ImGui backend halves, on their own ----------------------------
 *
 * ImGui is in three parts: the context (fonts, imgui.ini, the docking layout,
 * the pinned watches, the toast queue), a platform half bound to the SDL
 * window, and a renderer half bound to the graphics device. A hot backend
 * switch destroys the window and the device, so both halves have to go — but
 * the context must not, or the interface would come back with its layout reset
 * and its fonts reloaded every time somebody changed backend.
 *
 * The asymmetry between the two backends is real and not a shortcut: OpenGL's
 * renderer half is created here, on the thread that holds the context, while
 * Vulkan's needs a VkDevice that renderer_init() has not built yet, so the
 * Vulkan backend brings its own half up and tears it down with the device it
 * belongs to. That is why only the OpenGL side appears below. */
extern "C" void debug_ui_backend_init(SDL_Window* window, void* gl_context, int backend) {
    s_gfx_backend = backend;
    if (backend == GFX_BACKEND_VULKAN) {
        ImGui_ImplSDL3_InitForVulkan(window);
    } else {
        ImGui_ImplSDL3_InitForOpenGL(window, (SDL_GLContext)gl_context);
        ImGui_ImplOpenGL3_Init("#version 330");
    }
}

extern "C" void debug_ui_backend_shutdown(void) {
    /* The Vulkan renderer half is shut down by its own backend, which owns the
     * device it was built against. */
    if (s_gfx_backend != GFX_BACKEND_VULKAN) ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

extern "C" void debug_ui_init(SDL_Window* window, void* gl_context, int backend) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    /* Fonts and scale.
     *
     * The interface used to load one 15 px face and then push it again for the
     * "monospace" log windows, which is why the logs never looked monospaced:
     * Fonts[0] is the same proportional face. Three faces are loaded here — UI,
     * a display size for panel titles, and a real mono for logs, disassembly
     * and hex — each at the display's scale, because 15 px on a 1440p laptop
     * panel is a squint, not a design.
     *
     * ZS1_UI_SCALE overrides the automatic factor for anyone whose display
     * disagrees. */
    float ui_scale = 1.0f;
    if (const char* env = getenv("ZS1_UI_SCALE")) {
        float v = (float)atof(env);
        if (v >= 0.6f && v <= 3.0f) ui_scale = v;
    } else {
        float dsp = SDL_GetWindowDisplayScale(window);
        if (dsp >= 1.05f) {
            ui_scale = dsp;                 /* the compositor already says so */
        } else {
            int pw = 0, ph = 0;
            SDL_GetWindowSizeInPixels(window, &pw, &ph);
            ui_scale = (ph >= 2000) ? 1.6f : (ph >= 1300) ? 1.25f : 1.0f;
        }
    }
    g_ui_scale = ui_scale;

    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = true;

    auto load_first = [&](const char* const* paths, int n, float px) -> ImFont* {
        for (int i = 0; i < n; i++)
            if (access(paths[i], F_OK) == 0)
                return io.Fonts->AddFontFromFileTTF(paths[i], px, &font_cfg);
        return nullptr;
    };

    static const char* ui_paths[] = {
        "/usr/share/fonts/truetype/inter-zorin-os/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    static const char* ui_semibold_paths[] = {
        "/usr/share/fonts/truetype/inter-zorin-os/Inter-SemiBold.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    };
    static const char* mono_paths[] = {
        "/usr/share/fonts/truetype/jetbrains-mono-zorin-os/JetBrainsMono-Regular.ttf",
        "/usr/share/fonts/truetype/roboto-mono-zorin-os/RobotoMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };

    g_font_ui   = load_first(ui_paths,       4, 16.0f * ui_scale);
    g_font_mono = load_first(mono_paths,     4, 14.5f * ui_scale);
    g_font_h1   = load_first(ui_semibold_paths, 4, 19.0f * ui_scale);

    if (!g_font_ui) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.15f * ui_scale;
    }
    if (!g_font_mono) g_font_mono = g_font_ui;
    if (!g_font_h1)   g_font_h1   = g_font_ui;

    /* Which shell the window opens in. The gameplay shell is the default: it
     * is what a run is for. ZS1_UI=debug opens the workspace instead, which is
     * what a debugging session wants. */
    if (const char* want = getenv("ZS1_UI")) {
        if (strcmp(want, "debug") == 0 || strcmp(want, "workspace") == 0) g_shell = SHELL_DEBUG;
        else if (strcmp(want, "gameplay") == 0 || strcmp(want, "game") == 0) g_shell = SHELL_GAMEPLAY;
    }

    theme_load_initial();
    apply_zonistation_style();
    ImGui::GetStyle().ScaleAllSizes(ui_scale);

    /* The platform half of ImGui is set up here for either backend; the
     * renderer half is not symmetrical. OpenGL's is initialised right now,
     * while the context is current on this thread. Vulkan's cannot be — it
     * needs a VkDevice that renderer_init() has not created yet — so the Vulkan
     * backend brings it up itself once the device exists. The ImGui *context*
     * is created here in both cases and outlives either, which is what a hot
     * backend switch depends on. */
    debug_ui_backend_init(window, gl_context, backend);

    /* Open per-component log files (one file per category, lives for whole session) */
    mkdir("logs", 0755);
    for (auto& comp : g_log_components) {
        char path[160];
        snprintf(path, sizeof(path), "logs/%s.log", comp.name);
        comp.file = fopen(path, "w");
        if (comp.file) {
            /* Fully buffered, 64 KB. Line buffering turned every emitted line
             * into a write() on whichever thread logged it; at DEBUG that is
             * thousands of syscalls per field on the emulation thread. The
             * buffer is emptied once per field (debug_ui_flush_logs) and by
             * the signal handlers in main.c, so a crash or a Ctrl+C still
             * leaves everything up to the last field on disk. */
            setvbuf(comp.file, NULL, _IOFBF, 64 * 1024);
        }
    }

    log_add_sink(log_sink_callback, nullptr);
}

extern "C" void debug_ui_process_event(SDL_Event* event) {
    ImGui_ImplSDL3_ProcessEvent(event);
}

// ---------------------------------------------------------------------------
// Gameplay shell
//
// The debug workspace is the right interface for finding a bug and the wrong
// one for playing a game: nine view modes, a log dock and an inspector are
// noise when the answer wanted is "does this game run". So the same window has
// two shells. This one shows the emulated screen and nothing else until asked,
// and everything it does show is read from the machine — there is no state here
// that a game could contradict.
//
// Keys: Esc opens the quick menu and steps back out of it, the backquote (or
// F1) hands the window to the debug workspace, F5/F8 save and load the current
// slot and F6/F7 change it, F2 cycles the overlay, F10 pauses, F11 is
// fullscreen, F12 is the Analog button. On the pad, Select + Start opens the
// menu and the D-pad, Cross and Circle drive it. ZS1_UI=debug boots into the
// workspace instead.
// ---------------------------------------------------------------------------

static bool   g_gp_menu_open     = false;
static int    g_gp_menu_idx      = 0;
static int    g_gp_slot          = 0;
static double g_gp_last_input_t  = 0.0;
static ImVec2 g_gp_last_mouse    = ImVec2(0, 0);

/* Where the menu's focus is. The list on the left and the pane on the right
 * are one navigation space driven by the same six inputs (up, down, left,
 * right, confirm, back) whether they come from the keyboard or from the pad,
 * so every control the mouse can reach has an index the other two can reach.
 * The pane's buttons number themselves as they are drawn (g_gp_pane_next) and
 * say how many sit on a row (g_gp_pane_cols). */
static int g_gp_focus      = 0;      /* 0 = list, 1 = pane */
static int g_gp_pane_idx   = 0;
static int g_gp_pane_next  = 0;
static int g_gp_pane_count = 0;
static int g_gp_pane_cols  = 1;

/* A destructive slot action asks twice: the first press arms it, the second
 * within four seconds carries it out. */
static int    g_gp_arm_kind = 0;     /* 0 none, 1 overwrite, 2 load */
static int    g_gp_arm_slot = -1;
static double g_gp_arm_t    = 0.0;

enum { GP_HUD_AUTO = 0, GP_HUD_ALWAYS, GP_HUD_OFF, GP_HUD_COUNT };
static int  g_gp_hud_mode = GP_HUD_AUTO;
static bool g_gp_graph    = true;
static const char* const g_gp_hud_names[GP_HUD_COUNT] = { "Auto-hide", "Always on", "Off" };

/* True while the machine is paused because the menu is up, as opposed to
 * paused by F10 or by the debugger — only the first is undone on close. */
static bool g_gp_menu_paused = false;

/* The pad word the host read this frame (active low, as the SIO sends it) and
 * last frame's, for edges. g_pad_hold keeps the pad away from the game after
 * the interface gives it back until every button has been let go, so the press
 * that closed the menu is not also a press in the game. */
static uint16_t g_pad_word = 0xFFFF;
static uint16_t g_pad_prev = 0xFFFF;
static bool     g_pad_hold = false;

static bool g_req_fullscreen  = false;
static bool g_ws_quit_confirm = false;

struct GpNav { bool up, down, left, right, ok, back; };
static GpNav g_nav = { false, false, false, false, false, false };

static ImU32 gp_col(ImVec4 c, float a) { c.w *= a; return ImGui::GetColorU32(c); }

/* The emblem: the four pad glyphs on a ring. Drawn, not loaded — there is no
 * image asset in the tree, and the console maker's own logo is a registered
 * mark that a GPL project has no licence to ship. To use an asset instead,
 * this is the one function to replace. */
static void draw_zs_emblem(ImDrawList* dl, ImVec2 c, float r, float a) {
    float g  = r * 0.27f;
    float d  = r * 0.54f;
    float th = r * 0.085f; if (th < 1.3f) th = 1.3f;
    dl->AddCircleFilled(c, r, IM_COL32(10, 13, 20, (int)(235 * a)), 48);
    dl->AddCircle(c, r, gp_col(ZS_DATA, a * 0.75f), 48, th * 0.8f);
    draw_ps_triangle(dl, ImVec2(c.x, c.y - d), g, gp_col(ZS_PS_GREEN, a), th);
    draw_ps_circle  (dl, ImVec2(c.x + d, c.y), g, gp_col(ZS_PS_RED,   a), th);
    draw_ps_cross   (dl, ImVec2(c.x, c.y + d), g, gp_col(ZS_PS_BLUE,  a), th);
    draw_ps_square  (dl, ImVec2(c.x - d, c.y), g, gp_col(ZS_PS_PINK,  a), th);
}

/* A key cap and what it does. Returns the x after it. */
static float gp_keycap(ImDrawList* dl, ImVec2 p, const char* key, const char* label, float a) {
    ImFont* m = g_font_mono ? g_font_mono : ImGui::GetFont();
    ImFont* f = g_font_ui ? g_font_ui : ImGui::GetFont();
    float s  = g_ui_scale;
    float fs = m->LegacySize * 0.90f;
    float kw = m->CalcTextSizeA(fs, FLT_MAX, 0.0f, key).x + 12.0f * s;
    float kh = fs + 6.0f * s;
    dl->AddRectFilled(p, ImVec2(p.x + kw, p.y + kh), IM_COL32(26, 35, 52, (int)(235 * a)), 4.0f * s);
    dl->AddRect(p, ImVec2(p.x + kw, p.y + kh), IM_COL32(62, 84, 120, (int)(255 * a)), 4.0f * s);
    dl->AddText(m, fs, ImVec2(p.x + 6.0f * s, p.y + 3.0f * s), gp_col(ZS_TEXT, a), key);
    float x = p.x + kw + 6.0f * s;
    float ls = f->LegacySize * 0.88f;
    dl->AddText(f, ls, ImVec2(x, p.y + (kh - ls) * 0.5f), gp_col(ZS_MUTED, a), label);
    return x + f->CalcTextSizeA(ls, FLT_MAX, 0.0f, label).x + 16.0f * s;
}

/* One-shot requests for the host loop: the shell does not own the machine, it
 * asks. debug_ui_take_* hand them over and clear them. */
static bool g_req_quit        = false;
static bool g_req_state       = false;
static bool g_req_state_save  = false;
static char g_req_state_path[192] = {0};

/* The pending graphics-backend change, and the device list it is chosen from.
 *
 * The list is enumerated once and cached because asking costs a whole
 * VkInstance — created, queried and destroyed — which is not something to do
 * every frame a panel is visible. It cannot change during a session anyway:
 * hot-plugging a GPU is not a case this emulator has. */
static bool             g_req_gfx = false;
static GfxDeviceRequest g_req_gfx_what = { GFX_BACKEND_GL33, -1, 1 };

static GfxDeviceInfo g_gfx_devs[2][8];
static int           g_gfx_dev_count[2] = { -1, -1 };   /* -1 = not asked yet */

static const GfxDeviceInfo* gfx_devices(GfxBackendType type, int* count) {
    int idx = (int)type;
    if (idx < 0 || idx > 1) { *count = 0; return nullptr; }
    if (g_gfx_dev_count[idx] < 0)
        g_gfx_dev_count[idx] = renderer_enumerate_devices(type, g_gfx_devs[idx], 8);
    *count = g_gfx_dev_count[idx];
    return g_gfx_devs[idx];
}

/* What is running right now, so the panel can mark it rather than guess. The
 * device index is what was asked for at the last switch; -1 means the backend
 * picked, which is the honest answer at startup. */
static int g_gfx_live_device = -1;

static void gp_request_gfx(GfxBackendType type, int device_index, uint32_t scale) {
    g_req_gfx = true;
    g_req_gfx_what.type           = type;
    g_req_gfx_what.device_index   = device_index;
    g_req_gfx_what.internal_scale = scale ? scale : 1;
}

struct GpToast { char msg[96]; char sub[80]; double born; ImVec4 col; };
static GpToast g_gp_toasts[4];
static int     g_gp_toast_count = 0;

static void gp_toast(const char* msg, const char* sub, ImVec4 col) {
    if (g_gp_toast_count == 4) {
        memmove(&g_gp_toasts[0], &g_gp_toasts[1], sizeof(GpToast) * 3);
        g_gp_toast_count = 3;
    }
    GpToast& t = g_gp_toasts[g_gp_toast_count++];
    snprintf(t.msg, sizeof(t.msg), "%s", msg ? msg : "");
    snprintf(t.sub, sizeof(t.sub), "%s", sub ? sub : "");
    t.born = ImGui::GetTime();
    t.col = col;
}

static void gp_note_input(void) { g_gp_last_input_t = ImGui::GetTime(); }

static const char* gp_slot_path(int slot) {
    static char path[64];
    snprintf(path, sizeof(path), "savestates/slot%d.zst", slot);
    return path;
}

/* mtime and size of a slot file, so a slot tile can say when it was written
 * rather than claiming something. */
static bool gp_slot_info(int slot, char* out, size_t n) {
    struct stat st;
    if (stat(gp_slot_path(slot), &st) != 0) return false;
    struct tm tmv;
    localtime_r(&st.st_mtime, &tmv);
    char when[32];
    strftime(when, sizeof(when), "%d %b %H:%M", &tmv);
    snprintf(out, n, "%s  %.1f MB", when, (double)st.st_size / (1024.0 * 1024.0));
    return true;
}

static void gp_request_state(bool save, int slot) {
    g_req_state = true;
    g_req_state_save = save;
    snprintf(g_req_state_path, sizeof(g_req_state_path), "%s", gp_slot_path(slot));
}

// --- HUD ---------------------------------------------------------------------

static void gp_draw_hud(Interconnect* inter, float alpha) {
    if (alpha <= 0.01f) return;
    float s = g_ui_scale;

    bool   pal   = inter && inter->gpu.vmode == Pal;
    double hz    = nominal_hz(inter);
    /* Fields per real second, counted over half a second — see field_rate_hz(). */
    double fps   = field_rate_hz(inter);
    double speed = (hz > 0.1) ? (fps / hz) * 100.0 : 0.0;
    ImVec4 scol  = speed >= 98.0 ? ZS_OK : (speed >= 90.0 ? ZS_WARN : ZS_CRIT);

    SioPadMode pad = inter ? sio_get_pad_mode(&inter->sio) : SIO_PAD_DIGITAL;
    ImVec4 led = (pad == SIO_PAD_ANALOG) ? ZS_CRIT : (pad == SIO_PAD_STICK) ? ZS_OK : ZS_FAINT;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* mono = g_font_mono ? g_font_mono : ImGui::GetFont();
    ImFont* h1   = g_font_h1 ? g_font_h1 : ImGui::GetFont();

    /* --- top right: the performance block --- */
    float pad_in = 12.0f * s;
    float big    = 30.0f * s;
    float small  = mono->LegacySize * 0.92f;
    /* The second line carries the field time and region on the left and the pad
     * mode on the right; the block is as wide as both need, not a fixed 244. */
    char  line2[64];
    snprintf(line2, sizeof(line2), "%.1f ms  %s %.2f Hz", g_vit_frame_ema, pal ? "PAL" : "NTSC", hz);
    const char* pname = inter ? sio_pad_mode_name(pad) : "pad";
    float line2_w = mono->CalcTextSizeA(small, FLT_MAX, 0.0f, line2).x;
    float pname_w = mono->CalcTextSizeA(small, FLT_MAX, 0.0f, pname).x;
    float w = pad_in * 2.0f + 2.0f * s + line2_w + 22.0f * s + pname_w + 12.0f * s;
    if (w < 244.0f * s) w = 244.0f * s;
    float gh     = g_gp_graph ? 30.0f * s : 0.0f;
    float h      = pad_in * 2.0f + big + 4.0f * s + small + (g_gp_graph ? gh + 9.0f * s : 0.0f);

    ImVec2 p1 = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 18.0f * s, vp->WorkPos.y + 18.0f * s + h);
    ImVec2 p0 = ImVec2(p1.x - w, p1.y - h);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 11, 18, (int)(215 * alpha)), 8.0f * s);
    dl->AddRect(p0, p1, IM_COL32(40, 56, 82, (int)(255 * alpha)), 8.0f * s);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + 8.0f * s), ImVec2(p0.x + 3.0f * s, p1.y - 8.0f * s),
                      gp_col(scol, alpha), 2.0f);

    float x = p0.x + pad_in + 2.0f * s;
    float y = p0.y + pad_in;
    char b[64];

    snprintf(b, sizeof(b), "%.1f", fps);
    dl->AddText(h1, big, ImVec2(x, y), gp_col(ZS_TEXT, alpha), b);
    float fw = h1->CalcTextSizeA(big, FLT_MAX, 0.0f, b).x;
    dl->AddText(mono, small, ImVec2(x + fw + 6.0f * s, y + big - small - 4.0f * s),
                gp_col(ZS_FAINT, alpha), "FPS");

    snprintf(b, sizeof(b), "%.0f%%", speed);
    float sfs = big * 0.72f;
    float sw  = h1->CalcTextSizeA(sfs, FLT_MAX, 0.0f, b).x;
    dl->AddText(h1, sfs, ImVec2(p1.x - pad_in - sw, y + big - sfs - 2.0f * s), gp_col(scol, alpha), b);
    y += big + 4.0f * s;

    dl->AddText(mono, small, ImVec2(x, y), gp_col(ZS_MUTED, alpha), line2);
    float nw = pname_w;
    dl->AddText(mono, small, ImVec2(p1.x - pad_in - nw, y), gp_col(ZS_MUTED, alpha), pname);
    dl->AddCircleFilled(ImVec2(p1.x - pad_in - nw - 9.0f * s, y + small * 0.5f), 4.0f * s,
                        gp_col(led, alpha), 12);
    y += small + 9.0f * s;

    /* Frame-time graph: one bar per host loop iteration against the field
     * budget (the line at half height). A single iteration swings around the
     * mean by design — the pacing wait quantises to the millisecond — so the
     * warning colours start well above the budget, not at it. */
    if (g_gp_graph && g_vit_budget_ms > 0.001) {
        float gw = w - pad_in * 2.0f - 2.0f * s;
        float bw = gw / (float)ZS_FT_SAMPLES;
        float top = (float)(g_vit_budget_ms * 2.0);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + gw, y + gh), IM_COL32(14, 19, 30, (int)(200 * alpha)), 3.0f);
        for (int i = 0; i < ZS_FT_SAMPLES; i++) {
            float v = g_ft[(g_ft_head + i) % ZS_FT_SAMPLES];
            if (v <= 0.0f) continue;
            float r = v / top; if (r > 1.0f) r = 1.0f;
            double over = (double)v / g_vit_budget_ms;
            ImVec4 c = over > 1.8 ? ZS_CRIT : (over > 1.35 ? ZS_WARN : ZS_DATA);
            dl->AddRectFilled(ImVec2(x + bw * (float)i, y + gh * (1.0f - r)),
                              ImVec2(x + bw * (float)(i + 1), y + gh), gp_col(c, alpha * 0.85f));
        }
        dl->AddLine(ImVec2(x, y + gh * 0.5f), ImVec2(x + gw, y + gh * 0.5f),
                    IM_COL32(148, 163, 184, (int)(110 * alpha)), 1.0f);
    }

    /* --- top left: what is running --- */
    {
        float er = 17.0f * s;
        ImVec2 c = ImVec2(vp->WorkPos.x + 20.0f * s + er, vp->WorkPos.y + 20.0f * s + er);
        draw_zs_emblem(dl, c, er, alpha);
        const char* title = (g_disc_name[0] && strcmp(g_disc_name, "n/a")) ? g_disc_name
                                                                            : "No disc - BIOS shell";
        float ts = h1->LegacySize;
        dl->AddText(h1, ts, ImVec2(c.x + er + 10.0f * s + 1.0f, c.y - ts * 0.95f + 1.0f),
                    IM_COL32(0, 0, 0, (int)(200 * alpha)), title);
        dl->AddText(h1, ts, ImVec2(c.x + er + 10.0f * s, c.y - ts * 0.95f), gp_col(ZS_TEXT, alpha), title);
        snprintf(b, sizeof(b), "ZONISTATION ONE   SLOT %d", g_gp_slot);
        dl->AddText(mono, small, ImVec2(c.x + er + 10.0f * s, c.y + 3.0f * s), gp_col(ZS_DATA, alpha * 0.9f), b);
    }

    /* --- bottom left: what the keys do --- */
    if (!g_gp_menu_open) {
        ImVec2 p = ImVec2(vp->WorkPos.x + 20.0f * s,
                          vp->WorkPos.y + vp->WorkSize.y - 20.0f * s - (mono->LegacySize * 0.90f + 6.0f * s));
        /* A plate behind the strip: over a bright picture (a white logo, a menu
         * screen) the muted labels vanished. The plate is sized from the same
         * measurements the keycaps use, so it cannot drift from them. */
        {
            static const char* const cap_keys[8]   = { "Esc", "F5", "F8", "F6 F7", "F10", "F11", "F2", "`" };
            static const char* const cap_labels[8] = { "Menu", "Save", "Load", "Slot", "Pause", "Fullscreen", "HUD", "Debug" };
            ImFont* uif = g_font_ui ? g_font_ui : ImGui::GetFont();
            float fsz = mono->LegacySize * 0.90f, lsz = uif->LegacySize * 0.88f, tot = 0.0f;
            for (int i = 0; i < 8; i++)
                tot += mono->CalcTextSizeA(fsz, FLT_MAX, 0.0f, cap_keys[i]).x + 12.0f * s + 6.0f * s +
                       uif->CalcTextSizeA(lsz, FLT_MAX, 0.0f, cap_labels[i]).x + 16.0f * s;
            dl->AddRectFilled(ImVec2(p.x - 10.0f * s, p.y - 7.0f * s),
                              ImVec2(p.x + tot - 6.0f * s, p.y + fsz + 6.0f * s + 7.0f * s),
                              IM_COL32(6, 9, 15, (int)(190 * alpha)), 8.0f * s);
        }
        p.x = gp_keycap(dl, p, "Esc", "Menu", alpha);
        p.x = gp_keycap(dl, p, "F5", "Save", alpha);
        p.x = gp_keycap(dl, p, "F8", "Load", alpha);
        p.x = gp_keycap(dl, p, "F6 F7", "Slot", alpha);
        p.x = gp_keycap(dl, p, "F10", "Pause", alpha);
        p.x = gp_keycap(dl, p, "F11", "Fullscreen", alpha);
        p.x = gp_keycap(dl, p, "F2", "HUD", alpha);
        p.x = gp_keycap(dl, p, "`", "Debug", alpha);
    }
}

/* The machine is stopped and the menu is not the reason: say so, and say how
 * to start it again. Without this a pause taken in the workspace reads as a
 * hang from the gameplay shell. */
static void gp_draw_paused(void) {
    float s = g_ui_scale;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* h1 = g_font_h1 ? g_font_h1 : ImGui::GetFont();
    float fs = h1->LegacySize * 1.15f;
    const char* t = "PAUSED";
    float tw = h1->CalcTextSizeA(fs, FLT_MAX, 0.0f, t).x;
    float w = tw + 150.0f * s, h = fs + 18.0f * s;
    ImVec2 p0 = ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + 22.0f * s);
    ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);
    dl->AddRectFilled(p0, p1, IM_COL32(8, 11, 18, 230), h * 0.5f);
    dl->AddRect(p0, p1, gp_col(ZS_WARN, 0.9f), h * 0.5f, 0, 1.5f);
    float bx = p0.x + 18.0f * s, by = p0.y + 9.0f * s;
    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + 4.0f * s, by + fs), gp_col(ZS_WARN, 1.0f));
    dl->AddRectFilled(ImVec2(bx + 8.0f * s, by), ImVec2(bx + 12.0f * s, by + fs), gp_col(ZS_WARN, 1.0f));
    dl->AddText(h1, fs, ImVec2(bx + 22.0f * s, by), gp_col(ZS_TEXT, 1.0f), t);
    gp_keycap(dl, ImVec2(bx + 34.0f * s + tw, by + 1.0f * s), "F10", "Resume", 1.0f);
}

static void gp_draw_toasts(void) {
    if (!g_gp_toast_count) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* f = g_font_ui ? g_font_ui : ImGui::GetFont();
    ImFont* m = g_font_mono ? g_font_mono : f;
    double now = ImGui::GetTime();

    float y = vp->WorkPos.y + vp->WorkSize.y - 70.0f * g_ui_scale;
    for (int i = g_gp_toast_count - 1; i >= 0; i--) {
        GpToast& t = g_gp_toasts[i];
        double age = now - t.born;
        if (age > 3.0) {
            memmove(&g_gp_toasts[i], &g_gp_toasts[i + 1], sizeof(GpToast) * (size_t)(g_gp_toast_count - i - 1));
            g_gp_toast_count--;
            continue;
        }
        float a = (age > 2.4) ? (float)(1.0 - (age - 2.4) / 0.6) : 1.0f;

        float fs = f->LegacySize;
        float msg_w = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, t.msg).x;
        float sub_w = t.sub[0] ? m->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, t.sub).x : 0.0f;
        float w = (msg_w > sub_w ? msg_w : sub_w) + 28.0f * g_ui_scale;
        float h = fs + (t.sub[0] ? fs * 0.95f : 0.0f) + 16.0f * g_ui_scale;

        ImVec2 p1 = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 18.0f * g_ui_scale, y);
        ImVec2 p0 = ImVec2(p1.x - w, y - h);
        dl->AddRectFilled(p0, p1, IM_COL32(19, 25, 36, (int)(240 * a)), 5.0f);
        dl->AddRect(p0, p1, IM_COL32(40, 56, 82, (int)(255 * a)), 5.0f);
        ImVec4 lc = t.col; lc.w = a;
        dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), ImGui::GetColorU32(lc), 2.0f);

        ImVec4 tc = ZS_TEXT; tc.w = a;
        dl->AddText(f, fs, ImVec2(p0.x + 12.0f * g_ui_scale, p0.y + 7.0f * g_ui_scale),
                    ImGui::GetColorU32(tc), t.msg);
        if (t.sub[0]) {
            ImVec4 sc = ZS_FAINT; sc.w = a;
            dl->AddText(m, fs * 0.85f,
                        ImVec2(p0.x + 12.0f * g_ui_scale, p0.y + 7.0f * g_ui_scale + fs),
                        ImGui::GetColorU32(sc), t.sub);
        }
        y = p0.y - 8.0f * g_ui_scale;
    }
}

// --- quick menu ---------------------------------------------------------------

struct GpMenuItem { const char* label; const char* key; bool danger; };

/* Named, because the pane below and the navigation both switch on the index
 * and a bare number breaks silently the moment an entry is inserted. */
enum {
    GP_RESUME = 0, GP_SAVE, GP_LOAD, GP_PADS,
    GP_VIDEO, GP_MACHINE, GP_WORKSPACE, GP_QUIT
};

static const GpMenuItem g_gp_items[] = {
    { "Resume",           "Esc",  false },
    { "Save state",       "F5",   false },
    { "Load state",       "F8",   false },
    { "Controllers",      "",     false },
    { "Video & HUD",      "",     false },
    { "Machine",          "",     false },
    { "Debug workspace",  "`",    false },
    { "Quit",             "",     true  },
};
static const int GP_ITEM_COUNT = (int)(sizeof(g_gp_items) / sizeof(g_gp_items[0]));

static void gp_open_menu(void) {
    g_gp_menu_open = true;
    g_gp_menu_idx  = 0;
    g_gp_focus     = 0;
    g_gp_arm_kind  = 0;
    gp_note_input();
}

static void gp_close_menu(void) {
    g_gp_menu_open = false;
    g_gp_focus     = 0;
    g_gp_arm_kind  = 0;
    g_pad_hold     = true;
    gp_note_input();
}

static bool gp_mouse_moved(void) {
    ImVec2 d = ImGui::GetIO().MouseDelta;
    return d.x != 0.0f || d.y != 0.0f;
}

/* A pane control. It takes the next index in the pane, shows the focus ring
 * when the navigation is on it, and fires on a click or on confirm — one
 * widget, so nothing in a pane is reachable by the mouse alone. A disabled
 * control still takes focus (the cursor has to be able to cross it) and never
 * fires. `on` marks the live choice of a group. */
static bool gp_btn(const char* label, const char* sub, ImVec2 size, bool disabled, bool on, ImVec4 accent) {
    float s = g_ui_scale;
    int idx = g_gp_pane_next++;
    if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
    ImGui::PushID(idx);
    bool clicked = ImGui::InvisibleButton("##gpb", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hov && gp_mouse_moved()) { g_gp_focus = 1; g_gp_pane_idx = idx; }
    bool focused = (g_gp_focus == 1 && g_gp_pane_idx == idx);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float a = disabled ? 0.45f : 1.0f;
    dl->AddRectFilled(p0, p1, gp_col(on ? ZS_MODE_ON : ZS_PANEL2, focused ? 1.0f : 0.85f), 6.0f * s);
    if (focused) {
        dl->AddRectFilled(p0, p1, gp_col(accent, 0.10f), 6.0f * s);
        dl->AddRect(p0, p1, gp_col(accent, 0.95f), 6.0f * s, 0, 1.6f * s);
    } else {
        dl->AddRect(p0, p1, gp_col(on ? accent : ZS_LINE, on ? 0.55f : 1.0f), 6.0f * s);
    }
    if (on) dl->AddRectFilled(ImVec2(p0.x + 4.0f * s, p0.y + 7.0f * s),
                              ImVec2(p0.x + 7.0f * s, p1.y - 7.0f * s), gp_col(accent, 1.0f), 2.0f);

    ImFont* f = g_font_ui ? g_font_ui : ImGui::GetFont();
    ImFont* m = g_font_mono ? g_font_mono : f;
    float fs = f->LegacySize, ss = m->LegacySize * 0.88f;
    float th = fs + ((sub && sub[0]) ? ss + 3.0f * s : 0.0f);
    float tx = p0.x + 16.0f * s, ty = p0.y + (size.y - th) * 0.5f;
    dl->PushClipRect(p0, p1, true);
    dl->AddText(f, fs, ImVec2(tx, ty), gp_col(on ? accent : ZS_TEXT, a), label);
    if (sub && sub[0])
        dl->AddText(m, ss, ImVec2(tx, ty + fs + 3.0f * s), gp_col(ZS_MUTED, a), sub);
    dl->PopClipRect();

    if (focused && g_nav.ok) clicked = true;
    return clicked && !disabled;
}

static void gp_pane_text(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6.0f * g_ui_scale));
}

static void gp_draw_slots(bool save_mode) {
    float s = g_ui_scale;
    g_gp_pane_cols = 2;
    gp_pane_text(save_mode
        ? "Writes the whole machine: CPU, RAM, VRAM, SPU RAM, the drive. F5 and F8 use the "
          "highlighted slot; F6 and F7 change it. Overwriting asks twice."
        : "Loading replaces the running session, so it asks twice. States written by an older "
          "format are refused rather than loaded.");

    double now = ImGui::GetTime();
    if (g_gp_arm_kind && now - g_gp_arm_t > 4.0) g_gp_arm_kind = 0;

    float gap = 10.0f * s;
    float bw = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int i = 0; i < 4; i++) {
        if (i % 2) ImGui::SameLine(0, gap);
        char info[80];
        bool exists = gp_slot_info(i, info, sizeof(info));
        bool armed  = (g_gp_arm_kind == (save_mode ? 1 : 2)) && g_gp_arm_slot == i;
        char label[48];
        snprintf(label, sizeof(label), "Slot %d%s", i, i == g_gp_slot ? "   F5 / F8" : "");
        const char* sub = armed ? (save_mode ? "Press again to overwrite" : "Press again to load")
                                : (exists ? info : "empty");
        if (gp_btn(label, sub, ImVec2(bw, 66.0f * s), !save_mode && !exists,
                   i == g_gp_slot, armed ? ZS_WARN : ZS_DATA)) {
            if (save_mode && (!exists || armed)) {
                g_gp_slot = i; g_gp_arm_kind = 0;
                gp_request_state(true, i);
            } else if (!save_mode && armed) {
                g_gp_slot = i;
                gp_request_state(false, i);
                gp_close_menu();
            } else {
                g_gp_arm_kind = save_mode ? 1 : 2;
                g_gp_arm_slot = i;
                g_gp_arm_t    = now;
            }
        }
        if (i % 2) ImGui::Dummy(ImVec2(0, 2.0f * s));
    }
}

static void gp_draw_menu(Cpu* cpu, Interconnect* inter) {
    (void)cpu;
    float s = g_ui_scale;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImFont* h1   = g_font_h1 ? g_font_h1 : ImGui::GetFont();
    ImFont* mono = g_font_mono ? g_font_mono : ImGui::GetFont();
    ImFont* ui   = g_font_ui ? g_font_ui : ImGui::GetFont();

    ImVec2 size = ImVec2(vp->WorkSize.x * 0.80f, vp->WorkSize.y * 0.76f);
    if (size.x > 1060.0f * s) size.x = 1060.0f * s;
    if (size.y > 660.0f * s)  size.y = 660.0f * s;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
                                   vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f));
    ImGui::SetNextWindowSize(size);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, COLA(0x0B, 0x0F, 0x18, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ZS_LINE);
    bool open = ImGui::Begin("##gp_menu", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    if (!open) { ImGui::End(); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 w0 = ImGui::GetWindowPos();
    ImVec2 w1 = ImVec2(w0.x + size.x, w0.y + size.y);
    float hh = 70.0f * s, fh = 38.0f * s;
    bool pal = inter && inter->gpu.vmode == Pal;
    char buf[192];

    /* Header: the emblem, what is running, and the state of the machine. */
    dl->AddRectFilledMultiColor(ImVec2(w0.x + 1, w0.y + 1), ImVec2(w1.x - 1, w0.y + hh),
        gp_col(ZS_DATA, 0.13f), gp_col(ZS_AUDIO, 0.09f),
        gp_col(ZS_AUDIO, 0.02f), gp_col(ZS_DATA, 0.02f));
    dl->AddLine(ImVec2(w0.x, w0.y + hh), ImVec2(w1.x, w0.y + hh), gp_col(ZS_LINE, 1.0f));
    dl->AddRectFilled(ImVec2(w0.x + 18.0f * s, w0.y + hh - 2.0f), ImVec2(w0.x + 150.0f * s, w0.y + hh + 1.0f),
                      gp_col(ZS_DATA, 1.0f));
    draw_zs_emblem(dl, ImVec2(w0.x + 42.0f * s, w0.y + hh * 0.5f), 22.0f * s, 1.0f);
    {
        const char* title = (g_disc_name[0] && strcmp(g_disc_name, "n/a")) ? g_disc_name
                                                                            : "No disc - BIOS shell";
        float ts = h1->LegacySize * 1.25f;
        float tx = w0.x + 78.0f * s;
        dl->PushClipRect(ImVec2(tx, w0.y), ImVec2(w1.x - 190.0f * s, w0.y + hh), true);
        dl->AddText(h1, ts, ImVec2(tx, w0.y + 13.0f * s), gp_col(ZS_TEXT, 1.0f), title);
        snprintf(buf, sizeof(buf), "ZONISTATION ONE   %s   %s", pal ? "PAL" : "NTSC", g_bios_name);
        dl->AddText(mono, mono->LegacySize * 0.9f, ImVec2(tx, w0.y + 15.0f * s + ts),
                    gp_col(ZS_FAINT, 1.0f), buf);
        dl->PopClipRect();

        const char* st = "MACHINE PAUSED";
        float ss = mono->LegacySize * 0.9f;
        float sw = mono->CalcTextSizeA(ss, FLT_MAX, 0.0f, st).x;
        ImVec2 c0 = ImVec2(w1.x - 22.0f * s - sw - 20.0f * s, w0.y + hh * 0.5f - ss * 0.5f - 5.0f * s);
        ImVec2 c1 = ImVec2(w1.x - 22.0f * s, c0.y + ss + 10.0f * s);
        dl->AddRectFilled(c0, c1, gp_col(ZS_WARN, 0.12f), 4.0f * s);
        dl->AddRect(c0, c1, gp_col(ZS_WARN, 0.7f), 4.0f * s);
        dl->AddText(mono, ss, ImVec2(c0.x + 10.0f * s, c0.y + 5.0f * s), gp_col(ZS_WARN, 1.0f), st);
    }

    /* --- the list --- */
    bool list_ok = (g_gp_focus == 0 && g_nav.ok);
    float list_w = 236.0f * s;
    float body_h = size.y - hh - fh;
    ImGui::SetCursorScreenPos(ImVec2(w0.x, w0.y + hh + 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * s, 12.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3.0f * s));
    ImGui::BeginChild("##gp_list", ImVec2(list_w, body_h - 1.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    {
        ImDrawList* ldl = ImGui::GetWindowDrawList();
        for (int i = 0; i < GP_ITEM_COUNT; i++) {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x, rh = 38.0f * s;
            ImVec2 p1 = ImVec2(p0.x + rw, p0.y + rh);
            ImGui::PushID(i);
            bool click = ImGui::InvisibleButton("##it", ImVec2(rw, rh));
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();
            /* The pointer selects only when it moves: a cursor left resting on
             * the list must not fight the arrow keys. */
            if ((hov && gp_mouse_moved() && g_gp_menu_idx != i) || click) {
                if (g_gp_menu_idx != i) g_gp_arm_kind = 0;
                g_gp_menu_idx = i;
                g_gp_focus = 0;
            }
            if (click) list_ok = true;

            bool sel = (i == g_gp_menu_idx);
            ImVec4 acc = g_gp_items[i].danger ? ZS_CRIT : ZS_DATA;
            if (sel) {
                float k = (g_gp_focus == 0) ? 1.0f : 0.45f;
                ldl->AddRectFilledMultiColor(p0, p1, gp_col(acc, 0.26f * k), gp_col(acc, 0.03f * k),
                                             gp_col(acc, 0.03f * k), gp_col(acc, 0.26f * k));
                ldl->AddRectFilled(p0, ImVec2(p0.x + 3.0f * s, p1.y), gp_col(acc, k));
            }
            float fs = ui->LegacySize;
            ldl->AddText(ui, fs, ImVec2(p0.x + 16.0f * s, p0.y + (rh - fs) * 0.5f),
                         gp_col(sel ? (g_gp_items[i].danger ? ZS_CRIT : ZS_TEXT) : ZS_MUTED, 1.0f),
                         g_gp_items[i].label);
            if (g_gp_items[i].key[0]) {
                float ks = mono->LegacySize * 0.88f;
                float kw = mono->CalcTextSizeA(ks, FLT_MAX, 0.0f, g_gp_items[i].key).x;
                ldl->AddText(mono, ks, ImVec2(p1.x - kw - 10.0f * s, p0.y + (rh - ks) * 0.5f),
                             gp_col(ZS_FAINT, 1.0f), g_gp_items[i].key);
            }
        }
    }
    ImGui::EndChild();
    dl->AddLine(ImVec2(w0.x + list_w, w0.y + hh), ImVec2(w0.x + list_w, w1.y - fh), gp_col(ZS_LINE_SOFT, 1.0f));

    /* --- the pane --- */
    ImGui::SetCursorScreenPos(ImVec2(w0.x + list_w + 1.0f, w0.y + hh + 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * s, 16.0f * s));
    ImGui::BeginChild("##gp_pane", ImVec2(size.x - list_w - 2.0f, body_h - 1.0f),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar();

    g_gp_pane_next = 0;
    g_gp_pane_cols = 1;
    ImVec2 bsz = ImVec2(200.0f * s, 38.0f * s);

    switch (g_gp_menu_idx) {
        case GP_RESUME: {  /* the session, from counters that already exist */
            card_header_icon("This session", ZS_DATA, draw_icon_clock);
            if (ImGui::BeginTable("gp_sess", 2, ImGuiTableFlags_SizingStretchProp)) {
                double hz = nominal_hz(inter), fps = field_rate_hz(inter);
                double speed = (hz > 0.1) ? (fps / hz) * 100.0 : 0.0;
                /* The menu stops the machine, so a rate and a drained audio queue
                 * are the expected state here, not a fault: say so in the
                 * quiet colour instead of the warning one. */
                bool halted = inter && inter->debugger.paused;
                if (halted) snprintf(buf, sizeof(buf), "paused  (%.2f fields/s nominal)", hz);
                else        snprintf(buf, sizeof(buf), "%.0f%%  (%.1f of %.2f fields/s)", speed, fps, hz);
                gp_row("Speed", buf, halted ? ZS_MUTED : (speed >= 98.0 ? ZS_OK : ZS_WARN));
                int secs = (int)ImGui::GetTime();
                snprintf(buf, sizeof(buf), "%d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60);
                gp_row("Session", buf, ZS_TEXT);
                snprintf(buf, sizeof(buf), "%.1f ms of %.1f ms", g_vit_frame_ema, g_vit_budget_ms);
                gp_row("Field time", buf, ZS_TEXT);
                snprintf(buf, sizeof(buf), "%u sectors read, %s",
                         inter ? inter->cdrom.sectors_read_total : 0u,
                         inter && inter->cdrom.double_speed ? "2x" : "1x");
                gp_row("Disc", buf, ZS_TEXT);
                if (halted) snprintf(buf, sizeof(buf), "paused  (%d samples queued)", g_vit_aq);
                else        snprintf(buf, sizeof(buf), "%d / %d samples queued", g_vit_aq, g_vit_aq_target);
                gp_row("Audio", buf, halted ? ZS_MUTED : ZS_AUDIO);
                snprintf(buf, sizeof(buf), "Slot %d", g_gp_slot);
                gp_row("Quick state", buf, ZS_DATA);
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0, 10.0f * s));
            gp_pane_text("The machine is paused while this menu is open and picks up where it "
                         "stopped when it closes.");
            break;
        }
        case GP_SAVE:
            card_header_icon("Save state", ZS_OK, draw_icon_disc);
            gp_draw_slots(true);
            break;
        case GP_LOAD:
            card_header_icon("Load state", ZS_DATA, draw_icon_disc);
            gp_draw_slots(false);
            break;
        case GP_PADS: {
            card_header_icon("Controllers", ZS_DATA, draw_icon_pad);
            g_gp_pane_cols = 3;
            SioPadMode mode = inter ? sio_get_pad_mode(&inter->sio) : SIO_PAD_DIGITAL;
            const char* names[3] = { "Digital", "Analog", "Stick" };
            const char* subs[3]  = { "LED off", "LED red", "LED green" };
            SioPadMode modes[3]  = { SIO_PAD_DIGITAL, SIO_PAD_ANALOG, SIO_PAD_STICK };
            float gap = 10.0f * s;
            float bw = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
            for (int i = 0; i < 3; i++) {
                if (i) ImGui::SameLine(0, gap);
                if (gp_btn(names[i], subs[i], ImVec2(bw, 56.0f * s), inter == nullptr,
                           mode == modes[i], ZS_DATA) && inter) {
                    sio_set_pad_mode(&inter->sio, modes[i]);
                    gp_toast(sio_pad_mode_name(modes[i]), "pad mode", ZS_OK);
                }
            }
            ImGui::Dummy(ImVec2(0, 6.0f * s));
            if (gp_btn("Mapping editor", "opens in the debug workspace",
                       ImVec2(bw * 2.0f + gap, 50.0f * s), false, false, ZS_DATA)) {
                g_show_controller_mapping = true;
                g_shell = SHELL_DEBUG;
                gp_close_menu();
            }
            ImGui::Dummy(ImVec2(0, 8.0f * s));
            gp_pane_text("Digital is the boot mode, as on hardware. F12 or the DS4 touchpad click "
                         "is the Analog button. In this menu the pad navigates: D-pad to move, "
                         "Cross to confirm, Circle to go back; Select + Start opens it from a game.");
            break;
        }
        case GP_VIDEO: {
            /* This reports what the machine says and offers the controls that
             * exist. What cannot change under a running process is marked, not
             * hidden — a GL device row that says "next launch" is worth more
             * than a control that looks live and does nothing. */
            const HostInfo* host = host_info_get();
            card_header_icon("Video & HUD", ZS_DATA, draw_icon_screen);

            if (ImGui::BeginTable("gp_video_now", 2, ImGuiTableFlags_SizingStretchProp)) {
                gp_row("GPU", host->gl_renderer[0] ? host->gl_renderer : "unknown",
                       host->gpu_request[0] && !host->gpu_request_honoured ? ZS_CRIT : ZS_TEXT);
                snprintf(buf, sizeof(buf), "%ux%u  %s",
                         inter ? inter->gpu.crtc.display_width  : 0,
                         inter ? inter->gpu.crtc.display_height : 0,
                         inter && inter->gpu.display_depth == D24Bits ? "24bpp" : "15bpp");
                gp_row("Output", buf, ZS_TEXT);
                ImGui::EndTable();
            }
            ImGui::Dummy(ImVec2(0, 6.0f * s));

            micro_label("Theme");
            {
                /* One row of five: the choice is visible without scrolling. */
                float gap = 8.0f * s;
                float tw = (ImGui::GetContentRegionAvail().x - gap * (float)(ZS_THEME_COUNT - 1)) / (float)ZS_THEME_COUNT;
                for (int i = 0; i < ZS_THEME_COUNT; i++) {
                    if (i) ImGui::SameLine(0, gap);
                    if (gp_btn(g_themes[i].name, g_theme == i ? "active" : "", ImVec2(tw, 50.0f * s), false,
                               g_theme == i, g_themes[i].data)) {
                        theme_set(i, true);
                        gp_toast(g_themes[i].name, g_themes[i].blurb, g_themes[i].data);
                    }
                }
            }
            ImGui::Dummy(ImVec2(0, 6.0f * s));

            micro_label("Renderer");
            const char* vk_why = gfx_backend_unavailable_reason(GFX_BACKEND_VULKAN);
            struct { const char* label; GfxBackendType type; } backends[2] = {
                { "OpenGL 3.3", GFX_BACKEND_GL33 },
                { "Vulkan 1.3", GFX_BACKEND_VULKAN },
            };
            for (int i = 0; i < 2; i++) {
                bool live    = (s_gfx_backend == (int)backends[i].type);
                bool blocked = (backends[i].type == GFX_BACKEND_VULKAN) && vk_why;
                if (i) ImGui::SameLine(0, 10.0f * s);
                if (gp_btn(backends[i].label, live ? "running" : (blocked ? "unavailable" : "switch"),
                           ImVec2(bsz.x, 50.0f * s), live || blocked || g_req_gfx, live, ZS_OK))
                    gp_request_gfx(backends[i].type, -1, g_req_gfx_what.internal_scale);
            }
            if (vk_why) {
                ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
                ImGui::TextWrapped("Vulkan: %s", vk_why);
                ImGui::PopStyleColor();
            }

            int ndev = 0;
            const GfxDeviceInfo* devs = gfx_devices((GfxBackendType)s_gfx_backend, &ndev);
            for (int i = 0; i < ndev; i++) {
                bool live = (i == g_gfx_live_device);
                if (gp_btn(devs[i].name, live ? "running" : (devs[i].live_switchable ? "switch" : "next launch"),
                           ImVec2(0, 46.0f * s), live || !devs[i].live_switchable || g_req_gfx, live, ZS_OK))
                    gp_request_gfx((GfxBackendType)s_gfx_backend, i, g_req_gfx_what.internal_scale);
            }
            if (g_req_gfx) {
                ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
                ImGui::TextUnformatted("Switching at the end of this frame...");
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, 6.0f * s));

            micro_label("Screen");
            if (gp_btn("Fullscreen", "F11 or Alt+Enter", ImVec2(0, 46.0f * s), false, false, ZS_DATA))
                g_req_fullscreen = true;
            snprintf(buf, sizeof(buf), "Overlay: %s", g_gp_hud_names[g_gp_hud_mode]);
            if (gp_btn(buf, "F2 cycles auto-hide, always on, off", ImVec2(0, 46.0f * s), false, false, ZS_DATA))
                g_gp_hud_mode = (g_gp_hud_mode + 1) % GP_HUD_COUNT;
            if (gp_btn(g_gp_graph ? "Frame-time graph: on" : "Frame-time graph: off",
                       "one bar per field against the budget", ImVec2(0, 46.0f * s), false, g_gp_graph, ZS_DATA))
                g_gp_graph = !g_gp_graph;
            break;
        }
        case GP_MACHINE: {
            const HostInfo* host = host_info_get();
            host_info_sample();
            card_header_icon("Machine", ZS_OK, draw_icon_cpu);
            if (ImGui::BeginTable("gp_machine", 2, ImGuiTableFlags_SizingStretchProp)) {
                gp_row("BIOS", g_bios_name, ZS_TEXT);
                gp_row("Disc", g_disc_name, ZS_TEXT);
                snprintf(buf, sizeof(buf), "%ux%u  %s  %s",
                         inter ? inter->gpu.crtc.display_width : 0,
                         inter ? inter->gpu.crtc.display_height : 0,
                         inter && inter->gpu.display_depth == D24Bits ? "24bpp" : "15bpp",
                         inter && inter->gpu.display_disabled ? "(display off)" : "");
                gp_row("Output", buf, ZS_DATA);
                gp_row("Renderer", s_gfx_backend == GFX_BACKEND_VULKAN ? "Vulkan 1.3" : "OpenGL 3.3", ZS_DATA);
                gp_row("GPU context", host->gl_renderer,
                       host->gpu_request[0] && !host->gpu_request_honoured ? ZS_CRIT : ZS_DATA);
                gp_row("Driver", host->gl_version[0] ? host->gl_version : host->gl_driver, ZS_MUTED);
                if (host->audio_freq > 0)
                    snprintf(buf, sizeof(buf), "%s  %d Hz  %.1f ms",
                             host->audio_driver, host->audio_freq, host_info_audio_latency_ms());
                else
                    snprintf(buf, sizeof(buf), "%s (no device)", host->audio_driver);
                gp_row("Audio device", buf, ZS_AUDIO);
                snprintf(buf, sizeof(buf), "%.0f%% of one core, %.0f MB",
                         host->process_cpu_pct, host->rss_mb);
                gp_row("Host cost", buf, ZS_TEXT);
                ImGui::EndTable();
            }
            break;
        }
        case GP_WORKSPACE:
            card_header_icon("Debug workspace", ZS_DATA, draw_icon_cpu);
            gp_pane_text("The same window, the other shell: nine view modes, the pipeline, "
                         "the frame inspector, disassembly, VRAM and the log dock. The backquote "
                         "key switches from either side.");
            if (gp_btn("Open the workspace", "the machine resumes", ImVec2(260.0f * s, 50.0f * s),
                       false, false, ZS_DATA)) {
                g_shell = SHELL_DEBUG;
                gp_close_menu();
            }
            break;
        case GP_QUIT:
            card_header_icon("Quit", ZS_CRIT, draw_icon_pin);
            g_gp_pane_cols = 2;
            gp_pane_text("The machine stops. Memory cards are written on the way out; "
                         "save states are not taken automatically.");
            if (gp_btn("Cancel", "back to the game", ImVec2(bsz.x, 50.0f * s), false, false, ZS_DATA))
                gp_close_menu();
            ImGui::SameLine(0, 10.0f * s);
            if (gp_btn("Quit now", "ends the session", ImVec2(bsz.x, 50.0f * s), false, false, ZS_CRIT))
                g_req_quit = true;
            break;
        default: break;
    }
    g_gp_pane_count = g_gp_pane_next;
    ImGui::EndChild();

    /* --- footer: the same six inputs, named for both devices --- */
    dl->AddLine(ImVec2(w0.x, w1.y - fh), ImVec2(w1.x, w1.y - fh), gp_col(ZS_LINE_SOFT, 1.0f));
    {
        ImVec2 p = ImVec2(w0.x + 18.0f * s, w1.y - fh + (fh - (mono->LegacySize * 0.90f + 6.0f * s)) * 0.5f);
        p.x = gp_keycap(dl, p, "Arrows / D-pad", "Move", 1.0f);
        p.x = gp_keycap(dl, p, "Enter / Cross", "Select", 1.0f);
        p.x = gp_keycap(dl, p, "Esc / Circle", "Back", 1.0f);
        p.x = gp_keycap(dl, p, "Start", "Resume", 1.0f);
    }
    ImGui::End();

    /* --- navigation, now that the pane has counted its controls --- */
    if (!g_gp_menu_open) return;   /* a control above closed the menu */
    if (g_gp_focus == 1 && g_gp_pane_count == 0) g_gp_focus = 0;
    if (g_gp_focus == 0) {
        if (g_nav.down) { g_gp_menu_idx = (g_gp_menu_idx + 1) % GP_ITEM_COUNT; g_gp_arm_kind = 0; }
        if (g_nav.up)   { g_gp_menu_idx = (g_gp_menu_idx + GP_ITEM_COUNT - 1) % GP_ITEM_COUNT; g_gp_arm_kind = 0; }
        if (g_nav.back) { gp_close_menu(); return; }
        if (!g_nav.up && !g_nav.down) {
            if (list_ok && g_gp_menu_idx == GP_RESUME) { gp_close_menu(); return; }
            if (list_ok && g_gp_menu_idx == GP_WORKSPACE) { g_shell = SHELL_DEBUG; gp_close_menu(); return; }
            if ((list_ok || g_nav.right) && g_gp_pane_count > 0) {
                g_gp_focus = 1;
                g_gp_pane_idx = 0;   /* Quit lands on Cancel, not on Quit now */
            }
        }
    } else {
        int cols = g_gp_pane_cols > 0 ? g_gp_pane_cols : 1;
        int i = g_gp_pane_idx;
        if (i >= g_gp_pane_count) i = g_gp_pane_count - 1;
        if (g_nav.back) { g_gp_focus = 0; g_gp_arm_kind = 0; }
        else if (g_nav.left)  { if (i % cols == 0) { g_gp_focus = 0; g_gp_arm_kind = 0; } else i--; }
        else if (g_nav.right) { if (i % cols != cols - 1 && i + 1 < g_gp_pane_count) i++; }
        else if (g_nav.up)    { if (i - cols >= 0) i -= cols; }
        else if (g_nav.down)  { i = (i + cols < g_gp_pane_count) ? i + cols : g_gp_pane_count - 1; }
        g_gp_pane_idx = i;
    }
}

// --- the shell ----------------------------------------------------------------

/* The menu stops the machine and gives it back on the way out. Run every
 * frame in both shells, because leaving for the workspace closes the menu
 * without going through the gameplay shell again. */
static void gp_sync_pause(Interconnect* inter) {
    if (!inter) return;
    Debugger* dbg = &inter->debugger;
    bool want = g_gp_menu_open && g_shell == SHELL_GAMEPLAY;
    if (want && !dbg->paused) { dbg->paused = true; g_gp_menu_paused = true; }
    else if (!want && g_gp_menu_paused) { dbg->paused = false; g_gp_menu_paused = false; }
}

/* Keys and pad for the gameplay shell, read once per frame before anything is
 * drawn so the menu sees this frame's input. Escape arrives through
 * debug_ui_escape_pressed() and F5/F8 through debug_ui_quick_state(), because
 * the host loop sees the event first. */
static void gp_handle_keys(Interconnect* inter) {
    ImGuiIO& io = ImGui::GetIO();
    memset(&g_nav, 0, sizeof(g_nav));
    uint16_t pressed = (uint16_t)(g_pad_prev & (uint16_t)~g_pad_word);
    uint16_t down    = (uint16_t)~g_pad_word;
    g_pad_prev = g_pad_word;
    if (io.WantTextInput) return;

    if ((ImGui::IsKeyPressed(ImGuiKey_F1, false) && !io.KeyShift) ||
        ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false)) {
        g_shell = SHELL_DEBUG;
        if (g_gp_menu_open) gp_close_menu();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) { g_req_fullscreen = true; gp_note_input(); }
    if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        g_gp_hud_mode = (g_gp_hud_mode + 1) % GP_HUD_COUNT;
        gp_toast(g_gp_hud_names[g_gp_hud_mode], "overlay", ZS_DATA);
        gp_note_input();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6, false) || ImGui::IsKeyPressed(ImGuiKey_F7, false)) {
        int step = ImGui::IsKeyPressed(ImGuiKey_F7, false) ? 1 : 3;
        g_gp_slot = (g_gp_slot + step) % 4;
        char info[80], name[24];
        snprintf(name, sizeof(name), "Slot %d", g_gp_slot);
        gp_toast(name, gp_slot_info(g_gp_slot, info, sizeof(info)) ? info : "empty", ZS_DATA);
        gp_note_input();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false) && inter) {
        /* The host also cycles the pad on F12; this only reports what it did. */
        gp_toast(sio_pad_mode_name(sio_get_pad_mode(&inter->sio)), "pad mode", ZS_OK);
        gp_note_input();
    }

    if (!g_gp_menu_open) {
        if (inter && (ImGui::IsKeyPressed(ImGuiKey_F10, false) || ImGui::IsKeyPressed(ImGuiKey_Pause, false))) {
            inter->debugger.paused = !inter->debugger.paused;
            gp_note_input();
        }
        /* Select + Start: both held, one of them just pressed. */
        if ((down & 0x0009) == 0x0009 && (pressed & 0x0009)) gp_open_menu();
        return;
    }

    const uint16_t PAD_START = 1u << 3, PAD_UP = 1u << 4, PAD_RIGHT = 1u << 5, PAD_DOWN = 1u << 6,
                   PAD_LEFT = 1u << 7, PAD_CIRCLE = 1u << 13, PAD_CROSS = 1u << 14;
    g_nav.up    = ImGui::IsKeyPressed(ImGuiKey_UpArrow)    || (pressed & PAD_UP);
    g_nav.down  = ImGui::IsKeyPressed(ImGuiKey_DownArrow)  || (pressed & PAD_DOWN);
    g_nav.left  = ImGui::IsKeyPressed(ImGuiKey_LeftArrow)  || (pressed & PAD_LEFT);
    g_nav.right = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || (pressed & PAD_RIGHT);
    g_nav.ok    = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                  (pressed & PAD_CROSS);
    g_nav.back  = ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || (pressed & PAD_CIRCLE);
    if (pressed & PAD_START) { gp_close_menu(); memset(&g_nav, 0, sizeof(g_nav)); }
    gp_note_input();
}

static void draw_gameplay_shell(Cpu* cpu, Interconnect* inter) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin("##GameplayShell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);

    GfxTexHandle tex_id = inter ? renderer_get_display_texture(&inter->gpu.renderer) : 0;
    draw_scanout(tex_id, inter, ImGui::GetContentRegionAvail());

    /* The veil goes on this window's own draw list. Not the foreground one,
     * which is painted over every window and would put the menu itself in
     * shadow; not the background one, which this window's opaque black covers.
     * Here it lands above the picture and below the menu window. */
    if (g_gp_menu_open) {
        ImVec2 a = ImGui::GetWindowPos();
        ImVec2 b = ImVec2(a.x + ImGui::GetWindowWidth(), a.y + ImGui::GetWindowHeight());
        ImGui::GetWindowDrawList()->AddRectFilled(a, b, IM_COL32(4, 6, 10, 185));
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    /* The overlay comes back for the pointer and for the shell's own keys, and
     * fades after a few idle seconds. Game keys do not wake it: someone playing
     * on the keyboard would otherwise never see it go. */
    ImVec2 mouse = ImGui::GetIO().MousePos;
    if (fabsf(mouse.x - g_gp_last_mouse.x) > 1.0f || fabsf(mouse.y - g_gp_last_mouse.y) > 1.0f) {
        g_gp_last_mouse = mouse;
        gp_note_input();
    }
    double idle = ImGui::GetTime() - g_gp_last_input_t;
    float wake = (idle < 3.2) ? 1.0f
               : (idle < 3.9) ? (float)(1.0 - (idle - 3.2) / 0.7)
               : 0.0f;
    float alpha = g_gp_menu_open ? 0.0f
                : g_gp_hud_mode == GP_HUD_ALWAYS ? 1.0f
                : g_gp_hud_mode == GP_HUD_OFF    ? 0.0f
                : wake;

    /* An idle pointer over the picture goes away with the overlay. */
    if (!g_gp_menu_open && wake <= 0.0f) ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    gp_draw_hud(inter, alpha);
    if (inter && inter->debugger.paused && !g_gp_menu_open) gp_draw_paused();
    gp_draw_toasts();

    if (g_gp_menu_open) gp_draw_menu(cpu, inter);
}

/* --- UI tour: a developer tool, not a feature ---------------------------------
 *
 * ZS1_UI_TOUR=<dir> walks the interface by itself - the gameplay HUD, every
 * page of the quick menu, every view mode of the workspace, the controller
 * window - and saves a PPM of what the window showed for each. It exists
 * because a host-side screen grab is not available on every session (a Wayland
 * compositor refuses to hand out the root window), and a layout bug is found
 * by looking at the picture, not by reading the code. The GPU thread does the
 * read-back, after ImGui has drawn and before the swap, so the picture is the
 * composed window and not a re-render. OpenGL only. */
static std::atomic<bool> g_shot_pending(false);
static char              g_shot_path[320];
static std::atomic<int>  g_shot_done(0);

extern "C" int debug_ui_shot_take(char* out, size_t n) {
    if (!g_shot_pending.load(std::memory_order_acquire)) return 0;
    snprintf(out, n, "%s", g_shot_path);
    g_shot_pending.store(false, std::memory_order_release);
    g_shot_done.fetch_add(1);
    return 1;
}

static void ui_tour_tick(void) {
    static const char* dir = getenv("ZS1_UI_TOUR");
    if (!dir || !dir[0]) return;
    static int  frame = 0, step = 0, settle = 0, shots_seen = 0;
    static bool waiting = false;
    static int  warm = -1;
    if (warm < 0) { const char* w = getenv("ZS1_UI_TOUR_WARM"); warm = w ? atoi(w) : 900; }
    frame++;
    if (frame < warm) return;

    /* Step table: 1 HUD + 8 menu pages + 9 view modes + controller window. */
    const int total = 1 + GP_ITEM_COUNT + MODE_COUNT + 1;
    if (waiting) {
        if (g_shot_done.load() > shots_seen) { shots_seen = g_shot_done.load(); waiting = false; step++; settle = 0; }
        return;
    }
    if (step >= total) { g_req_quit = true; return; }

    char name[64];
    if (step == 0) {
        g_shell = SHELL_GAMEPLAY; g_gp_menu_open = false; g_gp_hud_mode = GP_HUD_ALWAYS;
        gp_note_input();
        snprintf(name, sizeof(name), "gp_hud");
    } else if (step <= GP_ITEM_COUNT) {
        int i = step - 1;
        g_shell = SHELL_GAMEPLAY;
        if (!g_gp_menu_open) gp_open_menu();
        g_gp_menu_idx = i;
        snprintf(name, sizeof(name), "gp_menu_%d_%s", i, g_gp_items[i].label);
    } else if (step <= GP_ITEM_COUNT + MODE_COUNT) {
        int m = step - 1 - GP_ITEM_COUNT;
        if (g_gp_menu_open) gp_close_menu();
        g_shell = SHELL_DEBUG;
        if (g_mode != m) { g_mode = m; g_layout_dirty = true; }
        g_show_controller_mapping = false;
        snprintf(name, sizeof(name), "dbg_%d_%s", m, g_modes[m].name);
    } else {
        g_shell = SHELL_DEBUG; g_mode = MODE_PIPELINE;
        g_show_controller_mapping = true;
        snprintf(name, sizeof(name), "dbg_controllers");
    }
    for (char* c = name; *c; c++) if (*c == ' ' || *c == '&') *c = '_';

    /* Let the new state draw for a while - the dock layout needs a few frames
     * to rebuild, and the HUD and menu fade in. */
    if (++settle < 25) return;
    snprintf(g_shot_path, sizeof(g_shot_path), "%s/%s.ppm", dir, name);
    g_shot_pending.store(true, std::memory_order_release);
    waiting = true;
}

extern "C" void debug_ui_render(void* cpu_ptr, void* interconnect_ptr) {
    Cpu* cpu = (Cpu*)cpu_ptr;
    Interconnect* inter = (Interconnect*)interconnect_ptr;

    /* ImGui_ImplOpenGL3_NewFrame() moved to GPU thread — owns GL context */
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    g_vram_viewer_drawn = false;
    ui_tour_tick();

    /* Two shells, one window. The gameplay shell draws the screen and its
     * overlay and nothing else — no dockspace, no rail, no log dock, so none of
     * the panels cost anything while it is up. */
    if (g_shell == SHELL_GAMEPLAY) {
        gp_handle_keys(inter);
        gp_sync_pause(inter);
        if (g_shell == SHELL_GAMEPLAY) draw_gameplay_shell(cpu, inter);
        vram_view_publish_hidden(inter);
        ImGui::Render();
        return;
    }

    gp_sync_pause(inter);
    g_pad_prev = g_pad_word;   /* no stale edge when the gameplay shell returns */

    // Mode drives which windows are live this frame (the rail replaced the
    // scattered floating panels). The screen is up in every mode.
    g_show_display     = true;
    g_show_disasm      = (g_mode == MODE_CODE);
    g_show_registers   = (g_mode == MODE_CODE || g_mode == MODE_MEMORY);
    g_show_breakpoints = (g_mode == MODE_CODE);
    g_show_spu         = (g_mode == MODE_AUDIO);
    g_show_vram_viewer = (g_mode == MODE_VRAM);
    g_show_lua_console = (g_mode == MODE_SCRIPT);

    // --- Host window: machine bar on top, the mode dockspace below it ---
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags dock_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##DockHost", nullptr, dock_flags);
    ImGui::PopStyleVar(3);

    // Render PlayStation Boot Screen Inspired Ambient Background Gradient (Sony Amber Diamond & PS Red/Yellow/Teal/Blue)
    ImDrawList* bg_dl = ImGui::GetWindowDrawList();
    ImVec2 vp_p0 = viewport->WorkPos;
    ImVec2 vp_p1 = ImVec2(vp_p0.x + viewport->WorkSize.x, vp_p0.y + viewport->WorkSize.y);

    // 4-corner multi-color gradient inspired by Sony Computer Entertainment boot screen & PS logo
    bg_dl->AddRectFilledMultiColor(vp_p0, vp_p1,
        IM_COL32(38, 22, 10, 255),   // Top-left (Sony Diamond Gold Amber Glow)
        IM_COL32(42, 12, 16, 255),   // Top-right (PlayStation Logo Red Glow)
        IM_COL32(8, 18, 32, 255),    // Bottom-right (PlayStation Royal Blue Glow)
        IM_COL32(6, 22, 26, 255));   // Bottom-left (PlayStation Teal/Green Glow)

    // Smooth obsidian dark theme overlay for high contrast
    bg_dl->AddRectFilledMultiColor(vp_p0, vp_p1,
        IM_COL32(10, 13, 20, 215), IM_COL32(6, 8, 14, 245),
        IM_COL32(6, 8, 14, 245), IM_COL32(10, 13, 20, 215));

    draw_machine_bar(cpu, inter);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    rebuild_layout(dockspace_id);

    // Mode rail (left), constant across modes
    draw_mode_rail();

    // The emulated screen — pinned to the top of the stage in every mode
    GfxTexHandle tex_id = 0;
    if (inter) tex_id = renderer_get_display_texture(&inter->gpu.renderer);
    draw_ps1_display(tex_id, inter);

    // Mode-specific stage body + inspector
    switch (g_mode) {
        case MODE_PIPELINE:
            if (ImGui::Begin("Pipeline", nullptr)) draw_pipeline_view(inter);
            ImGui::End();
            draw_inspector_window(inter);
            break;
        case MODE_DISPLAY:
            break;  // screen fills the stage
        case MODE_FRAME:
            if (ImGui::Begin("Frame", nullptr)) draw_frame_view(inter);
            else                                 frame_events_set_enabled(false);
            ImGui::End();
            break;
        case MODE_CODE:
            draw_disasm_window(cpu, inter);
            draw_registers_window(cpu);
            if (inter) draw_breakpoints_window(inter);
            break;
        case MODE_MEMORY:
            if (ImGui::Begin("Memory", nullptr)) draw_memory_view(inter);
            ImGui::End();
            draw_registers_window(cpu);
            break;
        case MODE_AUDIO:
            if (inter) draw_spu_debug_window(&inter->spu);
            if (ImGui::Begin("Audio Meters", nullptr)) draw_audio_meters(inter);
            ImGui::End();
            break;
        case MODE_VRAM:
            if (inter) draw_vram_viewer_window(&inter->gpu.renderer, inter);
            break;
        case MODE_HOST:
            draw_host_hw_window(inter);
            draw_inspector_window(inter);
            break;
        case MODE_SCRIPT:
            draw_lua_console_window();
            draw_inspector_window(inter);
            break;
    }

    // Logs tabbed along the bottom, every mode
    for (auto& comp : g_log_components)
        draw_component_log_window(comp);

    // Controller remapping & input tester window
    draw_controller_mapping_window();

    ImGui::End(); // DockHost

    // Keyboard — F1..F8 pick the mode; F10 (or Pause) toggles pause; F11 steps.
    // Pause is NOT on Space: Space is the emulated pad's START button, and the
    // controller reads the raw SDL keyboard state, so binding both made every
    // press of START also halt the machine.
    if (!ImGui::GetIO().WantTextInput) {
        ImGuiKey mode_keys[MODE_COUNT] = {
            ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4,
            ImGuiKey_F5, ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8, ImGuiKey_F9
        };
        /* Back to the gameplay shell: the backquote toggles from either side,
         * and Shift+F1 does the same for a keyboard that puts backquote
         * somewhere awkward. Plain F1 stays the Pipeline mode key it has always
         * been, so the rail's F1..F9 are untouched. */
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent) ||
            (ImGui::IsKeyPressed(ImGuiKey_F1) && ImGui::GetIO().KeyShift)) {
            g_shell = SHELL_GAMEPLAY;
            gp_note_input();
        } else {
            /* Shift+F5 / Shift+F8 are the savestate keys here (the host loop
             * takes them), so a shifted function key is not a mode change. */
            for (int i = 0; i < MODE_COUNT && !ImGui::GetIO().KeyShift; i++)
                if (ImGui::IsKeyPressed(mode_keys[i]) && g_mode != i) {
                    g_mode = i; g_layout_dirty = true;
                }
        }
        if (inter) {
            Debugger* dbg = &inter->debugger;
            if (ImGui::IsKeyPressed(ImGuiKey_F10) || ImGui::IsKeyPressed(ImGuiKey_Pause)) {
                if (dbg->paused) { dbg->paused = false; g_disasm_follow_pc = true; }
                else dbg->paused = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F11) && dbg->paused)
                g_step_pending = true;
        }
    }

    /* Escape asks before it ends the session. It used to quit outright, which
     * is also what cancelling a text field or closing a popup did. */
    if (g_ws_quit_confirm) {
        ImGuiViewport* qvp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(qvp->WorkPos.x + qvp->WorkSize.x * 0.5f,
                                       qvp->WorkPos.y + qvp->WorkSize.y * 0.5f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * g_ui_scale, 18.0f * g_ui_scale));
        ImGui::Begin("##ws_quit", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        if (g_font_h1) ImGui::PushFont(g_font_h1);
        ImGui::TextUnformatted("Quit ZoniStation One?");
        if (g_font_h1) ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
        ImGui::TextUnformatted("Memory cards are written on the way out; no save state is taken.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8.0f * g_ui_scale));
        ImVec2 qb = ImVec2(150.0f * g_ui_scale, 34.0f * g_ui_scale);
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_CRIT);
        if (ImGui::Button("Quit  (Enter)", qb) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            g_req_quit = true;
            g_ws_quit_confirm = false;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel  (Esc)", qb)) g_ws_quit_confirm = false;
        ImGui::SameLine();
        if (ImGui::Button("Back to the game", qb)) { g_ws_quit_confirm = false; g_shell = SHELL_GAMEPLAY; }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    /* Savestate and renderer results are reported here too, not only in the
     * gameplay shell. */
    gp_draw_toasts();

    vram_view_publish_hidden(inter);
    ImGui::Render();
    /* ImGui_ImplOpenGL3_RenderDrawData moved to GPU thread via imgui_render_draw_data() */
}

extern "C" void* debug_ui_get_draw_data(void) {
    return ImGui::GetDrawData();
}

/* Called from GPU thread to render ImGui onto the screen */
extern "C" void imgui_render_draw_data(void* draw_data) {
    if (draw_data)
        ImGui_ImplOpenGL3_RenderDrawData(static_cast<ImDrawData*>(draw_data));
}

/* Called from GPU thread at start of each frame (needs GL context) */
extern "C" void imgui_opengl_new_frame(void) {
    ImGui_ImplOpenGL3_NewFrame();
}

extern "C" void debug_ui_shutdown(void) {
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        for (auto& comp : g_log_components) {
            if (comp.file) {
                fflush(comp.file);
                fclose(comp.file);
                comp.file = nullptr;
            }
        }
    }
    debug_ui_backend_shutdown();
    ImGui::DestroyContext();
}

/* ---- host-loop handshake --------------------------------------------------
 * The shell asks; main.c owns the machine and does. Each of these clears the
 * request as it hands it over. */

extern "C" bool debug_ui_escape_pressed(void) {
    if (g_shell == SHELL_GAMEPLAY) {
        /* One step back each press: pane -> list -> closed. */
        if (!g_gp_menu_open)       gp_open_menu();
        else if (g_gp_focus == 1) { g_gp_focus = 0; g_gp_arm_kind = 0; }
        else                       gp_close_menu();
        g_gp_last_input_t = ImGui::GetTime();
        return true;
    }
    /* The workspace: Escape belongs to whatever ImGui has open first — a text
     * field being edited, a popup — and only then asks about quitting. */
    if (g_ws_quit_confirm) { g_ws_quit_confirm = false; return true; }
    if (ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return true;
    g_ws_quit_confirm = true;
    return true;
}

/* True while the keyboard is the interface's and not the machine's: a text
 * field has it, or a menu or a confirmation is up. */
extern "C" bool debug_ui_keyboard_captured(void) {
    return ImGui::GetIO().WantTextInput || g_ws_quit_confirm ||
           (g_shell == SHELL_GAMEPLAY && g_gp_menu_open);
}

/* The pad word on its way to the SIO. The interface keeps a copy for its own
 * navigation and hands the game "nothing pressed" while it holds the input,
 * and afterwards until every button is released. */
extern "C" uint16_t debug_ui_filter_pad(uint16_t buttons) {
    g_pad_word = buttons;
    if (debug_ui_keyboard_captured()) { g_pad_hold = true; return 0xFFFF; }
    if (g_pad_hold) {
        if (buttons == 0xFFFF) g_pad_hold = false;
        return 0xFFFF;
    }
    return buttons;
}

/* F5 / F8 from the host loop. In the gameplay shell they act on the current
 * slot; in the workspace plain F5 and F8 are the Memory and Host HW modes, so
 * the savestate keys there are Shift+F5 and Shift+F8. Returns false when the
 * key was not a savestate request. */
extern "C" bool debug_ui_quick_state(bool save, bool shift) {
    if (ImGui::GetIO().WantTextInput || g_ws_quit_confirm) return false;
    if (g_shell == SHELL_DEBUG && !shift) return false;
    gp_request_state(save, g_gp_slot);
    gp_note_input();
    return true;
}

extern "C" bool debug_ui_take_fullscreen_request(void) {
    bool f = g_req_fullscreen;
    g_req_fullscreen = false;
    return f;
}

extern "C" bool debug_ui_take_quit_request(void) {
    bool q = g_req_quit;
    g_req_quit = false;
    return q;
}

extern "C" bool debug_ui_take_state_request(bool* out_save, char* path, size_t path_size) {
    if (!g_req_state) return false;
    g_req_state = false;
    if (out_save) *out_save = g_req_state_save;
    if (path && path_size) snprintf(path, path_size, "%s", g_req_state_path);
    return true;
}

extern "C" bool debug_ui_take_gfx_request(GfxDeviceRequest* out) {
    if (!g_req_gfx) return false;
    g_req_gfx = false;
    if (out) *out = g_req_gfx_what;
    return true;
}

/* What the host actually ended up running, after the switch — including the
 * case where it failed and the old backend was put back. The panel marks the
 * live entry from this rather than from what was asked for, so a fallback shows
 * as a fallback instead of as a change that never happened. */
extern "C" void debug_ui_notify_gfx_result(int backend, int device_index, bool ok,
                                           const char* detail) {
    s_gfx_backend     = backend;
    g_gfx_live_device = device_index;
    gp_toast(ok ? "Renderer switched" : "Switch failed — kept the old renderer",
             detail ? detail : (backend == GFX_BACKEND_VULKAN ? "Vulkan 1.3" : "OpenGL 3.3"),
             ok ? ZS_OK : ZS_CRIT);
}

extern "C" void debug_ui_notify_state_result(bool save, bool ok, const char* path) {
    const char* base = path ? strrchr(path, '/') : nullptr;
    gp_toast(ok ? (save ? "State saved" : "State loaded")
                : (save ? "Save failed" : "Load refused"),
             base ? base + 1 : (path ? path : ""),
             ok ? ZS_OK : ZS_CRIT);
}
