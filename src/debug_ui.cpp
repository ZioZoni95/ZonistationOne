#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "debug_ui.h"
#include "log.h"
#include <vector>
#include <string>
#include <mutex>

struct LogEntry {
    int category;
    int level;
    std::string message;
};

static std::vector<LogEntry> g_log_buffer;
static std::mutex g_log_mutex;
static bool g_auto_scroll = true;

static void log_sink_callback(int category, int level, const char* msg, void* udata) {
    (void)category;
    (void)udata;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_buffer.push_back({category, level, msg});
    if (g_log_buffer.size() > 2000) {
        g_log_buffer.erase(g_log_buffer.begin());
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

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
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

static void draw_log_window() {
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Emulator Logs")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_buffer.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &g_auto_scroll);

    ImGui::Separator();
    ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        for (const auto& entry : g_log_buffer) {
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

    if (g_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
    ImGui::End();
}

extern "C" void debug_ui_render(void) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    draw_log_window();

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
