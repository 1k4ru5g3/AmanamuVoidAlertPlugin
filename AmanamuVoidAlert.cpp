// ============================================================================
// AmanamuVoidPlugin.cpp
// POE2Fixer / POEFixer SDK v6 Plugin
//
// Funktion:
// - Erkennt Amanamu/Lightless-Well-Monster sofort über MonsterMods / OMP:
//      MonsterAbyssLightlessFaction1
//      Metadata/Monsters/MonsterMods/LeagueAbyss/LightlessWells
//      HASH16 0x63D1
//      HASH32 0xBFDA2A36
//
// - Erkennt zusätzlich den Cloud-Status über Buffs:
//      INSIDE CLOUD  = Monster hat aktuell "abyss_lightless_well_immune..."
//      OUTSIDE CLOUD = Monster hat den MonsterMod, aber nicht den Inside-Buff
//
// - Buff+Cache bleibt als Fallback erhalten.
// - Zeichnet On-Screen Marker und Edge-Pfeile.
// - Pfeil kann auch angezeigt werden, wenn Monster bereits sichtbar ist.
// - Debug-Fenster mit MonsterMods und Buffs.
// ============================================================================

#define NOMINMAX

#include "sdk/PluginSDK.h"

// Je nach Projekt-Setup kann einer dieser Includes passen.
// Falls "imgui/imgui.h" nicht gefunden wird, nimm stattdessen: #include "imgui.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr const char* kPluginName = "Amanamu Void Alert";

    constexpr const char* kBuffPrefixAbyssLightlessWell = "abyss_lightless_well";
    constexpr const char* kBuffInsideCloud = "abyss_lightless_well_immune";

    constexpr const char* kExpectedMonsterModId = "MonsterAbyssLightlessFaction1";
    constexpr const char* kExpectedMonsterModMetadata =
        "Metadata/Monsters/MonsterMods/LeagueAbyss/LightlessWells";

    constexpr uint16_t kExpectedMonsterModHash16 = 0x63D1;
    constexpr uint32_t kExpectedMonsterModHash32 = 0xBFDA2A36;

    static std::string ToLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty())
            return true;

        std::string h = ToLower(haystack);
        std::string n = ToLower(needle);
        return h.find(n) != std::string::npos;
    }

    static std::string WideToNarrowAscii(const std::wstring& ws)
    {
        std::string out;
        out.reserve(ws.size());
        for (wchar_t c : ws)
            out.push_back(c < 128 ? static_cast<char>(c) : '?');
        return out;
    }

    static float Distance2D(float ax, float ay, float bx, float by)
    {
        const float dx = ax - bx;
        const float dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    static ImVec2 Normalize(ImVec2 v)
    {
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len <= 0.001f)
            return ImVec2(0.0f, -1.0f);

        return ImVec2(v.x / len, v.y / len);
    }

    static ImVec2 ClampToScreenEdge(ImVec2 center, ImVec2 dir, float width, float height, float margin)
    {
        dir = Normalize(dir);

        float tx = 999999.0f;
        float ty = 999999.0f;

        if (std::abs(dir.x) > 0.001f)
        {
            const float edgeX = dir.x > 0.0f ? (width - margin) : margin;
            tx = (edgeX - center.x) / dir.x;
        }

        if (std::abs(dir.y) > 0.001f)
        {
            const float edgeY = dir.y > 0.0f ? (height - margin) : margin;
            ty = (edgeY - center.y) / dir.y;
        }

        float t = std::min(tx, ty);
        if (t < 0.0f || !std::isfinite(t))
            t = 0.0f;

        return ImVec2(center.x + dir.x * t, center.y + dir.y * t);
    }

    static void DrawArrow(ImDrawList* draw, ImVec2 pos, ImVec2 dir, ImU32 color)
    {
        dir = Normalize(dir);
        ImVec2 perp(-dir.y, dir.x);

        const float size = 18.0f;
        const ImVec2 tip(pos.x + dir.x * size, pos.y + dir.y * size);
        const ImVec2 back(pos.x - dir.x * size * 0.65f, pos.y - dir.y * size * 0.65f);

        const ImVec2 p1 = tip;
        const ImVec2 p2(back.x + perp.x * size * 0.55f, back.y + perp.y * size * 0.55f);
        const ImVec2 p3(back.x - perp.x * size * 0.55f, back.y - perp.y * size * 0.55f);

        draw->AddTriangleFilled(p1, p2, p3, color);
        draw->AddTriangle(p1, p2, p3, IM_COL32(0, 0, 0, 220), 2.0f);
    }

    struct TrackedMonster
    {
        uint32_t Id = 0;
        uintptr_t Address = 0;
        std::string Path;

        bool SeenThisFrame = false;
        bool InsideCloud = false;
        bool HasAnyLightlessBuff = false;
        bool HasAmanamuMonsterMod = false;

        float Distance = 0.0f;
        float LastSeenSeconds = 0.0f;

        std::vector<std::string> LastBuffs;
        std::vector<std::string> LastMonsterMods;
    };
}

