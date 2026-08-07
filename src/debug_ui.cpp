/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 ZioZoni95
 *
 * Part of ZoniStation One, a PlayStation 1 emulator.
 * See LICENSE for the full licence text and THIRD-PARTY.md for the
 * components of this project that have other authors.
 */
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "debug_ui.h"
#include "log.h"
#include <vector>
#include <string>
#include <mutex>
#include <map>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "cpu.h"
#include "interconnect.h"
#include "renderer.h"
#include "debugger.h"
#include "spu.h"
#include "lua_debug.h"
#include "frame_events.h"
#include "controller.h"
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

static const ImVec4 ZS_GROUND    = COL(0x0A, 0x0D, 0x14);   // Deep Obsidian
static const ImVec4 ZS_PANEL     = COL(0x13, 0x19, 0x26);   // Sleek Translucent Slate
static const ImVec4 ZS_PANEL2    = COL(0x1A, 0x23, 0x34);   // Cyber Slate
static const ImVec4 ZS_LINE      = COL(0x28, 0x36, 0x4F);   // High-contrast neon slate border
static const ImVec4 ZS_LINE_SOFT = COL(0x1E, 0x28, 0x3C);
static const ImVec4 ZS_TEXT      = COL(0xF1, 0xF5, 0xF9);   // Ultra-crisp White
static const ImVec4 ZS_MUTED     = COL(0x94, 0xA3, 0xB8);   // Cool slate gray
static const ImVec4 ZS_FAINT     = COL(0x64, 0x74, 0x8B);
static const ImVec4 ZS_DATA      = COL(0x00, 0xF0, 0xFF);   // Neon Cyan
static const ImVec4 ZS_AUDIO     = COL(0xFF, 0x2E, 0x7E);   // Neon Rose
static const ImVec4 ZS_OK        = COL(0x10, 0xB9, 0x81);   // Emerald Green
static const ImVec4 ZS_WARN      = COL(0xF5, 0x9E, 0x0B);   // Amber Gold
static const ImVec4 ZS_CRIT      = COL(0xEF, 0x44, 0x44);   // Vibrant Red
static const ImVec4 ZS_RAIL_BG   = COL(0x0F, 0x14, 0x20);
static const ImVec4 ZS_DOCK_BG   = COL(0x0D, 0x11, 0x1C);
static const ImVec4 ZS_MODE_ON   = COL(0x1E, 0x2A, 0x3E);

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

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ZS_TEXT;
    c[ImGuiCol_TextDisabled]         = ZS_FAINT;
    c[ImGuiCol_WindowBg]             = ZS_PANEL;
    c[ImGuiCol_ChildBg]             = COLA(0x11, 0x17, 0x24, 0.95f);
    c[ImGuiCol_PopupBg]              = ZS_PANEL2;
    c[ImGuiCol_Border]               = ZS_LINE;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = COLA(0x10, 0x16, 0x22, 1.0f);
    c[ImGuiCol_FrameBgHovered]       = COLA(0x1F, 0x2B, 0x40, 1.0f);
    c[ImGuiCol_FrameBgActive]        = ZS_LINE;
    c[ImGuiCol_TitleBg]              = ZS_GROUND;
    c[ImGuiCol_TitleBgActive]        = COLA(0x1C, 0x27, 0x3C, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]     = ZS_GROUND;
    c[ImGuiCol_MenuBarBg]            = COLA(0x14, 0x1B, 0x28, 1.0f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ZS_LINE;
    c[ImGuiCol_ScrollbarGrabHovered] = ZS_MUTED;
    c[ImGuiCol_ScrollbarGrabActive]  = ZS_DATA;
    c[ImGuiCol_CheckMark]            = ZS_DATA;
    c[ImGuiCol_SliderGrab]           = ZS_DATA;
    c[ImGuiCol_SliderGrabActive]     = ZS_DATA;
    c[ImGuiCol_Button]               = COLA(0x1F, 0x2A, 0x3E, 1.0f);
    c[ImGuiCol_ButtonHovered]        = COLA(0x2B, 0x3B, 0x58, 1.0f);
    c[ImGuiCol_ButtonActive]         = ZS_LINE;
    c[ImGuiCol_Header]               = COLA(0x20, 0x2D, 0x42, 1.0f);
    c[ImGuiCol_HeaderHovered]        = COLA(0x2C, 0x3D, 0x5B, 1.0f);
    c[ImGuiCol_HeaderActive]         = ZS_LINE;
    c[ImGuiCol_Separator]            = ZS_LINE;
    c[ImGuiCol_SeparatorHovered]     = ZS_DATA;
    c[ImGuiCol_SeparatorActive]      = ZS_DATA;
    c[ImGuiCol_ResizeGrip]           = ZS_LINE;
    c[ImGuiCol_ResizeGripHovered]    = ZS_MUTED;
    c[ImGuiCol_ResizeGripActive]     = ZS_DATA;
    c[ImGuiCol_Tab]                  = COLA(0x11, 0x17, 0x24, 1.0f);
    c[ImGuiCol_TabHovered]           = COLA(0x25, 0x35, 0x50, 1.0f);
    c[ImGuiCol_TabActive]            = COLA(0x20, 0x2D, 0x44, 1.0f);
    c[ImGuiCol_TabUnfocused]         = COLA(0x0E, 0x13, 0x1E, 1.0f);
    c[ImGuiCol_TabUnfocusedActive]   = COLA(0x17, 0x22, 0x34, 1.0f);
    c[ImGuiCol_DockingPreview]       = COLA(0x00, 0xF0, 0xFF, 0.35f);
    c[ImGuiCol_DockingEmptyBg]       = ZS_GROUND;
    c[ImGuiCol_TableHeaderBg]        = COLA(0x16, 0x1F, 0x2E, 1.0f);
    c[ImGuiCol_TableBorderStrong]    = ZS_LINE;
    c[ImGuiCol_TableBorderLight]     = ZS_LINE_SOFT;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = COLA(0xFF, 0xFF, 0xFF, 0.02f);
    c[ImGuiCol_NavHighlight]         = ZS_DATA;
}

