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
}

extern "C" const char* disassemble_mips(uint32_t instruction, uint32_t pc);

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
    std::vector<LogEntry> buffer;
    ImGuiTextFilter filter;
};

static std::map<LogCategory, LogComponent> g_log_components = {
    {LOG_CAT_SYSTEM,       {"System",       LOG_CAT_SYSTEM,       true, true, true, {}, {}}},
    {LOG_CAT_CPU,          {"CPU",          LOG_CAT_CPU,          true, true, true, {}, {}}},
    {LOG_CAT_IRQ,          {"IRQ",          LOG_CAT_IRQ,          true, true, true, {}, {}}},
    {LOG_CAT_DMA,          {"DMA",          LOG_CAT_DMA,          true, true, true, {}, {}}},
    {LOG_CAT_GPU,          {"GPU",          LOG_CAT_GPU,          true, true, true, {}, {}}},
    {LOG_CAT_CDROM,        {"CDROM",        LOG_CAT_CDROM,        true, true, true, {}, {}}},
    {LOG_CAT_TIMER,        {"Timer",        LOG_CAT_TIMER,        true, true, true, {}, {}}},
    {LOG_CAT_BIOS,         {"BIOS",         LOG_CAT_BIOS,         true, true, true, {}, {}}},
    {LOG_CAT_INTERCONNECT, {"Interconnect", LOG_CAT_INTERCONNECT, true, true, true, {}, {}}},
    {LOG_CAT_RENDERER,     {"Renderer",     LOG_CAT_RENDERER,     true, true, true, {}, {}}},
    {LOG_CAT_EVENT,        {"Event",        LOG_CAT_EVENT,        true, true, true, {}, {}}},
    {LOG_CAT_GTE,          {"GTE",          LOG_CAT_GTE,          true, true, true, {}, {}}},
    {LOG_CAT_VRAM,         {"VRAM",         LOG_CAT_VRAM,         true, true, true, {}, {}}},
    {LOG_CAT_RAM,          {"RAM",          LOG_CAT_RAM,          true, true, true, {}, {}}},
    {LOG_CAT_DEBUG,        {"Debug",        LOG_CAT_DEBUG,        true, true, true, {}, {}}},
    {LOG_CAT_MDEC,         {"MDEC",         LOG_CAT_MDEC,         true, true, true, {}, {}}},
    {LOG_CAT_SPU,          {"SPU",          LOG_CAT_SPU,          true, true, true, {}, {}}}
};

static std::mutex g_log_mutex;

static void log_sink_callback(int category, int level, const char* msg, void* udata) {
    (void)udata;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    auto it = g_log_components.find((LogCategory)category);
    if (it != g_log_components.end()) {
        it->second.buffer.push_back({level, msg});
        if (it->second.buffer.size() > 5000)
            it->second.buffer.erase(it->second.buffer.begin());
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
    if (ImGui::Button("Export")) export_component_log(comp);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &comp.auto_scroll);
    ImGui::SameLine();
    ImGui::Checkbox("Mono", &comp.monospace);
    ImGui::SameLine();
    bool copy = ImGui::Button("Copy");
    ImGui::SameLine();
    comp.filter.Draw("Filter", -100.0f);

    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (comp.monospace) ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
    if (copy) ImGui::LogToClipboard();

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::vector<int> indices;
        for (int i = 0; i < (int)comp.buffer.size(); i++)
            if (comp.filter.PassFilter(comp.buffer[i].message.c_str()))
                indices.push_back(i);

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
    ImGui::End();
}

// ---------------------------------------------------------------------------
// SPU Debug window
// ---------------------------------------------------------------------------

static const char* s_adsr_phase_names[] = {
    "Off", "Attack", "Decay", "Sustain", "Release"
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
                bool active = (voice->adsr_phase != ADSR_PHASE_OFF);

                if (!active && !ImGui::GetIO().KeyShift) continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", v);

                ImGui::TableNextColumn();
                if (active)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_adsr_phase_names[voice->adsr_phase]);
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
// PS1 Display window
// ---------------------------------------------------------------------------

static void draw_ps1_display(GLuint texture_id) {
    if (!g_show_display) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("PS1 Display", &g_show_display,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (texture_id)
            ImGui::Image((void*)(intptr_t)texture_id, size, ImVec2(0, 1), ImVec2(1, 0));
        else
            ImGui::TextDisabled("Display not ready");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Dockspace initial layout
// ---------------------------------------------------------------------------

static void setup_dockspace(ImGuiID dockspace_id) {
    static bool first_time = true;
    if (!first_time) return;
    first_time = false;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID main = dockspace_id;
    ImGuiID right = ImGui::DockBuilderSplitNode(main, ImGuiDir_Right, 0.38f, nullptr, &main);
    ImGuiID right_bottom;
    ImGuiID right_top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.62f, nullptr, &right_bottom);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, 0.22f, nullptr, &main);

    ImGui::DockBuilderDockWindow("PS1 Display",   main);
    ImGui::DockBuilderDockWindow("Disassembly",   right_top);
    ImGui::DockBuilderDockWindow("CPU Registers", right_bottom);
    ImGui::DockBuilderDockWindow("Breakpoints",   right_bottom);
    ImGui::DockBuilderDockWindow("SPU Debug",     right_bottom);
    ImGui::DockBuilderDockWindow("BIOS",          bottom);

    for (auto& pair : g_log_components) {
        if (pair.second.category != LOG_CAT_BIOS)
            ImGui::DockBuilderDockWindow(pair.second.name, bottom);
    }
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
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    log_add_sink(log_sink_callback, nullptr);
}

