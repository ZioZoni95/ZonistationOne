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

extern "C" {
#include "cpu.h"
#include "interconnect.h"
#include "renderer.h"
#include "debugger.h"
#include "spu.h"
#include "lua_debug.h"
#include "frame_events.h"
}

extern "C" const char* disassemble_mips(uint32_t instruction, uint32_t pc);

// ---------------------------------------------------------------------------
// Visual identity — the design tokens from docs/ui/ui_direction.html.
// The two accents ENCODE the data path: cyan is the video chain
// (CD → MDEC → DMA → VRAM), rose is the audio chain (XA → SPU → device).
// Severity (ok/warn/crit) is separate and reserved — never decoration.
// ---------------------------------------------------------------------------

#define COL(r,g,b)  ImVec4((r)/255.0f, (g)/255.0f, (b)/255.0f, 1.0f)
#define COLA(r,g,b,a) ImVec4((r)/255.0f, (g)/255.0f, (b)/255.0f, (a))

static const ImVec4 ZS_GROUND    = COL(0x0E, 0x11, 0x17);
static const ImVec4 ZS_PANEL     = COL(0x16, 0x1B, 0x24);
static const ImVec4 ZS_PANEL2    = COL(0x1B, 0x21, 0x30);
static const ImVec4 ZS_LINE      = COL(0x24, 0x2C, 0x3A);
static const ImVec4 ZS_LINE_SOFT = COL(0x1E, 0x25, 0x32);
static const ImVec4 ZS_TEXT      = COL(0xDD, 0xE4, 0xF0);
static const ImVec4 ZS_MUTED     = COL(0x87, 0x92, 0xA6);
static const ImVec4 ZS_FAINT     = COL(0x5C, 0x65, 0x79);
static const ImVec4 ZS_DATA      = COL(0x35, 0xC3, 0xF0);   // video path (cyan)
static const ImVec4 ZS_AUDIO     = COL(0xFF, 0x5C, 0x8A);   // audio path (rose)
static const ImVec4 ZS_OK        = COL(0x4C, 0xC3, 0x8A);
static const ImVec4 ZS_WARN      = COL(0xE8, 0xB3, 0x3A);
static const ImVec4 ZS_CRIT      = COL(0xE5, 0x48, 0x4D);
static const ImVec4 ZS_RAIL_BG   = COL(0x12, 0x16, 0x1F);
static const ImVec4 ZS_DOCK_BG   = COL(0x10, 0x15, 0x1E);
static const ImVec4 ZS_MODE_ON   = COL(0x1B, 0x24, 0x34);

static void apply_zonistation_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);

    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 5.0f;
    s.FrameRounding     = 3.0f;
    s.PopupRounding     = 4.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 3.0f;
    s.ScrollbarRounding = 4.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;
    s.WindowPadding     = ImVec2(10, 10);
    s.FramePadding      = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.ItemInnerSpacing  = ImVec2(6, 4);
    s.CellPadding       = ImVec2(7, 3);
    s.GrabMinSize       = 9.0f;
    s.ScrollbarSize     = 11.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ZS_TEXT;
    c[ImGuiCol_TextDisabled]         = ZS_FAINT;
    c[ImGuiCol_WindowBg]             = ZS_PANEL;
    c[ImGuiCol_ChildBg]             = COLA(0x12, 0x16, 0x1F, 1.0f);
    c[ImGuiCol_PopupBg]              = ZS_PANEL2;
    c[ImGuiCol_Border]               = ZS_LINE;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = COLA(0x10, 0x15, 0x1E, 1.0f);
    c[ImGuiCol_FrameBgHovered]       = ZS_LINE_SOFT;
    c[ImGuiCol_FrameBgActive]        = ZS_LINE;
    c[ImGuiCol_TitleBg]              = ZS_GROUND;
    c[ImGuiCol_TitleBgActive]        = COLA(0x1A, 0x21, 0x30, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]     = ZS_GROUND;
    c[ImGuiCol_MenuBarBg]            = COLA(0x14, 0x1A, 0x25, 1.0f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ZS_LINE;
    c[ImGuiCol_ScrollbarGrabHovered] = ZS_MUTED;
    c[ImGuiCol_ScrollbarGrabActive]  = ZS_DATA;
    c[ImGuiCol_CheckMark]            = ZS_DATA;
    c[ImGuiCol_SliderGrab]           = ZS_DATA;
    c[ImGuiCol_SliderGrabActive]     = ZS_DATA;
    c[ImGuiCol_Button]               = COLA(0x1D, 0x26, 0x35, 1.0f);
    c[ImGuiCol_ButtonHovered]        = COLA(0x25, 0x30, 0x44, 1.0f);
    c[ImGuiCol_ButtonActive]         = ZS_LINE;
    c[ImGuiCol_Header]               = ZS_MODE_ON;
    c[ImGuiCol_HeaderHovered]        = COLA(0x17, 0x1E, 0x2B, 1.0f);
    c[ImGuiCol_HeaderActive]         = ZS_LINE;
    c[ImGuiCol_Separator]            = ZS_LINE;
    c[ImGuiCol_SeparatorHovered]     = ZS_DATA;
    c[ImGuiCol_SeparatorActive]      = ZS_DATA;
    c[ImGuiCol_ResizeGrip]           = ZS_LINE;
    c[ImGuiCol_ResizeGripHovered]    = ZS_MUTED;
    c[ImGuiCol_ResizeGripActive]     = ZS_DATA;
    c[ImGuiCol_Tab]                  = ZS_DOCK_BG;
    c[ImGuiCol_TabHovered]           = ZS_MODE_ON;
    c[ImGuiCol_TabActive]            = ZS_PANEL2;
    c[ImGuiCol_TabUnfocused]         = ZS_DOCK_BG;
    c[ImGuiCol_TabUnfocusedActive]   = ZS_PANEL;
    c[ImGuiCol_DockingPreview]       = COLA(0x35, 0xC3, 0xF0, 0.35f);
    c[ImGuiCol_DockingEmptyBg]       = ZS_GROUND;
    c[ImGuiCol_TableHeaderBg]        = COLA(0x14, 0x1A, 0x25, 1.0f);
    c[ImGuiCol_TableBorderStrong]    = ZS_LINE;
    c[ImGuiCol_TableBorderLight]     = ZS_LINE_SOFT;
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = COLA(0xFF, 0xFF, 0xFF, 0.015f);
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
}

