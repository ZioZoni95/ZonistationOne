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

extern "C" {
#include "cpu.h"
#include "interconnect.h"
#include "renderer.h"
}

// Disassembler from cpu_disasm.c
extern "C" const char* disassemble_mips(uint32_t instruction, uint32_t pc);

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
    {LOG_CAT_SYSTEM,       {"System Log",       LOG_CAT_SYSTEM,       false, true, true, {}, {}}},
    {LOG_CAT_CPU,          {"CPU Log",          LOG_CAT_CPU,          false, true, true, {}, {}}},
    {LOG_CAT_IRQ,          {"IRQ Log",          LOG_CAT_IRQ,          false, true, true, {}, {}}},
    {LOG_CAT_DMA,          {"DMA Log",          LOG_CAT_DMA,          false, true, true, {}, {}}},
    {LOG_CAT_GPU,          {"GPU Log",          LOG_CAT_GPU,          false, true, true, {}, {}}},
    {LOG_CAT_CDROM,        {"CDROM Log",        LOG_CAT_CDROM,        false, true, true, {}, {}}},
    {LOG_CAT_TIMER,        {"Timer Log",        LOG_CAT_TIMER,        false, true, true, {}, {}}},
    {LOG_CAT_BIOS,         {"BIOS Log",         LOG_CAT_BIOS,         true,  true, true, {}, {}}}, // Default open
    {LOG_CAT_INTERCONNECT, {"Interconnect Log", LOG_CAT_INTERCONNECT, false, true, true, {}, {}}},
    {LOG_CAT_RENDERER,     {"Renderer Log",     LOG_CAT_RENDERER,     false, true, true, {}, {}}},
    {LOG_CAT_EVENT,        {"Event Log",        LOG_CAT_EVENT,        false, true, true, {}, {}}},
    {LOG_CAT_GTE,          {"GTE Log",          LOG_CAT_GTE,          false, true, true, {}, {}}},
    {LOG_CAT_VRAM,         {"VRAM Log",         LOG_CAT_VRAM,         false, true, true, {}, {}}},
    {LOG_CAT_RAM,          {"RAM Log",          LOG_CAT_RAM,          false, true, true, {}, {}}},
    {LOG_CAT_DEBUG,        {"Debug Log",        LOG_CAT_DEBUG,        false, true, true, {}, {}}},
    {LOG_CAT_MDEC,         {"MDEC Log",         LOG_CAT_MDEC,         false, true, true, {}, {}}}
};

static std::mutex g_log_mutex;

static bool g_show_display = true;
static bool g_show_disasm = true;

static void log_sink_callback(int category, int level, const char* msg, void* udata) {
    (void)udata;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    
    auto it = g_log_components.find((LogCategory)category);
    if (it != g_log_components.end()) {
        it->second.buffer.push_back({level, msg});
        if (it->second.buffer.size() > 5000) {
            it->second.buffer.erase(it->second.buffer.begin());
        }
    }
}