class AmanamuVoidPlugin final : public PluginSDK::Plugin
{
public:
    const char* GetName() const override
    {
        return kPluginName;
    }

    void OnEnable(bool /*isGameAttached*/) override
    {
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        LoadSettings();

        m_EnableTime = std::chrono::steady_clock::now();
        ctx()->Log.Info("Amanamu Void Alert enabled");
    }

    void OnDisable() override
    {
        SaveSettings();
        m_Tracked.clear();

        if (ctx())
            ctx()->Log.Info("Amanamu Void Alert disabled");
    }

    bool WantsOverlay() const override
    {
        return m_EnableOverlay;
    }

    void DrawSettings() override
    {
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        ImGui::Checkbox("Enable overlay", &m_EnableOverlay);
        ImGui::Checkbox("Show debug window", &m_ShowDebugWindow);
        ImGui::Checkbox("Draw on-screen labels", &m_DrawOnScreenLabels);
        ImGui::Checkbox("Draw off-screen / edge arrows", &m_DrawOffscreenArrows);
        ImGui::Checkbox("Draw edge arrow even when monster is on screen", &m_DrawEdgeArrowForOnScreenMonsters);
        ImGui::Checkbox("Draw circle around monster", &m_DrawCircle);
        ImGui::Checkbox("Only rare/unique monsters", &m_OnlyRareOrUnique);
        ImGui::Checkbox("Log newly detected monsters", &m_LogNewDetections);

        ImGui::Separator();

        ImGui::SliderFloat("Max tracking distance", &m_MaxDistance, 500.0f, 8000.0f, "%.0f");
        ImGui::SliderFloat("Forget after seconds", &m_ForgetAfterSeconds, 1.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Label Y offset", &m_LabelYOffset, 20.0f, 140.0f, "%.0f");
        ImGui::SliderFloat("Circle radius", &m_CircleRadius, 12.0f, 80.0f, "%.0f");

        ImGui::Separator();

        ImGui::TextWrapped("Primary detection:");
        ImGui::BulletText("MonsterMod Id: %s", kExpectedMonsterModId);
        ImGui::BulletText("MonsterMod Metadata: %s", kExpectedMonsterModMetadata);
        ImGui::BulletText("MonsterMod Hash16: 0x%04X", static_cast<unsigned int>(kExpectedMonsterModHash16));
        ImGui::BulletText("MonsterMod Hash32: 0x%08X", static_cast<unsigned int>(kExpectedMonsterModHash32));

        ImGui::Separator();

        ImGui::TextWrapped("Cloud status detection:");
        ImGui::BulletText("Inside cloud: monster buff contains '%s'", kBuffInsideCloud);
        ImGui::BulletText("Fallback known monster: entity had any buff containing '%s'", kBuffPrefixAbyssLightlessWell);

        if (ImGui::Button("Clear tracked monsters"))
            m_Tracked.clear();
    }

    void DrawUI() override
    {
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        if (!m_EnableOverlay && !m_ShowDebugWindow)
            return;

        PluginSDK::Snapshot snapshot = ctx()->Game.GetSnapshot();

        if (!snapshot.IsAttached || snapshot.State != PluginSDK::GameState::InGame)
            return;

        if (snapshot.IsTown || snapshot.IsHideout || snapshot.IsPaused)
            return;

        const float now = SecondsSinceEnable();

        MarkAllUnseen();
        ScanEntities(snapshot, now);
        PruneOld(now);

        if (m_EnableOverlay)
            DrawOverlay(snapshot);

        if (m_ShowDebugWindow)
            DrawDebugWindow(snapshot);
    }

    void SaveSettings() override
    {
        namespace fs = std::filesystem;

        fs::path dir = DirectoryPath() / "config";
        std::error_code ec;
        fs::create_directories(dir, ec);

        std::ofstream file(dir / "settings.txt");
        if (!file.is_open())
            return;

        file << "EnableOverlay=" << (m_EnableOverlay ? 1 : 0) << "\n";
        file << "ShowDebugWindow=" << (m_ShowDebugWindow ? 1 : 0) << "\n";
        file << "DrawOnScreenLabels=" << (m_DrawOnScreenLabels ? 1 : 0) << "\n";
        file << "DrawOffscreenArrows=" << (m_DrawOffscreenArrows ? 1 : 0) << "\n";
        file << "DrawEdgeArrowForOnScreenMonsters=" << (m_DrawEdgeArrowForOnScreenMonsters ? 1 : 0) << "\n";
        file << "DrawCircle=" << (m_DrawCircle ? 1 : 0) << "\n";
        file << "OnlyRareOrUnique=" << (m_OnlyRareOrUnique ? 1 : 0) << "\n";
        file << "LogNewDetections=" << (m_LogNewDetections ? 1 : 0) << "\n";
        file << "MaxDistance=" << m_MaxDistance << "\n";
        file << "ForgetAfterSeconds=" << m_ForgetAfterSeconds << "\n";
        file << "LabelYOffset=" << m_LabelYOffset << "\n";
        file << "CircleRadius=" << m_CircleRadius << "\n";
    }

private:
    float SecondsSinceEnable() const
    {
        using namespace std::chrono;
        return duration<float>(steady_clock::now() - m_EnableTime).count();
    }

    void LoadSettings()
    {
        namespace fs = std::filesystem;

        fs::path settingsPath = DirectoryPath() / "config" / "settings.txt";
        if (!fs::exists(settingsPath))
            return;

        std::ifstream file(settingsPath);
        if (!file.is_open())
            return;

        std::string line;
        while (std::getline(file, line))
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);

            const bool b = value == "1";

            try
            {
                if (key == "EnableOverlay") m_EnableOverlay = b;
                else if (key == "ShowDebugWindow") m_ShowDebugWindow = b;
                else if (key == "DrawOnScreenLabels") m_DrawOnScreenLabels = b;
                else if (key == "DrawOffscreenArrows") m_DrawOffscreenArrows = b;
                else if (key == "DrawEdgeArrowForOnScreenMonsters") m_DrawEdgeArrowForOnScreenMonsters = b;
                else if (key == "DrawCircle") m_DrawCircle = b;
                else if (key == "OnlyRareOrUnique") m_OnlyRareOrUnique = b;
                else if (key == "LogNewDetections") m_LogNewDetections = b;
                else if (key == "MaxDistance") m_MaxDistance = std::stof(value);
                else if (key == "ForgetAfterSeconds") m_ForgetAfterSeconds = std::stof(value);
                else if (key == "LabelYOffset") m_LabelYOffset = std::stof(value);
                else if (key == "CircleRadius") m_CircleRadius = std::stof(value);
            }
            catch (...)
            {
                // Ignore bad config values.
            }
        }
    }

    void MarkAllUnseen()
    {
        for (auto& [id, tracked] : m_Tracked)
            tracked.SeenThisFrame = false;
    }

    bool IsMonsterCandidate(const PluginSDK::Entity& e) const
    {
        if (!e.IsValid)
            return false;

        if (e.EntityType != PluginSDK::EntityType::Monster)
            return false;

        if (e.EntityState == PluginSDK::EntityState::MonsterFriendly)
            return false;

        if (e.CurrentHP <= 0 && e.MaxHP > 0)
            return false;

        if (m_OnlyRareOrUnique && e.Rarity < 2)
            return false;

        // Wichtig:
        // Früher war hier nur HasBuffs().
        // Für Soforterkennung brauchen wir auch Entities mit OMP, bevor ein Buff existiert.
        if (!e.Components.HasBuffs() && !e.Components.HasOMP())
            return false;

        return true;
    }

    struct BuffScanResult
    {
        bool HasAnyLightlessWellBuff = false;
        bool InsideCloud = false;
        std::vector<std::string> BuffNames;
    };

    BuffScanResult ScanBuffs(const PluginSDK::Entity& e) const
    {
        BuffScanResult result;

        if (!e.Components.HasBuffs())
            return result;

        std::vector<PluginSDK::Buff> buffs = ctx()->Components.EnumerateBuffs(e.Components.Buffs);

        result.BuffNames.reserve(buffs.size());

        for (const PluginSDK::Buff& buff : buffs)
        {
            result.BuffNames.push_back(buff.Name);

            if (ContainsInsensitive(buff.Name, kBuffPrefixAbyssLightlessWell))
                result.HasAnyLightlessWellBuff = true;

            if (ContainsInsensitive(buff.Name, kBuffInsideCloud))
                result.InsideCloud = true;
        }

        return result;
    }

    bool HasAmanamuMonsterMod(const PluginSDK::Entity& e, std::vector<std::string>* debugMods = nullptr) const
    {
        if (!e.Components.HasOMP())
            return false;

        std::vector<PluginSDK::MonsterMod> mods =
            ctx()->Components.EnumerateMonsterMods(e.Components.OMP);

        bool found = false;

        for (const PluginSDK::MonsterMod& mod : mods)
        {
            if (debugMods)
            {
                char line[768];
                std::snprintf(
                    line,
                    sizeof(line),
                    "%s | %s | %s | h16=0x%04X h32=0x%08X gen=%d",
                    mod.Id.c_str(),
                    mod.Name.c_str(),
                    mod.Metadata.c_str(),
                    static_cast<unsigned int>(mod.Hash16),
                    static_cast<unsigned int>(mod.Hash32),
                    static_cast<int>(mod.GenerationType)
                );

                debugMods->push_back(line);
            }

            if (mod.Id == kExpectedMonsterModId)
                found = true;

            if (mod.Metadata == kExpectedMonsterModMetadata)
                found = true;

            if (mod.Hash16 == kExpectedMonsterModHash16)
                found = true;

            if (mod.Hash32 == kExpectedMonsterModHash32)
                found = true;
        }

        return found;
    }

    bool HasInterestingStatsFallback(const PluginSDK::Entity& e) const
    {
        // Optionaler Fallback/Debug:
        // Row 4754 MonsterProximalTangibility1 hatte Stat1 = 19783.
        // Das ist NICHT der bevorzugte LightlessWells-Mod Row 14451.
        // Default bewusst false lassen, um false positives zu vermeiden.
        //
        // Zum Testen könntest du aktivieren:
        //
        // if (e.Components.HasStats())
        // {
        //     for (auto s : ctx()->Components.EnumerateStats(e.Components.Stats))
        //         if (s.Key == 19783) return true;
        // }

        (void)e;
        return false;
    }

    void ScanEntities(const PluginSDK::Snapshot& snapshot, float now)
    {
        const auto& player = snapshot.Player;

        for (const PluginSDK::Entity& e : snapshot.Entities)
        {
            if (!IsMonsterCandidate(e))
                continue;

            const float distance = Distance2D(player.WorldX, player.WorldY, e.WorldX, e.WorldY);
            if (distance > m_MaxDistance)
                continue;

            BuffScanResult buffResult = ScanBuffs(e);

            std::vector<std::string> monsterModDebug;
            const bool foundByMonsterMod = HasAmanamuMonsterMod(e, &monsterModDebug);

            const bool foundByBuff = buffResult.HasAnyLightlessWellBuff;
            const bool foundByStatsFallback = HasInterestingStatsFallback(e);

            auto existingIt = m_Tracked.find(e.Id);
            const bool alreadyKnown = existingIt != m_Tracked.end();

            if (!foundByMonsterMod && !foundByBuff && !foundByStatsFallback && !alreadyKnown)
                continue;

            const bool isNew = !alreadyKnown;

            TrackedMonster& tracked = m_Tracked[e.Id];
            tracked.Id = e.Id;
            tracked.Address = e.Address;
            tracked.Path = WideToNarrowAscii(e.Path);
            tracked.SeenThisFrame = true;
            tracked.Distance = distance;
            tracked.LastSeenSeconds = now;

            tracked.HasAmanamuMonsterMod = foundByMonsterMod || tracked.HasAmanamuMonsterMod;
            tracked.HasAnyLightlessBuff = buffResult.HasAnyLightlessWellBuff || tracked.HasAnyLightlessBuff;

            // Status:
            // MonsterMod sagt: Das ist ein relevantes Monster.
            // Buff sagt: Es steht gerade in der Cloud.
            tracked.InsideCloud = buffResult.InsideCloud;

            tracked.LastBuffs = std::move(buffResult.BuffNames);

            if (!monsterModDebug.empty())
                tracked.LastMonsterMods = std::move(monsterModDebug);

            if (isNew && m_LogNewDetections)
            {
                char msg[768];
                std::snprintf(
                    msg,
                    sizeof(msg),
                    "Detected Amanamu/Lightless monster: id=%u byMod=%d byBuff=%d path=%s",
                    tracked.Id,
                    foundByMonsterMod ? 1 : 0,
                    foundByBuff ? 1 : 0,
                    tracked.Path.c_str()
                );
                ctx()->Log.Info(msg);
            }
        }
    }

    void PruneOld(float now)
    {
        std::vector<uint32_t> eraseIds;

        for (const auto& [id, tracked] : m_Tracked)
        {
            if ((now - tracked.LastSeenSeconds) > m_ForgetAfterSeconds)
                eraseIds.push_back(id);
        }

        for (uint32_t id : eraseIds)
            m_Tracked.erase(id);
    }

    void DrawOverlay(const PluginSDK::Snapshot& snapshot)
    {
        if (m_Tracked.empty())
            return;

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (!draw)
            return;

        const float screenW = static_cast<float>(snapshot.ScreenWidth);
        const float screenH = static_cast<float>(snapshot.ScreenHeight);

        if (screenW <= 0.0f || screenH <= 0.0f)
            return;

        const ImVec2 screenCenter(screenW * 0.5f, screenH * 0.5f);

        for (auto& [id, tracked] : m_Tracked)
        {
            auto entityOpt = ctx()->Entities.FindById(id);
            if (!entityOpt.has_value())
                continue;

            const PluginSDK::Entity& e = entityOpt.value();

            if (!e.IsValid)
                continue;

            // Live-Recheck direkt vor dem Zeichnen:
            // - MonsterMod erneut prüfen, falls Snapshot/OMP später verfügbar wird.
            // - Buff erneut prüfen, damit INSIDE/OUTSIDE schneller reagiert.
            std::vector<std::string> liveMonsterModDebug;
            const bool liveHasMonsterMod = HasAmanamuMonsterMod(e, &liveMonsterModDebug);

            if (liveHasMonsterMod)
                tracked.HasAmanamuMonsterMod = true;

            if (!liveMonsterModDebug.empty())
                tracked.LastMonsterMods = std::move(liveMonsterModDebug);

            if (e.Components.HasBuffs())
            {
                BuffScanResult liveBuffResult = ScanBuffs(e);

                tracked.InsideCloud = liveBuffResult.InsideCloud;
                tracked.HasAnyLightlessBuff =
                    liveBuffResult.HasAnyLightlessWellBuff || tracked.HasAnyLightlessBuff;

                tracked.LastBuffs = std::move(liveBuffResult.BuffNames);
            }

            float sx = 0.0f;
            float sy = 0.0f;

            const float markerZ = e.WorldZ + std::max(e.ModelBoundsZ, 80.0f);

            const bool onScreen = ctx()->Render.WorldToScreen(
                e.WorldX,
                e.WorldY,
                markerZ,
                sx,
                sy
            );

            const ImU32 insideColor = IM_COL32(180, 80, 255, 255);
            const ImU32 outsideColor = IM_COL32(80, 255, 120, 255);
            const ImU32 textShadow = IM_COL32(0, 0, 0, 230);
            const ImU32 color = tracked.InsideCloud ? insideColor : outsideColor;

            const bool visibleOnScreen =
                onScreen &&
                sx >= 0.0f &&
                sx <= screenW &&
                sy >= 0.0f &&
                sy <= screenH;

            if (visibleOnScreen)
            {
                if (m_DrawCircle)
                    draw->AddCircle(ImVec2(sx, sy), m_CircleRadius, color, 48, 3.0f);

                if (m_DrawOnScreenLabels)
                {
                    const char* state = tracked.InsideCloud
                        ? "INSIDE CLOUD"
                        : "OUTSIDE CLOUD";

                    char label[192];
                    std::snprintf(
                        label,
                        sizeof(label),
                        "AMANAMU VOID\n%s\n%.0f",
                        state,
                        tracked.Distance
                    );

                    const ImVec2 textSize = ImGui::CalcTextSize(label);
                    const ImVec2 textPos(
                        sx - textSize.x * 0.5f,
                        sy - m_LabelYOffset - textSize.y
                    );

                    draw->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), textShadow, label);
                    draw->AddText(textPos, color, label);
                }
            }

            // Pfeil wird gezeichnet, wenn:
            // - Monster off-screen ist
            // - oder Monster on-screen ist und die Option aktiv ist
            const bool shouldDrawEdgeArrow =
                m_DrawOffscreenArrows &&
                (!visibleOnScreen || m_DrawEdgeArrowForOnScreenMonsters);

            if (shouldDrawEdgeArrow)
            {
                ImVec2 dir(0.0f, -1.0f);

                if (visibleOnScreen)
                {
                    // Bei sichtbarem Monster ist die Screenposition die beste Richtung.
                    dir = ImVec2(sx - screenCenter.x, sy - screenCenter.y);
                }
                else if (std::isfinite(sx) && std::isfinite(sy) && (sx != 0.0f || sy != 0.0f))
                {
                    // Off-screen, aber WorldToScreen liefert brauchbare projizierte Koordinaten.
                    dir = ImVec2(sx - screenCenter.x, sy - screenCenter.y);
                }
                else
                {
                    // Fallback: World-Differenz Player -> Monster.
                    const float dx = e.WorldX - snapshot.Player.WorldX;
                    const float dy = e.WorldY - snapshot.Player.WorldY;
                    dir = ImVec2(dx, dy);
                }

                dir = Normalize(dir);

                const ImVec2 arrowPos = ClampToScreenEdge(
                    screenCenter,
                    dir,
                    screenW,
                    screenH,
                    55.0f
                );

                DrawArrow(draw, arrowPos, dir, color);

                char text[96];
                std::snprintf(
                    text,
                    sizeof(text),
                    "VOID %.0f %s",
                    tracked.Distance,
                    tracked.InsideCloud ? "IN" : "OUT"
                );

                const ImVec2 textSize = ImGui::CalcTextSize(text);
                const ImVec2 textPos(
                    arrowPos.x - textSize.x * 0.5f,
                    arrowPos.y + 22.0f
                );

                draw->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), textShadow, text);
                draw->AddText(textPos, color, text);
            }
        }
    }

    void DrawDebugWindow(const PluginSDK::Snapshot& snapshot)
    {
        ImGui::SetNextWindowSize(ImVec2(760, 440), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Amanamu Void Alert Debug", &m_ShowDebugWindow))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Area: %s", snapshot.CurrentAreaName.c_str());
        ImGui::Text("Tracked monsters: %zu", m_Tracked.size());
        ImGui::Text("MonsterMod Id: %s", kExpectedMonsterModId);
        ImGui::Text("MonsterMod Metadata: %s", kExpectedMonsterModMetadata);
        ImGui::Text("MonsterMod Hash16: 0x%04X", static_cast<unsigned int>(kExpectedMonsterModHash16));
        ImGui::Text("MonsterMod Hash32: 0x%08X", static_cast<unsigned int>(kExpectedMonsterModHash32));
        ImGui::Text("Inside-cloud buff: %s", kBuffInsideCloud);

        ImGui::Separator();

        if (ImGui::Button("Clear tracked"))
            m_Tracked.clear();

        ImGui::SameLine();

        if (ImGui::Button("Save settings"))
            SaveSettings();

        ImGui::Separator();

        if (ImGui::BeginTable("##tracked_amanamu", 7,
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Id");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("ByMod");
            ImGui::TableSetupColumn("Dist");
            ImGui::TableSetupColumn("Seen");
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Mods/Buffs");
            ImGui::TableHeadersRow();

            const float now = SecondsSinceEnable();

            for (const auto& [id, tracked] : m_Tracked)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", tracked.Id);

                ImGui::TableSetColumnIndex(1);
                if (tracked.InsideCloud)
                    ImGui::TextColored(ImVec4(0.75f, 0.35f, 1.0f, 1.0f), "INSIDE");
                else
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.45f, 1.0f), "OUTSIDE");

                ImGui::TableSetColumnIndex(2);
                if (tracked.HasAmanamuMonsterMod)
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.45f, 1.0f), "YES");
                else
                    ImGui::TextDisabled("NO");

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.0f", tracked.Distance);

                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.1fs", now - tracked.LastSeenSeconds);

                ImGui::TableSetColumnIndex(5);
                ImGui::TextWrapped("%s", tracked.Path.c_str());

                ImGui::TableSetColumnIndex(6);

                if (!tracked.LastMonsterMods.empty())
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1.0f), "MonsterMods:");
                    for (const std::string& modLine : tracked.LastMonsterMods)
                    {
                        if (ContainsInsensitive(modLine, "Abyss") ||
                            ContainsInsensitive(modLine, "Lightless") ||
                            ContainsInsensitive(modLine, "Amanamu") ||
                            ContainsInsensitive(modLine, "MonsterAbyss") ||
                            ContainsInsensitive(modLine, "0xBFDA2A36") ||
                            ContainsInsensitive(modLine, "0x63D1"))
                        {
                            ImGui::TextWrapped("%s", modLine.c_str());
                        }
                    }
                }

                if (!tracked.LastBuffs.empty())
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 1.0f, 1.0f), "Buffs:");
                    for (const std::string& buff : tracked.LastBuffs)
                    {
                        if (ContainsInsensitive(buff, "abyss") ||
                            ContainsInsensitive(buff, "lightless") ||
                            ContainsInsensitive(buff, "well"))
                        {
                            ImGui::TextWrapped("%s", buff.c_str());
                        }
                    }
                }

                if (tracked.LastMonsterMods.empty() && tracked.LastBuffs.empty())
                    ImGui::TextDisabled("-");
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

private:
    bool m_EnableOverlay = true;
    bool m_ShowDebugWindow = true;
    bool m_DrawOnScreenLabels = true;
    bool m_DrawOffscreenArrows = true;
    bool m_DrawEdgeArrowForOnScreenMonsters = true;
    bool m_DrawCircle = true;
    bool m_OnlyRareOrUnique = true;
    bool m_LogNewDetections = true;

    float m_MaxDistance = 3500.0f;
    float m_ForgetAfterSeconds = 8.0f;
    float m_LabelYOffset = 70.0f;
    float m_CircleRadius = 34.0f;

    std::chrono::steady_clock::time_point m_EnableTime{};
    std::unordered_map<uint32_t, TrackedMonster> m_Tracked;
};

// ============================================================================
// SDK v6 factory exports
// ============================================================================

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin()
{
    return new AmanamuVoidPlugin();
}

extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* plugin)
{
    delete plugin;
}