// ---------------------------------------------------------------------------
// View modes — the mode rail replaces the scattered floating windows
// ---------------------------------------------------------------------------

enum ViewMode {
    MODE_PIPELINE = 0, MODE_DISPLAY, MODE_FRAME, MODE_CODE,
    MODE_MEMORY, MODE_AUDIO, MODE_VRAM, MODE_SCRIPT, MODE_COUNT
};

struct ModeDef { const char* name; const char* key; bool audio; };
static const ModeDef g_modes[MODE_COUNT] = {
    { "Pipeline", "F1", false },
    { "Display",  "F2", false },
    { "Frame",    "F3", false },
    { "Code",     "F4", false },
    { "Memory",   "F5", false },
    { "Audio",    "F6", true  },
    { "VRAM",     "F7", false },
    { "Script",   "F8", false },
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

static bool     g_disasm_follow_pc    = true;
static uint32_t g_disasm_view_addr    = 0xBFC00000;
static char     g_goto_addr_buf[12]   = "";
static bool     g_scroll_to_pc        = false;

// Step edge: set by UI, consumed once by main loop via debug_ui_step_requested()
static bool g_step_pending = false;

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

    ImGui::SetNextWindowSize(ImVec2(260, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CPU Registers", &g_show_registers)) { ImGui::End(); return; }

    if (!cpu) { ImGui::TextDisabled("No CPU"); ImGui::End(); return; }

    // PC / COP0
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
    ImGui::Text("PC   %08X", cpu->current_pc);
    ImGui::PopStyleColor();
    ImGui::Text("SR   %08X", cpu->sr);
    ImGui::Text("Cause%08X", cpu->cause);
    ImGui::Text("EPC  %08X", cpu->epc);
    ImGui::Text("HI   %08X  LO   %08X", cpu->hi, cpu->lo);
    ImGui::Separator();

    // GPR table: 2 columns
    if (ImGui::BeginTable("gpr", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        for (int i = 0; i < 32; i++) {
            ImGui::TableNextColumn();
            uint32_t val = cpu->regs[i];
            if (val != 0)
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.5f, 1.0f), "$%-4s %08X", s_gpr_names[i], val);
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

    ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SPU Debug", &g_show_spu)) { ImGui::End(); return; }

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
            // The scanout pass already extracted the CRTC display window out of
            // the unified VRAM (and unpacked 15/24bpp) into scanout_texture's
            // lower-left corner, upright — so just show that sub-rect 1:1.
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
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad_x,
                                       ImGui::GetCursorPosY() + pad_y));

            ImGui::Image((void*)(intptr_t)texture_id, ImVec2(disp_w, disp_h),
                         ImVec2(u0, v0), ImVec2(u1, v1));
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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLA(0x17, 0x1D, 0x2B, 1.0f));
    ImGui::BeginChild("##MachineBar", ImVec2(0, 52), false, ImGuiWindowFlags_NoScrollbar);

    // Brand + video-mode subtitle
    bool pal = inter && inter->gpu.vmode == Pal;
    double hz = 0.0;
    if (inter) {
        uint32_t cpf = gpu_cycles_per_frame(&inter->gpu);
        if (cpf) hz = (double)PSX_SYSCLK_HZ / (double)cpf;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::TextUnformatted("ZoniStation One");
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

    // Controls, folded into a popup (replaces the old menu bar): pause/step,
    // log level, log windows.
    Debugger* dbg = inter ? &inter->debugger : nullptr;
    ImGui::SameLine();
    if (ImGui::Button("Controls")) ImGui::OpenPopup("##ctrl");
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

    micro_label("View");
    ImGui::Dummy(ImVec2(0, 2));

    for (int i = 0; i < MODE_COUNT; i++) {
        bool on = (g_mode == i);
        ImVec4 accent = g_modes[i].audio ? ZS_AUDIO : ZS_DATA;

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float row_h = ImGui::GetFrameHeight() + 4.0f;
        float row_w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (on) {
            dl->AddRectFilled(p0, ImVec2(p0.x + row_w, p0.y + row_h),
                              ImGui::GetColorU32(ZS_MODE_ON), 4.0f);
            dl->AddRectFilled(p0, ImVec2(p0.x + 2.0f, p0.y + row_h),
                              ImGui::GetColorU32(accent));
        }

        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COLA(0x17, 0x1E, 0x2B, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ZS_MODE_ON);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ZS_TEXT : ZS_MUTED);
        if (ImGui::Button(g_modes[i].name, ImVec2(row_w, row_h))) {
            if (g_mode != i) { g_mode = i; g_layout_dirty = true; }
        }
        ImGui::PopStyleColor(4);
        ImGui::PopID();

        // Shortcut key, right-aligned inside the row
        ImVec2 kp = ImVec2(p0.x + row_w - 22.0f, p0.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(kp, ImGui::GetColorU32(ZS_FAINT), g_modes[i].key);
        ImGui::Dummy(ImVec2(0, 1));
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

// One pipeline node: name + dot, a big tabular rate, unit, two meta lines.
static void pipe_node(const char* name, ImVec4 dot,
                      const char* rate, const char* unit,
                      const char* meta1, const char* meta2) {
    ImGui::BeginGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddCircleFilled(ImVec2(p.x + 4, p.y + ImGui::GetTextLineHeight() * 0.5f),
                        3.0f, ImGui::GetColorU32(dot));
    ImGui::Dummy(ImVec2(12, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::TextUnformatted(name);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, ZS_TEXT);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(rate);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 4);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextUnformatted(unit);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::TextUnformatted(meta1);
    ImGui::TextUnformatted(meta2);
    ImGui::PopStyleColor();
    ImGui::EndGroup();
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
static void draw_pipeline_view(Interconnect* inter) {
    Spu* spu = inter ? &inter->spu : nullptr;
    Mdec* mdec = inter ? &inter->mdec : nullptr;
    double t = ImGui::GetTime();

    static RateProbe pr_cd = {0,0,0,false}, pr_xa = {0,0,0,false},
                     pr_dma = {0,0,0,false};
    double cd_rate  = inter ? probe_rate(&pr_cd,  inter->cdrom.sectors_read_total,  t) : 0.0;
    double xa_rate  = inter ? probe_rate(&pr_xa,  inter->cdrom.audio_fifo.total_pushed, t) : 0.0;
    double dma_rate = inter ? probe_rate(&pr_dma, inter->dma.stat_ch2_uploads,      t) : 0.0;

    char cd[24], cdr[16], xa[24], xar[16], mdin[32], mdout[32], dmar[16], aq[24];
    snprintf(cdr,  sizeof(cdr),  "%.1f", cd_rate);
    snprintf(cd,   sizeof(cd),   "%u total", inter ? inter->cdrom.sectors_read_total : 0);
    snprintf(xar,  sizeof(xar),  "%.0f", xa_rate);
    snprintf(xa,   sizeof(xa),   "queue %d", spu ? spu_ring_used(spu) : 0);
    snprintf(mdin, sizeof(mdin), "in %u", mdec ? mdec->in_count : 0);
    snprintf(mdout,sizeof(mdout),"out %u", mdec ? mdec->out_count : 0);
    snprintf(dmar, sizeof(dmar), "%.1f", dma_rate);
    snprintf(aq,   sizeof(aq),   "%d / %d", spu ? spu_ring_used(spu) : 0, g_vit_aq_target);

    // The mockup's bordered 5-cell row with hairline separators between stages
    // and a connector chevron from each stage into the next.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ZS_RAIL_BG);
    ImGui::BeginChild("##pipe", ImVec2(0, 132), true);

    ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame;
    if (ImGui::BeginTable("##pipetbl", 5, tf)) {
        struct { const char* name; ImVec4 dot; const char* rate; const char* unit;
                 const char* m1; const char* m2; bool link_audio; bool last; } N[5] = {
            { "CD drive",    ZS_DATA,  cdr,    "sect/s",   "2x",           cd,    false, false },
            { "XA decode",   ZS_AUDIO, xar,    "smp/s",    "37800 Hz st",  xa,    true,  false },
            { "MDEC",        ZS_DATA,  "queue","",         mdin,           mdout, false, false },
            { "DMA ch2",     ZS_DATA,  dmar,   "up/s",     "block @ kick", "GPU ch2", false, false },
            { "VRAM -> out", ZS_OK,    "live", "",         "scanout",      aq,    false, true  },
        };
        ImGui::TableNextRow();
        for (int i = 0; i < 5; i++) {
            ImGui::TableSetColumnIndex(i);
            ImGui::Dummy(ImVec2(0, 2));
            pipe_node(N[i].name, N[i].dot, N[i].rate, N[i].unit, N[i].m1, N[i].m2);

            // Connector chevron into the next stage, coloured by the path.
            if (!N[i].last) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 rc = ImGui::GetItemRectMax();
                float cx = ImGui::GetWindowPos().x - ImGui::GetScrollX()
                         + (i + 1) * (ImGui::GetWindowSize().x / 5.0f);
                float cy = ImGui::GetWindowPos().y + 34.0f;
                ImU32 col = ImGui::GetColorU32(N[i].link_audio ? ZS_AUDIO : ZS_DATA);
                dl->AddLine(ImVec2(cx - 7, cy - 4), ImVec2(cx + 1, cy), col, 1.6f);
                dl->AddLine(ImVec2(cx + 1, cy), ImVec2(cx - 7, cy + 4), col, 1.6f);
                (void)rc;
            }
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Legend — the two accents are the data path, stated plainly.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 lp = ImGui::GetCursorScreenPos();
    float y = lp.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddRectFilled(ImVec2(lp.x, y - 4), ImVec2(lp.x + 9, y + 5), ImGui::GetColorU32(ZS_DATA), 2.0f);
    ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::Text("Video path  CD -> MDEC -> DMA -> VRAM");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 20);
    lp = ImGui::GetCursorScreenPos();
    y = lp.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddRectFilled(ImVec2(lp.x, y - 4), ImVec2(lp.x + 9, y + 5), ImGui::GetColorU32(ZS_AUDIO), 2.0f);
    ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_MUTED);
    ImGui::Text("Audio path  XA -> SPU -> device");
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

// Contextual inspector (Pipeline / Script modes): VRAM diff status + audio + watches.
static void draw_inspector_window(Interconnect* inter) {
    if (!ImGui::Begin("Inspector", nullptr)) { ImGui::End(); return; }

    // VRAM: CPU vs GPU. gpu.vram.data is the CPU-side model — uploads, fills and
    // VRAM→VRAM copies land there, rasterized pixels never do. Every halfword
    // counted here is a pixel the GL pipeline drew that GP0(0xC0) readback,
    // GP0(0x80) copy and texture sampling cannot see. That is gaps 3.1-3.3.
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

    card_header("Pinned watches", ZS_FAINT);
    ImGui::PushStyleColor(ImGuiCol_Text, ZS_FAINT);
    ImGui::TextWrapped("Pin any Lua expression here as a live tile (Phase 6).");
    ImGui::PopStyleColor();

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
        case MODE_SCRIPT:
            draw_lua_console_window();
            draw_inspector_window(inter);
            break;
    }

    // Logs tabbed along the bottom, every mode
    for (auto& pair : g_log_components)
        draw_component_log_window(pair.second);

    ImGui::End(); // DockHost

    // Keyboard — F1..F8 pick the mode; F10 (or Pause) toggles pause; F11 steps.
    // Pause is NOT on Space: Space is the emulated pad's START button, and the
    // controller reads the raw SDL keyboard state, so binding both made every
    // press of START also halt the machine.
    if (!ImGui::GetIO().WantTextInput) {
        ImGuiKey mode_keys[MODE_COUNT] = {
            ImGuiKey_F1, ImGuiKey_F2, ImGuiKey_F3, ImGuiKey_F4,
            ImGuiKey_F5, ImGuiKey_F6, ImGuiKey_F7, ImGuiKey_F8
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