extern "C" void debug_ui_init(SDL_Window* window, SDL_GLContext gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

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

static void draw_component_log_window(LogComponent& comp) {
    if (!comp.is_open) return;

    ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(comp.name, &comp.is_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        comp.buffer.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &comp.auto_scroll);
    ImGui::SameLine();
    ImGui::Checkbox("Monospace", &comp.monospace);
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
        
        std::vector<int> line_offsets;
        for (int i = 0; i < (int)comp.buffer.size(); i++) {
            const auto& entry = comp.buffer[i];
            if (comp.filter.PassFilter(entry.message.c_str())) {
                line_offsets.push_back(i);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(line_offsets.size());
        while (clipper.Step()) {
            for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++) {
                const auto& entry = comp.buffer[line_offsets[line_no]];
                
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                if (entry.level == LOG_LEVEL_ERROR) color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                else if (entry.level == LOG_LEVEL_WARN) color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
                else if (entry.level == LOG_LEVEL_DEBUG) color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
                else if (entry.level == LOG_LEVEL_TRACE) color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(entry.message.c_str());
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

static void draw_disasm_window(Cpu* cpu, Interconnect* interconnect) {
    if (!g_show_disasm) return;

    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CPU Disassembly", &g_show_disasm)) {
        ImGui::End();
        return;
    }

    if (!cpu || !interconnect) {
        ImGui::Text("CPU/Interconnect not initialized.");
        ImGui::End();
        return;
    }

    uint32_t pc = cpu->pc;
    uint32_t start_pc = (pc > 0x40) ? (pc - 0x40) : 0;
    uint32_t end_pc = pc + 0x100;

    ImGui::BeginChild("DisasmScroll", ImVec2(0, 0), true);
    for (uint32_t current_addr = start_pc; current_addr < end_pc; current_addr += 4) {
        uint32_t instruction = interconnect_load32(interconnect, current_addr);
        const char* disasm_text = disassemble_mips(instruction, current_addr);

        if (current_addr == pc) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow for current PC
            ImGui::Text("-> %08X: %08X  %s", current_addr, instruction, disasm_text);
            ImGui::PopStyleColor();
            
            // Auto scroll to PC if out of view
            if (ImGui::GetScrollY() == 0 || ImGui::GetScrollY() == ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(0.5f);
            }
        } else {
            ImGui::Text("   %08X: %08X  %s", current_addr, instruction, disasm_text);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

static void draw_ps1_display(GLuint texture_id) {
    if (!g_show_display) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("PS1 Display", &g_show_display, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImVec2 size = ImGui::GetContentRegionAvail();
        if (texture_id) {
            ImGui::Image((void*)(intptr_t)texture_id, size, ImVec2(0, 1), ImVec2(1, 0)); // UV flipped for OpenGL
        } else {
            ImGui::Text("Display not initialized.");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

static void setup_dockspace(ImGuiID dockspace_id) {
    static bool first_time = true;
    if (first_time) {
        first_time = false;
        
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.35f, nullptr, &dock_main_id);
        ImGuiID dock_down_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("PS1 Display", dock_main_id);
        ImGui::DockBuilderDockWindow("CPU Disassembly", dock_right_id);
        ImGui::DockBuilderDockWindow("BIOS Log", dock_down_id);
        
        // All other logs will dock to the bottom by default if opened
        for (auto& pair : g_log_components) {
            if (pair.second.category != LOG_CAT_BIOS) {
                ImGui::DockBuilderDockWindow(pair.second.name, dock_down_id);
            }
        }
        ImGui::DockBuilderFinish(dockspace_id);
    }
}

extern "C" void debug_ui_render(void* cpu_ptr, void* interconnect_ptr) {
    Cpu* cpu = (Cpu*)cpu_ptr;
    Interconnect* interconnect = (Interconnect*)interconnect_ptr;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Setup Main Menu Bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Views")) {
            ImGui::MenuItem("PS1 Display", NULL, &g_show_display);
            ImGui::MenuItem("CPU Disassembly", NULL, &g_show_disasm);
            ImGui::Separator();
            for (auto& pair : g_log_components) {
                ImGui::MenuItem(pair.second.name, NULL, &pair.second.is_open);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Options")) {
            extern LogLevel current_log_level;
            int level = (int)current_log_level;
            ImGui::Text("Global Log Level:");
            if (ImGui::RadioButton("Silent", level == LOG_LEVEL_SILENT)) log_set_level(LOG_LEVEL_SILENT);
            if (ImGui::RadioButton("Error", level == LOG_LEVEL_ERROR)) log_set_level(LOG_LEVEL_ERROR);
            if (ImGui::RadioButton("Warn", level == LOG_LEVEL_WARN)) log_set_level(LOG_LEVEL_WARN);
            if (ImGui::RadioButton("Info", level == LOG_LEVEL_INFO)) log_set_level(LOG_LEVEL_INFO);
            if (ImGui::RadioButton("Debug", level == LOG_LEVEL_DEBUG)) log_set_level(LOG_LEVEL_DEBUG);
            if (ImGui::RadioButton("Trace", level == LOG_LEVEL_TRACE)) log_set_level(LOG_LEVEL_TRACE);
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }

    // Create a full-screen dockspace for the ImGui host window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    setup_dockspace(dockspace_id);

    // Render inner windows
    for (auto& pair : g_log_components) {
        draw_component_log_window(pair.second);
    }
    
    draw_disasm_window(cpu, interconnect);
    
    GLuint tex_id = 0;
    if (interconnect) {
        tex_id = renderer_get_display_texture(&interconnect->gpu.renderer);
    }
    draw_ps1_display(tex_id);

    ImGui::End(); // End DockSpace

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }
}

extern "C" void debug_ui_shutdown(void) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

