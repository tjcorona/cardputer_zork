#include "ui.hpp"
#include <M5Cardputer.h>

void UI::begin() {
    // Crucial step: Initialize text boundaries, orientation, and color
    M5Cardputer.Display.setRotation(1); 
    M5Cardputer.Display.setTextColor(GREEN, BLACK); // Green on Black for retro terminal vibe
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(BLACK); // Force-wake physical pixel states
        M5Cardputer.Display.clear();
}

void UI::wrapAndPush(const std::string& line) {
    const int maxWidth = 40; // The Cardputer screen can fit ~40 small characters horizontally

    std::string temp = line;

    while (temp.length() > maxWidth) {
        buffer.push_back(temp.substr(0, maxWidth));
        temp = temp.substr(maxWidth);
    }

    buffer.push_back(temp);

    while (buffer.size() > maxLines) {
        buffer.erase(buffer.begin());
    }
}

void UI::print(const std::string& text) {
    currentLine += text;
}

void UI::println(const std::string& text) {
    wrapAndPush(currentLine + text);
    currentLine = "";
    render(); // Force clear-redraw instantly on printing line completions
}

void UI::setStatus(const std::string& left, const std::string& right) {
    statusLeft = left;
    statusRight = right;
}

void UI::render() {
    // 1. Render Status Bar (Row 0)
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.setTextColor(BLACK, WHITE); // Inverted look
    M5Cardputer.Display.print(statusLeft.c_str());

    int x = 240 - (statusRight.length() * 6);
    if (x < 0) x = 120;
    M5Cardputer.Display.setCursor(x, 0);
    M5Cardputer.Display.print(statusRight.c_str());

    // Switch back to green text on black background for standard terminal lines
    M5Cardputer.Display.setTextColor(GREEN, BLACK);

    // 2. Render Text Scroll Area
    int y = 14; 
    int lines_printed = 0; // 🟢 FIX: Defined cleanly right before the loop blocks

    for (const auto& line : buffer) {
        // 🟢 SAFETY OVERRIDE: The Cardputer screen can only fit 11 lines of text 
        // before colliding with the input tray. Clamp the loop immediately!
        if (lines_printed >= 11) break; 

        M5Cardputer.Display.setCursor(0, y);
        M5Cardputer.Display.setTextColor(GREEN, BLACK); 
        M5Cardputer.Display.print(line.c_str());
        
        y += 10;
        lines_printed++;
    }

    // 3. Render Input Prompt Area (Fixed Bottom)
    M5Cardputer.Display.setCursor(0, 124); 
    M5Cardputer.Display.print("> ");
    M5Cardputer.Display.print(inputLine.c_str());
}

std::string UI::getInput() {
    // Non-blocking single pass collection check to keep system ticking
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange()) { // Poll physical matrix changes safely
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

            // Handle return carriage confirmation flag explicitly
            if (status.enter) {
                std::string out = inputLine;
                inputLine = "";
                return out;
            }

            // Handle backspace removal vector shifts safely
            if (status.del && !inputLine.empty()) {
                inputLine.pop_back();
                render();
            }

            // Process text payload collection queues
            for (auto c : status.word) {
                if (c >= 32 && c <= 126) { // Enforce visible ASCII strings characters only
                    inputLine += c;
                    render();
                }
            }
        }
    }

    return ""; // Return empty if string payload is incomplete
}