extern "C" void debug_ui_process_event(SDL_Event* event) {
    ImGui_ImplSDL2_ProcessEvent(event);
}

extern "C" void debug_ui_render(void* cpu_ptr, void* interconnect_ptr) {
    Cpu* cpu = (Cpu*)cpu_ptr;
    Interconnect* inter = (Interconnect*)interconnect_ptr;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // --- Main Menu Bar ---
    if (ImGui::BeginMainMenuBar()) {

        // Emulator menu
        if (ImGui::BeginMenu("Emulator")) {
            Debugger* dbg = inter ? &inter->debugger : nullptr;
            bool paused = dbg && dbg->paused;
            if (!paused) {
                if (ImGui::MenuItem("Pause", "F5")) { if (dbg) dbg->paused = true; }
            } else {
                if (ImGui::MenuItem("Run",   "F5")) { if (dbg) { dbg->paused = false; g_disasm_follow_pc = true; } }
                if (ImGui::MenuItem("Step",  "F11")) g_step_pending = true;
            }
            ImGui::EndMenu();
        }

        // Debug menu
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Disassembly",   nullptr, &g_show_disasm);
            ImGui::MenuItem("CPU Registers", nullptr, &g_show_registers);
            ImGui::MenuItem("Breakpoints",   nullptr, &g_show_breakpoints);
            ImGui::MenuItem("SPU Debug",     nullptr, &g_show_spu);
            ImGui::MenuItem("PS1 Display",   nullptr, &g_show_display);
            ImGui::EndMenu();
        }

        // Logs menu
        if (ImGui::BeginMenu("Logs")) {
            if (ImGui::MenuItem("Open All")) {
                for (auto& p : g_log_components) p.second.is_open = true;
            }
            if (ImGui::MenuItem("Close All")) {
                for (auto& p : g_log_components) p.second.is_open = false;
            }
            if (ImGui::MenuItem("Export All to logs/")) {
                export_all_logs();
            }
            ImGui::Separator();
            for (auto& pair : g_log_components)
                ImGui::MenuItem(pair.second.name, nullptr, &pair.second.is_open);
            ImGui::EndMenu();
        }

        // Options menu
        if (ImGui::BeginMenu("Options")) {
            extern LogLevel current_log_level;
            int level = (int)current_log_level;
            ImGui::TextUnformatted("Log Level:");
            if (ImGui::RadioButton("Silent", level == LOG_LEVEL_SILENT)) log_set_level(LOG_LEVEL_SILENT);
            if (ImGui::RadioButton("Error",  level == LOG_LEVEL_ERROR))  log_set_level(LOG_LEVEL_ERROR);
            if (ImGui::RadioButton("Warn",   level == LOG_LEVEL_WARN))   log_set_level(LOG_LEVEL_WARN);
            if (ImGui::RadioButton("Info",   level == LOG_LEVEL_INFO))   log_set_level(LOG_LEVEL_INFO);
            if (ImGui::RadioButton("Debug",  level == LOG_LEVEL_DEBUG))  log_set_level(LOG_LEVEL_DEBUG);
            if (ImGui::RadioButton("Trace",  level == LOG_LEVEL_TRACE))  log_set_level(LOG_LEVEL_TRACE);
            ImGui::EndMenu();
        }

        // Status: show PAUSED indicator in menu bar
        if (inter && inter->debugger.paused) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 80.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::TextUnformatted("  [PAUSED]  ");
            ImGui::PopStyleColor();
        }

        ImGui::EndMainMenuBar();
    }

    // --- Full-screen DockSpace ---
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

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    setup_dockspace(dockspace_id);

    // Draw windows
    draw_disasm_window(cpu, inter);
    draw_registers_window(cpu);
    if (inter) draw_breakpoints_window(inter);
    if (inter) draw_spu_debug_window(&inter->spu);

    for (auto& pair : g_log_components)
        draw_component_log_window(pair.second);

    GLuint tex_id = 0;
    if (inter) tex_id = renderer_get_display_texture(&inter->gpu.renderer);
    draw_ps1_display(tex_id);

    ImGui::End(); // DockHost

    // Keyboard shortcuts (F5 / F11) — processed when ImGui has focus
    if (!ImGui::GetIO().WantTextInput && inter) {
        Debugger* dbg = &inter->debugger;
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
            if (dbg->paused) { dbg->paused = false; g_disasm_follow_pc = true; }
            else dbg->paused = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F11) && dbg->paused)
            g_step_pending = true;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window* bkp_win = SDL_GL_GetCurrentWindow();
        SDL_GLContext bkp_ctx = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(bkp_win, bkp_ctx);
    }
}

extern "C" void debug_ui_shutdown(void) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
