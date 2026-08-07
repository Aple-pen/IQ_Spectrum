#include "App.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>


#include <cstdio>

int main() {
  if (!glfwInit()) {
    std::fprintf(stderr, "Failed to initialize GLFW\n");
    return 1;
  }

  const char *glslVersion = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  GLFWwindow *window =
      glfwCreateWindow(1280, 780, "Bin TCP Spectrum", nullptr, nullptr);
  if (window == nullptr) {
    std::fprintf(stderr, "Failed to create GLFW window\n");
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Multi-viewport: ImGui 창을 메인 윈도우 밖으로 끌어내면 별도 OS 창이 된다
  // (Zone Configuration / Constellation 등을 다른 모니터에 띄울 수 있다).
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  {
    // 분리된 플랫폼 창은 투명/둥근 모서리를 지원하지 않으므로 불투명·직각으로.
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  App app;
  app.LoadSettings();

  // Render one frame (called from both the main loop and the refresh callback)
  auto renderFrame = [&]() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    app.Render();

    ImGui::Render();
    int displayWidth = 0;
    int displayHeight = 0;
    glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
    glViewport(0, 0, displayWidth, displayHeight);
    glClearColor(0.06f, 0.07f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // 메인 윈도우 밖으로 끌어낸 창들을 각자의 OS 창에 그린다. 현재 GL 컨텍스트를
    // 바꾸므로 호출 후 원래 컨텍스트로 복구해야 한다.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      GLFWwindow *backupContext = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backupContext);
    }

    glfwSwapBuffers(window);
  };

  // Called by Windows modal loop while the user drags/resizes the window
  glfwSetWindowUserPointer(window, &renderFrame);
  glfwSetWindowRefreshCallback(window, [](GLFWwindow *w) {
    auto *fn =
        static_cast<decltype(renderFrame) *>(glfwGetWindowUserPointer(w));
    (*fn)();
  });
  // WM_PAINT (refresh) fires on resize but NOT on move; pos callback covers
  // move
  glfwSetWindowPosCallback(window, [](GLFWwindow *w, int, int) {
    auto *fn =
        static_cast<decltype(renderFrame) *>(glfwGetWindowUserPointer(w));
    (*fn)();
  });

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    renderFrame();
  }

  app.SaveSettings();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}