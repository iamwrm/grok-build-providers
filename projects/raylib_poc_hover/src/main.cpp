#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kWindowWidth = 1440;
constexpr int kWindowHeight = 900;
constexpr float kDesignWidth = 1100.0f;
constexpr float kMinDesignWidth = 1040.0f;
constexpr float kMaxDesignWidth = 1280.0f;
constexpr float kDesignHeight = 720.0f;
constexpr float kHorizontalMargin = 70.0f;
constexpr float kFontSize = 20.0f;
constexpr float kTextSpacing = 0.0f;
constexpr float kLineHeight = 30.0f;
constexpr float kChipPaddingX = 12.0f;
constexpr float kChipHeight = 30.0f;
constexpr double kDoubleClickSeconds = 0.35;
constexpr int kPasteChipMinLines = 4;
constexpr std::size_t kPasteChipDisplayBytes = 10'000;
constexpr int kPreviewVisibleLines = 7;
constexpr int kPreviewWheelStep = 3;

const Color kBackground = {14, 16, 22, 255};
const Color kPanel = {21, 24, 32, 255};
const Color kPanelRaised = {27, 31, 41, 255};
const Color kBorder = {58, 65, 82, 255};
const Color kText = {224, 228, 238, 255};
const Color kMuted = {133, 142, 162, 255};
const Color kAccent = {105, 213, 180, 255};
const Color kChip = {37, 67, 65, 255};
const Color kChipHover = {48, 91, 85, 255};
const Color kPreviewBackground = {20, 38, 38, 255};
const Color kPreviewBorder = {75, 139, 128, 255};

float gUiScale = 1.0f;
float gUiDesignWidth = kDesignWidth;
float gUiScreenWidth = 0.0f;
float gUiScreenHeight = 0.0f;
Vector2 gUiOrigin{};

void UpdateUiMetrics() {
    const float reportedWidth = static_cast<float>(std::max(GetScreenWidth(), 1));
    const float reportedHeight = static_cast<float>(std::max(GetScreenHeight(), 1));

    if (gUiScreenWidth == 0.0f || IsWindowResized()) {
        gUiScreenWidth = reportedWidth;
        gUiScreenHeight = reportedHeight;

#if defined(_WIN32)
        // raylib 5.5 can report framebuffer pixels as the screen size after a
        // high-DPI Windows resize. Convert them back to the logical coordinates
        // used by drawing and mouse input when that mismatch is detected.
        const Vector2 dpiScale = GetWindowScaleDPI();
        const bool reportsFramebufferSize =
            dpiScale.x > 1.0f && std::abs(GetRenderWidth() - GetScreenWidth()) <= 1;
        if (IsWindowResized() && reportsFramebufferSize) {
            gUiScreenWidth /= dpiScale.x;
            gUiScreenHeight /= dpiScale.y;
        }
#endif
    }

    // Scale for height and minimum readable width, then let wider windows add usable
    // horizontal space instead of growing the entire interface indefinitely.
    gUiScale =
        std::min(gUiScreenWidth / kMinDesignWidth, gUiScreenHeight / kDesignHeight);
    gUiDesignWidth =
        std::clamp(gUiScreenWidth / gUiScale, kMinDesignWidth, kMaxDesignWidth);
    gUiOrigin = {(gUiScreenWidth - gUiDesignWidth * gUiScale) * 0.5f,
                 (gUiScreenHeight - kDesignHeight * gUiScale) * 0.5f};
}

float Ui(float value) {
    return value * gUiScale;
}

Vector2 UiPoint(float x, float y) {
    return {gUiOrigin.x + Ui(x), gUiOrigin.y + Ui(y)};
}

Rectangle UiRectangle(float x, float y, float width, float height) {
    const Vector2 point = UiPoint(x, y);
    return {point.x, point.y, Ui(width), Ui(height)};
}

struct Segment {
    enum class Kind { Text, Paste };

    Kind kind = Kind::Text;
    std::string content;
    Rectangle bounds{};
    int lineCount = 1;
};

