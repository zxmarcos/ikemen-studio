// The debugger's windows and its state (app.cpp).
#pragma once

// After ImGui::CreateContext: settings handler, ini path. With launch, the game
// starts on the first frame (once the saved settings are read).
void App_Init(bool launch);
void App_Poll();     // reads what the game sent
void App_Frame();    // App_Poll + every window, between NewFrame and Render
void App_Shutdown(); // ends the game it started
void App_Console(const char *lua); // scripted tests: run Lua as if typed in the Console