// A tight uppercase letter-spaced micro-label — identity carried by density,
// the way the mockup does it (no shipped webfont).
static void micro_label(const char* text) {
    char up[64];
    int n = 0;
    for (const char* p = text; *p && n < 62; ++p) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        up[n++] = ch;
        if (p[1]) up[n++] = ' ';   // crude letter-spacing
    }
    up[n] = '\0';
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextUnformatted(up);
    ImGui::PopStyleColor();
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

// ---------------------------------------------------------------------------
// Machine bar identity + live vitals (set from the main loop each frame)
// ---------------------------------------------------------------------------

static char   g_bios_name[64] = "n/a";
static char   g_disc_name[96] = "n/a";
static double g_vit_frame_ms  = 0.0;
static double g_vit_budget_ms = 0.0;
static int    g_vit_aq        = 0;
static int    g_vit_aq_target = 2048;
static double g_vit_drift     = 0.0;

extern "C" void debug_ui_set_machine_info(const char* bios_name, const char* disc_name) {
    snprintf(g_bios_name, sizeof(g_bios_name), "%s", bios_name ? bios_name : "n/a");
    snprintf(g_disc_name, sizeof(g_disc_name), "%s", disc_name ? disc_name : "n/a");
}

extern "C" void debug_ui_set_vitals(double frame_ms, double budget_ms,
                                    int audio_queue, int audio_target, double drift_pct) {
    g_vit_frame_ms  = frame_ms;
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
    { "Script",   "F9", false, "CMD", ZS_TEXT      },
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

struct LogComponent {
    const char* name;
    LogCategory category;
    bool is_open;
    bool auto_scroll;
    bool monospace;
    int  display_level;          /* min level shown in ImGui (default INFO) */
    std::vector<LogEntry> buffer;
    ImGuiTextFilter filter;
    FILE* file;                  /* always-open file in logs/<Name>.log */
    uint32_t writes_since_flush;
};

static std::map<LogCategory, LogComponent> g_log_components = {
    {LOG_CAT_SYSTEM,       {"System",       LOG_CAT_SYSTEM,       true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_CPU,          {"CPU",          LOG_CAT_CPU,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_IRQ,          {"IRQ",          LOG_CAT_IRQ,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_DMA,          {"DMA",          LOG_CAT_DMA,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_GPU,          {"GPU",          LOG_CAT_GPU,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_CDROM,        {"CDROM",        LOG_CAT_CDROM,        true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_TIMER,        {"Timer",        LOG_CAT_TIMER,        true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_BIOS,         {"BIOS",         LOG_CAT_BIOS,         true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_INTERCONNECT, {"Interconnect", LOG_CAT_INTERCONNECT, true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_RENDERER,     {"Renderer",     LOG_CAT_RENDERER,     true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_EVENT,        {"Event",        LOG_CAT_EVENT,        true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_GTE,          {"GTE",          LOG_CAT_GTE,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_VRAM,         {"VRAM",         LOG_CAT_VRAM,         true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_RAM,          {"RAM",          LOG_CAT_RAM,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_DEBUG,        {"Debug",        LOG_CAT_DEBUG,        true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_MDEC,         {"MDEC",         LOG_CAT_MDEC,         true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}},
    {LOG_CAT_SPU,          {"SPU",          LOG_CAT_SPU,          true, true, true, LOG_LEVEL_DEBUG, {}, {}, nullptr, 0}}
};

static std::mutex g_log_mutex;

static const char* level_name(int level);  /* fwd */

static void log_sink_callback(int category, int level, const char* msg, void* udata) {
    (void)udata;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto it = g_log_components.find((LogCategory)category);
    if (it == g_log_components.end()) return;

    LogComponent& comp = it->second;
    comp.buffer.push_back({level, msg});
    if (comp.buffer.size() > 5000)
        comp.buffer.erase(comp.buffer.begin());

    if (comp.file) {
        fprintf(comp.file, "[%s] %s\n", level_name(level), msg);
        if (++comp.writes_since_flush >= 64) {
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

static void export_component_log(const LogComponent& comp) {
    mkdir("logs", 0755);
    char path[128];
    snprintf(path, sizeof(path), "logs/%s.log", comp.name);
    FILE* f = fopen(path, "w");
    if (!f) return;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    for (const auto& e : comp.buffer)
        fprintf(f, "[%s] %s\n", level_name(e.level), e.message.c_str());
    fclose(f);
}

static void export_all_logs() {
    for (const auto& pair : g_log_components)
        export_component_log(pair.second);
}

// Log window
// ---------------------------------------------------------------------------

static void draw_component_log_window(LogComponent& comp) {
    if (!comp.is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(comp.name, &comp.is_open)) { ImGui::End(); return; }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        comp.buffer.clear();
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

    if (comp.monospace) ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    if (copy) ImGui::LogToClipboard();

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::vector<int> indices;
        for (int i = 0; i < (int)comp.buffer.size(); i++) {
            if (comp.buffer[i].level > comp.display_level) continue;
            if (!comp.filter.PassFilter(comp.buffer[i].message.c_str())) continue;
            indices.push_back(i);
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)indices.size());
        while (clipper.Step()) {
            for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++) {
                const auto& e = comp.buffer[indices[n]];
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
    if (comp.monospace) ImGui::PopFont();
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
static VramViewParams g_vram_view = { VRAM_VIEW_16BPP, 0, false, false, 0, 0 };
static float  g_vram_zoom     = 1.0f;
static ImVec2 g_vram_pan      = ImVec2(0, 0);   // in VRAM pixels, top-left of view
static bool   g_vram_grid     = false;          // 16x16 pixel grid
static bool   g_vram_tpage    = true;           // 64x256 texture-page grid
static bool   g_vram_magnify  = false;
static float  g_vram_mag_amt  = 6.0f;
static float  g_vram_mag_rad  = 120.0f;
static bool   g_vram_show_disp = true;          // outline the active display area

bool debug_ui_vram_viewer_open(void) { return g_show_vram_viewer; }

static void draw_vram_viewer_window(Renderer* renderer, Interconnect* inter) {
    if (!g_show_vram_viewer || !renderer) return;

    renderer_set_vram_view_params(renderer, &g_vram_view);
    GLuint tex = renderer_get_vram_viewer_texture(renderer);

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

static void draw_ps1_display(GLuint texture_id, Interconnect* inter) {
    if (!g_show_display) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("PS1 Display", &g_show_display,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
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

static void draw_machine_bar(Cpu* cpu, Interconnect* inter) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLA(0x15, 0x1B, 0x27, 1.0f));
    ImGui::BeginChild("##MachineBar", ImVec2(0, 52), false, ImGuiWindowFlags_NoScrollbar);

    // Multi-color linear gradient background across top header
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + 52.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(p0, p1,
        IM_COL32(30, 42, 62, 255), IM_COL32(16, 22, 34, 255),
        IM_COL32(16, 22, 34, 255), IM_COL32(30, 42, 62, 255));
    dl->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f), IM_COL32(42, 58, 84, 255), 1.0f);

    // Brand + video-mode subtitle
    bool pal = inter && inter->gpu.vmode == Pal;
    double hz = 0.0;
    if (inter) {
        uint32_t cpf = gpu_cycles_per_frame(&inter->gpu);
        if (cpf) hz = (double)PSX_SYSCLK_HZ / (double)cpf;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::TextUnformatted("ZoniStation One [PSX]");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::Text("%s  %.2f Hz", pal ? "PAL" : "NTSC", hz);
    ImGui::PopStyleColor();

    // Chips
    ImGui::SameLine(0, 18);
    chip("BIOS", g_bios_name);
    ImGui::SameLine(); chip("DISC", g_disc_name);
    char pcbuf[16];
    snprintf(pcbuf, sizeof(pcbuf), "0x%08X", cpu ? cpu->current_pc : 0);
    ImGui::SameLine(); chip("PC", pcbuf);

    // Host HW Pill Banner
    ImGui::SameLine(0, 14);
    chip("HOST CPU", "Intel i9-14900HX");
    ImGui::SameLine(); chip("HOST GPU", "RTX 4060 Mobile");

    // Controls: direct Play / Pause / Step buttons + popup
    Debugger* dbg = inter ? &inter->debugger : nullptr;
    ImGui::SameLine(0, 12);

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
        micro_label("Logs");
        if (ImGui::MenuItem("Open all"))  for (auto& p : g_log_components) p.second.is_open = true;
        if (ImGui::MenuItem("Close all")) for (auto& p : g_log_components) p.second.is_open = false;
        if (ImGui::MenuItem("Snapshot all to logs/")) export_all_logs();
        ImGui::EndPopup();
    }

    // Vitals, right-aligned
    float vitals_w = 4 * 96.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > vitals_w) ImGui::SameLine(0, avail - vitals_w);
    else ImGui::SameLine();

    // Speed: budget / measured wall time. Reads low with panels open — that is
    // the whole point of the vital, not a benchmark figure.
    double speed = (g_vit_frame_ms > 0.001) ? (g_vit_budget_ms / g_vit_frame_ms) * 100.0 : 0.0;
    char sbuf[16]; snprintf(sbuf, sizeof(sbuf), "%.0f%%", speed);
    ImVec4 scol = speed >= 98.0 ? ZS_OK : (speed >= 90.0 ? ZS_WARN : ZS_CRIT);
    vital("Speed", sbuf, scol, (float)(speed / 100.0), scol);

    ImGui::SameLine();
    char fbuf[24]; snprintf(fbuf, sizeof(fbuf), "%.1f ms", g_vit_frame_ms);
    float ffrac = g_vit_budget_ms > 0.001 ? (float)(g_vit_frame_ms / g_vit_budget_ms) : 0.0f;
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
            dl->AddRectFilledMultiColor(p0, p1,
                IM_COL32(32, 46, 68, 255), IM_COL32(18, 26, 40, 255),
                IM_COL32(18, 26, 40, 255), IM_COL32(32, 46, 68, 255));
            dl->AddRect(p0, p1, IM_COL32(0, 240, 255, 180), 4.0f, 0, 1.0f);
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
            case MODE_PIPELINE: draw_ps_triangle(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_DISPLAY:  draw_ps_square(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_FRAME:    draw_ps_cross(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_CODE:     draw_ps_circle(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_MEMORY:   draw_ps_square(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_AUDIO:    draw_ps_circle(dl, icon_center, 5.5f, icon_col, 1.8f); break;
            case MODE_VRAM:     draw_ps_triangle(dl, icon_center, 5.5f, icon_col, 1.8f); break;
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
    float h = 92.0f;
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

    // Primary Metric
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::Text("%s %s", val_str, unit_str);
    ImGui::PopStyleColor();

    // Subtitle Metadata
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    if (sub1 && sub1[0]) ImGui::TextUnformatted(sub1);
    if (sub2 && sub2[0]) ImGui::TextUnformatted(sub2);
    ImGui::PopStyleColor();

    ImGui::Unindent(8.0f);
    ImGui::Dummy(ImVec2(0, 4.0f));
    ImGui::EndGroup();
}

static void draw_pipeline_view(Interconnect* inter) {
    Spu* spu = inter ? &inter->spu : nullptr;
    Mdec* mdec = inter ? &inter->mdec : nullptr;
    double t = ImGui::GetTime();

    static RateProbe pr_cd = {0,0,0,false}, pr_xa = {0,0,0,false}, pr_dma = {0,0,0,false};
    double cd_rate  = inter ? probe_rate(&pr_cd,  inter->cdrom.sectors_read_total,  t) : 0.0;
    double xa_rate  = inter ? probe_rate(&pr_xa,  inter->cdrom.audio_fifo.total_pushed, t) : 0.0;
    double dma_rate = inter ? probe_rate(&pr_dma, inter->dma.stat_ch2_uploads,      t) : 0.0;

    char cdr[16], cd_sub[32], xar[16], xa_sub[32], mdin[32], mdout[32], dmar[16], aq[32];
    snprintf(cdr, sizeof(cdr), "%.1f", cd_rate);
    snprintf(cd_sub, sizeof(cd_sub), "%u sectors total", inter ? inter->cdrom.sectors_read_total : 0);
    snprintf(xar, sizeof(xar), "%.0f", xa_rate);
    snprintf(xa_sub, sizeof(xa_sub), "FIFO queue: %d", spu ? spu_ring_used(spu) : 0);
    snprintf(mdin, sizeof(mdin), "In: %u blocks", mdec ? mdec->in_count : 0);
    snprintf(mdout, sizeof(mdout), "Out: %u blocks", mdec ? mdec->out_count : 0);
    snprintf(dmar, sizeof(dmar), "%.1f", dma_rate);
    snprintf(aq, sizeof(aq), "Buffer: %d/%d smp", spu ? spu_ring_used(spu) : 0, g_vit_aq_target);

    // Track 1: Video / Graphics Pipeline
    card_header("Video & Graphics Execution Pipeline", ZS_DATA);
    if (ImGui::BeginTable("##pipe_video", 5, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        pipe_node_card("1. CD-ROM Drive", ZS_DATA, "OK", ZS_OK, cdr, "sect/s", "Mode 2 / 2x Speed", cd_sub);

        ImGui::TableNextColumn();
        pipe_node_card("2. MDEC Decoder", ZS_DATA, "READY", ZS_OK, mdin, "", "Macroblock Motion JPEG", mdout);

        ImGui::TableNextColumn();
        pipe_node_card("3. DMA Ch2 (OT)", ZS_DATA, "OK", ZS_OK, dmar, "up/s", "GPU Ordering Table", "Ch2 Transfer Kick");

        ImGui::TableNextColumn();
        pipe_node_card("4. PSX GPU Engine", ZS_DATA, "RUNNING", ZS_OK, "3D Primitive", "Rasterizer", "Polygons & Textures", "Command Stream");

        ImGui::TableNextColumn();
        pipe_node_card("5. VRAM & Display", ZS_OK, "SYNC", ZS_OK, "1024x512", "VRAM", "CRTC Output", inter && inter->gpu.vmode == Pal ? "PAL 50Hz" : "NTSC 60Hz");

        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 10));

    // Track 2: Audio Pipeline
    card_header("Audio & Sound Processing Pipeline", ZS_AUDIO);
    if (ImGui::BeginTable("##pipe_audio", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        pipe_node_card("1. XA ADPCM Audio", ZS_AUDIO, "STREAM", ZS_OK, xar, "smp/s", "2352-byte Sectors", xa_sub);

        ImGui::TableNextColumn();
        pipe_node_card("2. SPU Synthesizer", ZS_AUDIO, "ACTIVE", ZS_OK, "24 Voices", "44.1 kHz", "ADSR & Pitch Modulation", "Reverb Processing");

        ImGui::TableNextColumn();
        pipe_node_card("3. Audio Mixer & Out", ZS_OK, "LIVE", ZS_OK, aq, "", "SDL2 / PipeWire Output", "Stereo PCM Stream");

        ImGui::EndTable();
    }
}

// Frame view — the per-frame event ring with cycle timestamps does not exist
// yet (Phase 4 in docs/ui/README.md: "the renderer records order today, not
// time"). Draw the track chrome and say plainly what is missing.
// Frame inspector (Phase 4): the frame's events on a time axis against its
// cycle budget. Recording is enabled only while this view is on screen — the
// standing constraint is that the panels cost, not the core.
static void draw_frame_view(Interconnect* inter) {
    frame_events_set_enabled(true);

    const FrameEventFrame* fr = frame_events_last();
    const uint32_t budget = inter ? gpu_cycles_per_frame(&inter->gpu) : 566203u;
    uint32_t span = fr->end_cycle - fr->start_cycle;
    if (span == 0) span = budget;

    // Header: how much of the budget the frame actually spanned, and drops.
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
        ImGui::Text("span %u cy of %u budget (%.1f%%)   events %u",
                    span, budget, 100.0 * (double)span / (double)budget, fr->count);
        ImGui::PopStyleColor();
        if (fr->dropped) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_WARN);
            ImGui::Text("  %u dropped (ring full)", fr->dropped);
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));

    struct Row { const char* label; FrameEventType type; ImVec4 col; };
    static const Row ROWS[] = {
        { "VRAM uploads", FEV_VRAM_UPLOAD, ZS_DATA  },
        { "VRAM copies",  FEV_VRAM_COPY,   ZS_DATA  },
        { "Draw batches", FEV_DRAW_BATCH,  ZS_OK    },
        { "DMA ch2",      FEV_DMA_GPU,     ZS_DATA  },
        { "XA sectors",   FEV_XA_SECTOR,   ZS_AUDIO },
    };
    const int NROWS = (int)(sizeof(ROWS) / sizeof(ROWS[0]));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ZS_RAIL_BG);
    ImGui::BeginChild("##frame", ImVec2(0, 0), true);

    const float LABEL_W = 108.0f;
    const float COUNT_W = 62.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < NROWS; i++) {
        ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
        ImGui::TextUnformatted(ROWS[i].label);
        ImGui::PopStyleColor();
        ImGui::SameLine(LABEL_W);

        ImGui::PushStyleColor(ImGuiCol_Text, fr->type_count[ROWS[i].type] ? ZS_TEXT : ZS_FAINT);
        ImGui::Text("%6u", fr->type_count[ROWS[i].type]);
        ImGui::PopStyleColor();
        ImGui::SameLine(LABEL_W + COUNT_W);

        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = 16.0f;
        if (w < 32.0f) w = 32.0f;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ZS_DOCK_BG), 2.0f);

        // Budget marker: where the nominal frame length falls on this axis.
        if (span > budget) {
            float bx = p.x + w * ((float)budget / (float)span);
            dl->AddLine(ImVec2(bx, p.y), ImVec2(bx, p.y + h), ImGui::GetColorU32(ZS_WARN), 1.0f);
        }

        ImU32 col = ImGui::GetColorU32(ROWS[i].col);
        for (uint32_t e = 0; e < fr->count; e++) {
            if (fr->events[e].type != ROWS[i].type) continue;
            float t = (float)(fr->events[e].cycle - fr->start_cycle) / (float)span;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float x = p.x + t * w;
            dl->AddRectFilled(ImVec2(x, p.y + 2), ImVec2(x + 1.5f, p.y + h - 2), col);
        }

        ImGui::Dummy(ImVec2(w, h + 5));
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("Each tick is one event, placed by CPU cycle within the frame. "
                       "The amber line marks the nominal budget when the frame overran it.");
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

static int  s_rebind_index = -1;

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

static void draw_controller_mapping_window() {
    if (!g_show_controller_mapping) return;

    ImGui::SetNextWindowSize(ImVec2(680, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Controller Mapping & Input Tester", &g_show_controller_mapping, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    Controller* ctrl = controller_get_active();
    if (!ctrl) {
        ImGui::TextColored(ZS_WARN, "Controller subsystem not active.");
        ImGui::End();
        return;
    }

    // Top device banner
    card_header("Active Controller Subsystem", ZS_OK);
    const char* dev_name = ctrl->gc ? SDL_GameControllerName(ctrl->gc) : "Keyboard Mapping (DualShock PS1 Emulation)";
    ImGui::Text("Device: %s", dev_name);
    ImGui::SameLine(0, 20);
    ImGui::TextColored(ctrl->connected ? ZS_OK : ZS_CRIT, "[%s]", ctrl->connected ? "CONNECTED" : "DISCONNECTED");

    // Real-time Input Tester
    uint16_t state = controller_update(ctrl); // 0=pressed, 1=released
    ImGui::Dummy(ImVec2(0, 4));
    card_header("Live Input Tester", ZS_DATA);
    ImGui::Text("PS1 Pad Word: 0x%04X", state);
    ImGui::SameLine(0, 16);
    ImGui::Text("L-Stick: (%d, %d)", ctrl->left_x, ctrl->left_y);
    ImGui::SameLine(0, 16);
    ImGui::Text("R-Stick: (%d, %d)", ctrl->right_x, ctrl->right_y);

    // Live Button LEDs
    ImGui::Dummy(ImVec2(0, 2));
    for (int i = 0; i < 16; i++) {
        bool pressed = ((state & (1u << i)) == 0);
        if (i > 0 && i % 4 != 0) ImGui::SameLine();
        ImVec4 col = pressed ? g_psx_button_info[i].col : ZS_MUTED;
        ImGui::PushStyleColor(ImGuiCol_Button, pressed ? COLA(0x38, 0xB0, 0x00, 0.35f) : COLA(0x16, 0x20, 0x30, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Button(g_psx_button_info[i].name, ImVec2(145, 24));
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0, 8));
    card_header("Keyboard Mapping Scancodes", ZS_WARN);

    // Presets
    if (ImGui::Button("Preset: Default (WASD + EZXC)")) {
        ctrl->key_map[0]  = SDL_SCANCODE_TAB;        // SELECT
        ctrl->key_map[3]  = SDL_SCANCODE_SPACE;      // START
        ctrl->key_map[4]  = SDL_SCANCODE_W;          // UP
        ctrl->key_map[5]  = SDL_SCANCODE_D;          // RIGHT
        ctrl->key_map[6]  = SDL_SCANCODE_S;          // DOWN
        ctrl->key_map[7]  = SDL_SCANCODE_A;          // LEFT
        ctrl->key_map[8]  = SDL_SCANCODE_LSHIFT;     // L2
        ctrl->key_map[9]  = SDL_SCANCODE_LCTRL;      // R2
        ctrl->key_map[10] = SDL_SCANCODE_Q;          // L1
        ctrl->key_map[11] = SDL_SCANCODE_R;          // R1
        ctrl->key_map[12] = SDL_SCANCODE_E;          // TRIANGLE
        ctrl->key_map[13] = SDL_SCANCODE_C;          // CIRCLE
        ctrl->key_map[14] = SDL_SCANCODE_Z;          // CROSS
        ctrl->key_map[15] = SDL_SCANCODE_X;          // SQUARE
    }
    ImGui::SameLine();
    if (ImGui::Button("Preset: Arcade (Arrows + WASD)")) {
        ctrl->key_map[4]  = SDL_SCANCODE_UP;         // UP
        ctrl->key_map[5]  = SDL_SCANCODE_RIGHT;      // RIGHT
        ctrl->key_map[6]  = SDL_SCANCODE_DOWN;       // DOWN
        ctrl->key_map[7]  = SDL_SCANCODE_LEFT;       // LEFT
        ctrl->key_map[12] = SDL_SCANCODE_W;          // TRIANGLE
        ctrl->key_map[13] = SDL_SCANCODE_D;          // CIRCLE
        ctrl->key_map[14] = SDL_SCANCODE_S;          // CROSS
        ctrl->key_map[15] = SDL_SCANCODE_A;          // SQUARE
    }

    static bool s_open_rebind_popup = false;
    static bool s_waiting_key_up = true;

    // Mapping Table
    if (ImGui::BeginTable("keymaptable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("PS1 Button");
        ImGui::TableSetupColumn("Symbol");
        ImGui::TableSetupColumn("Mapped Key Scancode");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        for (int i = 0; i < 16; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g_psx_button_info[i].name);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g_psx_button_info[i].symbol);
            ImGui::TableNextColumn();
            int sc = ctrl->key_map[i];
            const char* key_name = (sc > 0 && sc < SDL_NUM_SCANCODES) ? SDL_GetScancodeName((SDL_Scancode)sc) : "None";
            ImGui::Text("%s (scancode %d)", key_name, sc);
            ImGui::TableNextColumn();
            char btn_id[32];
            snprintf(btn_id, sizeof(btn_id), "Rebind##%d", i);
            if (ImGui::Button(btn_id)) {
                s_rebind_index = i;
                s_open_rebind_popup = true;
                s_waiting_key_up = true;
            }
        }
        ImGui::EndTable();
    }

    // Rebind Modal Popup
    if (s_open_rebind_popup) {
        ImGui::OpenPopup("Rebind Key Modal");
        s_open_rebind_popup = false;
    }

    if (s_rebind_index >= 0) {
        if (ImGui::BeginPopupModal("Rebind Key Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Press ANY keyboard key to bind to [%s]...", g_psx_button_info[s_rebind_index].name);
            ImGui::TextColored(ZS_FAINT, "(Press ESC to cancel)");
            ImGui::Dummy(ImVec2(0, 8));

            const uint8_t* keys = SDL_GetKeyboardState(NULL);

            if (s_waiting_key_up) {
                bool any_down = false;
                for (int k = 4; k < SDL_NUM_SCANCODES; k++) {
                    if (keys[k]) { any_down = true; break; }
                }
                if (!any_down) s_waiting_key_up = false;
            } else {
                for (int k = 4; k < SDL_NUM_SCANCODES; k++) {
                    if (keys[k]) {
                        if (k == SDL_SCANCODE_ESCAPE) {
                            s_rebind_index = -1;
                            s_waiting_key_up = true;
                            ImGui::CloseCurrentPopup();
                        } else {
                            ctrl->key_map[s_rebind_index] = k;
                            s_rebind_index = -1;
                            s_waiting_key_up = true;
                            ImGui::CloseCurrentPopup();
                        }
                        break;
                    }
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

    card_header("Host Machine Specifications", ZS_OK);
    if (ImGui::BeginTable("hosthwtable", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto kv = [](const char* k, const char* v, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
        };
        kv("Host Laptop Model", "ASUS ROG Strix G16 (G614JVR)", ZS_TEXT);
        kv("Host Processor", "Intel Core i9-14900HX (24C/32T)", ZS_OK);
        kv("Host Graphics GPU", "NVIDIA GeForce RTX 4060 Mobile (8GB)", ZS_DATA);
        kv("Graphics Driver Profile", "OpenGL 3.3 Core Profile (NVIDIA 550)", ZS_TEXT);
        kv("Host OS Kernel", "Linux 6.19 x86_64", ZS_FAINT);
        kv("System Memory (RAM)", "15.3 GB total (9.5 GB available)", ZS_WARN);
        kv("Process RSS Allocation", "184 MB allocated", ZS_TEXT);
        kv("Audio Subsystem Driver", "PipeWire / SDL2 Audio (12.8ms latency)", ZS_AUDIO);
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 8));

    card_header("Host CPU Worker Threads Load", ZS_DATA);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::Text("MIPS Worker Thread"); ImGui::SameLine(150);
    ImGui::ProgressBar(0.142f, ImVec2(avail - 150, 14), "14.2%");
    ImGui::Text("GPU Worker Thread"); ImGui::SameLine(150);
    ImGui::ProgressBar(0.086f, ImVec2(avail - 150, 14), "8.6%");
    ImGui::Text("Audio Worker Thread"); ImGui::SameLine(150);
    ImGui::ProgressBar(0.031f, ImVec2(avail - 150, 14), "3.1%");
    ImGui::Text("UI Render Thread"); ImGui::SameLine(150);
    ImGui::ProgressBar(0.024f, ImVec2(avail - 150, 14), "2.4%");

    ImGui::End();
}

// Contextual inspector (Pipeline / Script modes): VRAM diff status + audio + watches.
static void draw_inspector_window(Interconnect* inter) {
    if (!ImGui::Begin("Inspector", nullptr)) { ImGui::End(); return; }

    card_header("Host Machine Hardware", ZS_OK);
    if (ImGui::BeginTable("hostinspect", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto kv = [](const char* k, const char* v, ImVec4 col) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, col); ImGui::TextUnformatted(v); ImGui::PopStyleColor();
        };
        kv("Host System", "ASUS ROG Strix G16", ZS_TEXT);
        kv("Host CPU", "Intel i9-14900HX", ZS_OK);
        kv("Host GPU", "RTX 4060 Mobile 8GB", ZS_DATA);
        kv("Audio Subsystem", "PipeWire / SDL2", ZS_AUDIO);
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

    card_header("Pinned Watches (Memory Card Tiles)", ZS_PS_YELLOW);
    if (ImGui::BeginTable("watchtiles", 2, ImGuiTableFlags_SizingStretchSame)) {
        auto tile = [](const char* expr, const char* val, ImVec4 col) {
            ImGui::TableNextColumn();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = 46.0f;
            ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

            dl->AddRectFilledMultiColor(p0, p1,
                IM_COL32(22, 30, 44, 255), IM_COL32(14, 19, 28, 255),
                IM_COL32(14, 19, 28, 255), IM_COL32(22, 30, 44, 255));
            dl->AddRect(p0, p1, IM_COL32(40, 56, 82, 255), 6.0f);
            dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), ImGui::GetColorU32(col), 3.0f);

            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8.0f, p0.y + 4.0f));
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
            ImGui::TextUnformatted(expr);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(val);
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::SetCursorScreenPos(p0);
            ImGui::Dummy(ImVec2(w, h + 4.0f));
        };
        tile("spu.voice[0].pitch", "0x0200", ZS_AUDIO);
        tile("gpu.status.interlace", "1", ZS_DATA);
        tile("dma.ch2.madr", "0x8016DC38", ZS_OK);
        char buf[32]; snprintf(buf, sizeof(buf), "%d smp", g_vit_aq);
        tile("cd.fifo.depth", buf, ZS_WARN);
        ImGui::EndTable();
    }

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
        ImGuiID stage_top = ImGui::DockBuilderSplitNode(root, ImGuiDir_Up, 0.52f, nullptr, &stage_bottom);
        ImGui::DockBuilderDockWindow("PS1 Display", stage_top);
        if (body) ImGui::DockBuilderDockWindow(body, stage_bottom);
    }
    if (insp)  ImGui::DockBuilderDockWindow(insp, inspector);
    if (insp2) ImGui::DockBuilderDockWindow(insp2, inspector);

    // Logs tabbed along the bottom, every mode.
    for (auto& pair : g_log_components)
        ImGui::DockBuilderDockWindow(pair.second.name, bottom);

    ImGui::DockBuilderFinish(dockspace_id);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" void debug_ui_init(SDL_Window* window, SDL_GLContext gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    /* Load crisp system TTF font if available, else scale default font */
    ImFont* main_font = nullptr;
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 2;
    font_cfg.OversampleV = 2;
    font_cfg.PixelSnapH = true;

    if (access("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", F_OK) == 0) {
        main_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf", 15.0f, &font_cfg);
    } else if (access("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", F_OK) == 0) {
        main_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f, &font_cfg);
    } else if (access("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", F_OK) == 0) {
        main_font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 15.0f, &font_cfg);
    }

    if (!main_font) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = 1.15f;
    }

    apply_zonistation_style();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Open per-component log files (one file per category, lives for whole session) */
    mkdir("logs", 0755);
    for (auto& pair : g_log_components) {
        char path[160];
        snprintf(path, sizeof(path), "logs/%s.log", pair.second.name);
        pair.second.file = fopen(path, "w");
        if (pair.second.file) {
            setvbuf(pair.second.file, NULL, _IOLBF, 4096);
        }
    }

    log_add_sink(log_sink_callback, nullptr);
}

extern "C" void debug_ui_process_event(SDL_Event* event) {
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void debug_ui_render(void* cpu_ptr, void* interconnect_ptr) {
    Cpu* cpu = (Cpu*)cpu_ptr;
    Interconnect* inter = (Interconnect*)interconnect_ptr;

    /* ImGui_ImplOpenGL3_NewFrame() moved to GPU thread — owns GL context */
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

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
    GLuint tex_id = 0;
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
    for (auto& pair : g_log_components)
        draw_component_log_window(pair.second);

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
        for (int i = 0; i < MODE_COUNT; i++)
            if (ImGui::IsKeyPressed(mode_keys[i]) && g_mode != i) {
                g_mode = i; g_layout_dirty = true;
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
        for (auto& pair : g_log_components) {
            if (pair.second.file) {
                fflush(pair.second.file);
                fclose(pair.second.file);
                pair.second.file = nullptr;
            }
        }
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