int CountLines(const std::string& text) {
    if (text.empty()) return 0;
    return 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

std::string NormalizePaste(std::string text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            result.push_back('\n');
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

std::string ChipLabel(const Segment& segment) {
    return "[Pasted: " + std::to_string(segment.lineCount) +
           (segment.lineCount == 1 ? " line]" : " lines]");
}

std::string TruncateToWidth(Font font, const std::string& text, float maxWidth) {
    if (MeasureTextEx(font, text.c_str(), Ui(kFontSize), kTextSpacing).x <= maxWidth) return text;

    const std::string suffix = "...";
    std::string result;
    for (char character : text) {
        std::string candidate = result + character + suffix;
        if (MeasureTextEx(font, candidate.c_str(), Ui(kFontSize), kTextSpacing).x > maxWidth) break;
        result.push_back(character);
    }
    return result + suffix;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

void AppendText(std::vector<Segment>& segments, const std::string& text) {
    if (text.empty()) return;
    if (!segments.empty() && segments.back().kind == Segment::Kind::Text) {
        segments.back().content += text;
    } else {
        segments.push_back({Segment::Kind::Text, text});
    }
}

void AppendPaste(std::vector<Segment>& segments, const std::string& clipboardText) {
    const std::string text = NormalizePaste(clipboardText);
    if (text.empty()) return;

    const int lines = CountLines(text);
    if (lines < kPasteChipMinLines && text.size() <= kPasteChipDisplayBytes) {
        AppendText(segments, text);
        return;
    }

    Segment paste;
    paste.kind = Segment::Kind::Paste;
    paste.content = text;
    paste.lineCount = lines;
    segments.push_back(std::move(paste));
}

void RemoveLastInput(std::vector<Segment>& segments) {
    if (segments.empty()) return;
    Segment& last = segments.back();
    if (last.kind == Segment::Kind::Paste || last.content.empty()) {
        segments.pop_back();
        return;
    }

    std::size_t eraseAt = last.content.size() - 1;
    while (eraseAt > 0 &&
           (static_cast<unsigned char>(last.content[eraseAt]) & 0xC0U) == 0x80U) {
        --eraseAt;
    }
    last.content.erase(eraseAt);
    if (last.content.empty()) segments.pop_back();
}

void DrawLabel(Font font, const char* text, Vector2 position, Color color,
               float designSize = kFontSize) {
    DrawTextEx(font, text, position, Ui(designSize), kTextSpacing, color);
}

float DrawTextSegment(Font font, Segment& segment, Vector2& cursor,
                      const Rectangle& contentArea, bool draw) {
    const float lineHeight = Ui(kLineHeight);
    const float startY = cursor.y;
    segment.bounds = {cursor.x, cursor.y, 0.0f, lineHeight};

    std::string run;
    auto flushRun = [&]() {
        if (run.empty()) return;
        if (draw) DrawLabel(font, run.c_str(), cursor, kText);
        const float width = MeasureTextEx(font, run.c_str(), Ui(kFontSize), kTextSpacing).x;
        cursor.x += width;
        segment.bounds.width = std::max(segment.bounds.width, cursor.x - segment.bounds.x);
        run.clear();
    };

    for (char character : segment.content) {
        if (character == '\n') {
            flushRun();
            cursor.x = contentArea.x;
            cursor.y += lineHeight;
            continue;
        }

        std::string candidate = run + character;
        const float candidateWidth =
            MeasureTextEx(font, candidate.c_str(), Ui(kFontSize), kTextSpacing).x;
        if (cursor.x + candidateWidth > contentArea.x + contentArea.width && !run.empty()) {
            flushRun();
            cursor.x = contentArea.x;
            cursor.y += lineHeight;
        }
        run.push_back(character);
    }
    flushRun();
    segment.bounds.height = cursor.y - startY + lineHeight;
    return cursor.y;
}

void DrawChip(Font font, Segment& segment, Vector2& cursor, const Rectangle& contentArea,
              bool hovered, bool draw) {
    const std::string label = ChipLabel(segment);
    const float textWidth = MeasureTextEx(font, label.c_str(), Ui(kFontSize), kTextSpacing).x;
    const float chipWidth = textWidth + Ui(kChipPaddingX * 2.0f);

    if (cursor.x + chipWidth > contentArea.x + contentArea.width && cursor.x > contentArea.x) {
        cursor.x = contentArea.x;
        cursor.y += Ui(kLineHeight + 4.0f);
    }

    segment.bounds = {cursor.x, cursor.y, chipWidth, Ui(kChipHeight)};
    if (draw) {
        DrawRectangleRounded(segment.bounds, 0.35f, 8, hovered ? kChipHover : kChip);
        DrawRectangleRoundedLinesEx(segment.bounds, 0.35f, 8, Ui(1.0f),
                                    hovered ? kAccent : kPreviewBorder);
        DrawLabel(font, label.c_str(),
                  {cursor.x + Ui(kChipPaddingX), cursor.y + Ui(4.0f)},
                  hovered ? RAYWHITE : kAccent, kFontSize - 2.0f);
    }
    cursor.x += chipWidth + Ui(5.0f);
}

Rectangle GetEditorRectangle() {
    return UiRectangle(kHorizontalMargin, 500.0f,
                       gUiDesignWidth - kHorizontalMargin * 2.0f, 145.0f);
}

Rectangle GetEditorContentRectangle(const Rectangle& editor) {
    return {editor.x + Ui(22.0f), editor.y + Ui(26.0f), editor.width - Ui(44.0f),
            editor.height - Ui(45.0f)};
}

void LayoutEditor(Font font, std::vector<Segment>& segments, const Rectangle& editor) {
    const Rectangle content = GetEditorContentRectangle(editor);
    Vector2 cursor = {content.x, content.y};
    for (Segment& segment : segments) {
        if (segment.kind == Segment::Kind::Paste) {
            DrawChip(font, segment, cursor, content, false, false);
        } else {
            DrawTextSegment(font, segment, cursor, content, false);
        }
    }
}

Rectangle DrawEditor(Font font, std::vector<Segment>& segments, int hoveredIndex) {
    const Rectangle editor = GetEditorRectangle();
    const Rectangle content = GetEditorContentRectangle(editor);

    DrawRectangleRounded(editor, 0.08f, 12, kPanel);
    DrawRectangleRoundedLinesEx(editor, 0.08f, 12, Ui(1.5f), kBorder);

    BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                     static_cast<int>(content.width), static_cast<int>(content.height));
    Vector2 cursor = {content.x, content.y};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        Segment& segment = segments[i];
        if (segment.kind == Segment::Kind::Paste) {
            DrawChip(font, segment, cursor, content, static_cast<int>(i) == hoveredIndex,
                     true);
        } else {
            DrawTextSegment(font, segment, cursor, content, true);
        }
    }

    if (segments.empty()) {
        DrawLabel(font, "Paste multiline text here...", cursor, kMuted);
    } else if (static_cast<int>(GetTime() * 2.0) % 2 == 0) {
        DrawRectangle(static_cast<int>(cursor.x + Ui(2.0f)),
                      static_cast<int>(cursor.y + Ui(3.0f)),
                      std::max(2, static_cast<int>(Ui(2.0f))),
                      static_cast<int>(Ui(kFontSize + 1.0f)), kAccent);
    }
    EndScissorMode();

    DrawLabel(font, "PROMPT", {editor.x + Ui(18.0f), editor.y - Ui(10.0f)}, kMuted, 14.0f);
    return editor;
}

void DrawPreview(Font font, const Segment& paste, const Rectangle& editor, int scrollLine) {
    const std::vector<std::string> lines = SplitLines(paste.content);
    if (lines.empty()) return;

    const int totalLines = static_cast<int>(lines.size());
    const int visibleLines = std::min(kPreviewVisibleLines, totalLines);
    const int maxScroll = std::max(0, totalLines - visibleLines);
    scrollLine = std::clamp(scrollLine, 0, maxScroll);

    const float boxWidth = editor.width * 0.75f;
    const float boxHeight = Ui(58.0f + static_cast<float>(visibleLines) * 25.0f);
    const Rectangle box = {editor.x + (editor.width - boxWidth) / 2.0f,
                           editor.y - boxHeight - Ui(18.0f), boxWidth, boxHeight};

    DrawRectangleRounded(box, 0.06f, 12, kPreviewBackground);
    DrawRectangleRoundedLinesEx(box, 0.06f, 12, Ui(2.0f), kPreviewBorder);

    const int lastVisibleLine = scrollLine + visibleLines;
    const std::string title = "PASTE PREVIEW  |  lines " + std::to_string(scrollLine + 1) +
                              "-" + std::to_string(lastVisibleLine) + " of " +
                              std::to_string(totalLines);
    DrawLabel(font, title.c_str(), {box.x + Ui(18.0f), box.y + Ui(13.0f)}, kAccent, 14.0f);

    const Rectangle clip = {box.x + Ui(18.0f), box.y + Ui(39.0f), box.width - Ui(52.0f),
                            box.height - Ui(62.0f)};
    BeginScissorMode(static_cast<int>(clip.x), static_cast<int>(clip.y),
                     static_cast<int>(clip.width), static_cast<int>(clip.height));
    float y = clip.y;
    for (int index = scrollLine; index < lastVisibleLine; ++index) {
        const std::string visible = TruncateToWidth(font, lines[index], clip.width);
        DrawLabel(font, visible.c_str(), {clip.x, y}, kText, kFontSize - 2.0f);
        y += Ui(25.0f);
    }
    EndScissorMode();

    if (maxScroll > 0) {
        const Rectangle track = {box.x + box.width - Ui(15.0f), clip.y, Ui(4.0f), clip.height};
        DrawRectangleRounded(track, 1.0f, 4, kChip);
        const float thumbHeight =
            std::max(Ui(20.0f), track.height * visibleLines / static_cast<float>(totalLines));
        const float progress = scrollLine / static_cast<float>(maxScroll);
        const Rectangle thumb = {track.x, track.y + (track.height - thumbHeight) * progress,
                                 track.width, thumbHeight};
        DrawRectangleRounded(thumb, 1.0f, 4, kAccent);
    }

    const char* hint = "mouse wheel to scroll  |  double-click to expand";
    const float hintWidth = MeasureTextEx(font, hint, Ui(14.0f), kTextSpacing).x;
    DrawRectangle(static_cast<int>(box.x + (box.width - hintWidth) / 2.0f - Ui(8.0f)),
                  static_cast<int>(box.y + box.height - Ui(9.0f)),
                  static_cast<int>(hintWidth + Ui(16.0f)), static_cast<int>(Ui(18.0f)),
                  kPreviewBackground);
    DrawLabel(font, hint,
              {box.x + (box.width - hintWidth) / 2.0f, box.y + box.height - Ui(8.0f)},
              kMuted, 14.0f);
}

std::vector<Segment> DemoContent() {
    std::vector<Segment> segments;
    AppendText(segments, "Please review this code: ");
    AppendPaste(segments,
                "class PasteChip {\n"
                "public:\n"
                "    void preserveRawText();\n"
                "    void renderSummary();\n"
                "    void hitTestMouse();\n"
                "    void showPreviewOverlay();\n"
                "    void expandOnDoubleClick();\n"
                "};\n"
                "// The model still receives every original line.");
    AppendText(segments, " and explain the interaction.");
    return segments;
}

}  // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT |
                   FLAG_MSAA_4X_HINT);
    InitWindow(kWindowWidth, kWindowHeight, "raylib - Grok paste chip hover POC");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(800, 560);
    SetTargetFPS(60);

    const std::string fontPath =
        std::string(GetApplicationDirectory()) + "assets/fonts/FiraCode.ttf";
    Font font = LoadFontEx(fontPath.c_str(), 96, nullptr, 0);
    const bool ownsFont = FileExists(fontPath.c_str()) && font.texture.id != 0;
    if (!ownsFont) font = GetFontDefault();
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    std::vector<Segment> segments = DemoContent();
    int hoveredIndex = -1;
    int previousHoveredIndex = -1;
    int previewScrollLine = 0;
    float previewWheelRemainder = 0.0f;
    int lastClickedIndex = -1;
    double lastClickTime = -1.0;
    Rectangle editor{};

    while (!WindowShouldClose()) {
        UpdateUiMetrics();
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                          IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);

        if (ctrl && IsKeyPressed(KEY_V)) {
            const char* clipboard = GetClipboardText();
            if (clipboard != nullptr) AppendPaste(segments, clipboard);
        }
        if (IsKeyPressed(KEY_BACKSPACE)) RemoveLastInput(segments);
        if (IsKeyPressed(KEY_ESCAPE)) segments.clear();
        if (ctrl && IsKeyPressed(KEY_R)) segments = DemoContent();

        if (!ctrl) {
            for (int codepoint = GetCharPressed(); codepoint > 0; codepoint = GetCharPressed()) {
                if (codepoint >= 32 && codepoint <= 126) {
                    AppendText(segments, std::string(1, static_cast<char>(codepoint)));
                }
            }
        }

        // Recompute hit rectangles before input handling so resizing never leaves the
        // mouse interacting with bounds from the previous frame.
        editor = GetEditorRectangle();
        LayoutEditor(font, segments, editor);

        const Vector2 mouse = GetMousePosition();
        hoveredIndex = -1;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            if (segments[i].kind == Segment::Kind::Paste &&
                CheckCollisionPointRec(mouse, segments[i].bounds)) {
                hoveredIndex = static_cast<int>(i);
                break;
            }
        }

        if (hoveredIndex != previousHoveredIndex) {
            previewScrollLine = 0;
            previewWheelRemainder = 0.0f;
        }
        if (hoveredIndex >= 0) {
            const int totalLines = static_cast<int>(SplitLines(segments[hoveredIndex].content).size());
            const int maxScroll = std::max(0, totalLines - kPreviewVisibleLines);
            previewWheelRemainder += GetMouseWheelMove();
            const int wheelSteps = static_cast<int>(previewWheelRemainder);
            previewWheelRemainder -= static_cast<float>(wheelSteps);
            previewScrollLine = std::clamp(
                previewScrollLine - wheelSteps * kPreviewWheelStep, 0, maxScroll);
        }
        previousHoveredIndex = hoveredIndex;

        if (hoveredIndex >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            const double now = GetTime();
            if (lastClickedIndex == hoveredIndex && now - lastClickTime <= kDoubleClickSeconds) {
                segments[hoveredIndex].kind = Segment::Kind::Text;
                lastClickedIndex = -1;
                hoveredIndex = -1;
            } else {
                lastClickedIndex = hoveredIndex;
                lastClickTime = now;
            }
        }

        BeginDrawing();
        ClearBackground(kBackground);

        DrawLabel(font, "Atomic paste chips", UiPoint(70.0f, 54.0f), kText, 32.0f);
        DrawLabel(font,
                  "Raw multiline text stays in the model, while the editor renders one compact token.",
                  UiPoint(70.0f, 98.0f), kMuted, 19.0f);

        const Vector2 dpiScale = GetWindowScaleDPI();
        const std::string scaleLabel =
            "DPI " + std::to_string(static_cast<int>(std::round(dpiScale.x * 100.0f))) +
            "%  |  UI " + std::to_string(static_cast<int>(std::round(gUiScale * 100.0f))) + "%";
        const float scaleLabelWidth =
            MeasureTextEx(font, scaleLabel.c_str(), Ui(14.0f), kTextSpacing).x;
        const Vector2 scaleAnchor = UiPoint(gUiDesignWidth - kHorizontalMargin, 64.0f);
        DrawLabel(font, scaleLabel.c_str(), {scaleAnchor.x - scaleLabelWidth, scaleAnchor.y},
                  kAccent, 14.0f);

        const Rectangle explanation =
            UiRectangle(kHorizontalMargin, 148.0f,
                        gUiDesignWidth - kHorizontalMargin * 2.0f, 92.0f);
        DrawRectangleRounded(explanation, 0.08f, 10, kPanelRaised);
        DrawLabel(font, "1  Paste multiline text -> store raw content + render a summary chip",
                  UiPoint(92.0f, 169.0f), kText, 18.0f);
        DrawLabel(font, "2  Hit-test the chip rectangle on every mouse move -> draw preview overlay",
                  UiPoint(92.0f, 202.0f), kText, 18.0f);

        editor = DrawEditor(font, segments, hoveredIndex);
        if (hoveredIndex >= 0) {
            DrawPreview(font, segments[hoveredIndex], editor, previewScrollLine);
        }

        DrawLabel(font, "Ctrl/Cmd+V paste   |   hover + wheel preview   |   double-click expand   |   Esc clear   |   Ctrl+R reset",
                  UiPoint(70.0f, 674.0f), kMuted, 14.0f);
        EndDrawing();
    }

    if (ownsFont) UnloadFont(font);
    CloseWindow();
    return 0;
}
