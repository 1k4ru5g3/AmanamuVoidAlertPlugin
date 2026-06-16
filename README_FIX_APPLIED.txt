Fix applied by ChatGPT

Changed files:
- AmanamuVoidAlert.cpp
  - DrawSettings no longer calls ImGui::SetCurrentContext(ctx()->ImGuiContext).
  - DrawSettings only uses ImGui::GetCurrentContext() guard and can show controls even when HostCompatible() is false.
- sdk/PluginSDK.h
  - Host ABI compatibility check is patched to require the ABI only up to enumerate_monster_mods instead of sizeof(HostAbi).

Build steps:
1. Open AmanamuVoidAlert.sln
2. Clean Solution
3. Rebuild x64 Release
4. Delete old DLLs from the POEFixer plugin folder
5. Copy only the newly built DLL into the plugin folder

If DrawSettings still crashes after this, the next likely cause is an ImGui binary/version mismatch between the running POEFixer host and the imgui/ folder compiled into the plugin. In that case copy the entire imgui/ folder from the exact ExamplePlugin commit that matches your running POEFixer version.
