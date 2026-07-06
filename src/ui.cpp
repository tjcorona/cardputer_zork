#include "ui.hpp"

#ifdef NATIVE_DESKTOP
    #include <iostream>
    #include <vector>
#else
    #include <M5Cardputer.h>
#endif

void UI::begin() {
    #ifdef NATIVE_DESKTOP
        // Clear terminal screen and reset cursor (ANSI escape codes)
        std::cout << "\033[2J\033[H" << std::endl;
        std::cout << "[UI] Native Terminal Interface Loaded." << std::endl;
    #else
        // Crucial step: Initialize text boundaries, orientation, and color
        M5Cardputer.Display.setRotation(1);
        M5Cardputer.Display.setTextColor(GREEN, BLACK); // Green on Black for retro terminal vibe
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.fillScreen(BLACK); // Force-wake physical pixel states
        M5Cardputer.Display.clear();
    #endif
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
    #ifdef NATIVE_DESKTOP
        // 1. Render Status Bar inside terminal window
        std::cout << "\n=== STATUS | " << statusLeft << " | " << statusRight << " ===" << std::endl;

        // 2. Render Text Scroll Area
        for (const auto& line : buffer) {
            std::cout << line << std::endl;
        }

        // 3. Render Input Prompt Line
        std::cout << "> " << inputLine;
        std::cout.flush();
    #else
        // 1. Render Status Bar (Row 0) on hardware screen
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
        int lines_printed = 0;

        for (const auto& line : buffer) {
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
    #endif
}

std::string UI::getInput() {
    #ifdef NATIVE_DESKTOP
        // 🟢 FIX: Clear any stream error flags (like EOF or fail bits)
        // that might have been tripped by background compilation hooks
        std::cin.clear();

        std::string out;

        // Block and fetch the input line from the user keyboard string track
        if (std::getline(std::cin, out)) {
            return out;
        }

        return "";
    #else
        // Non-blocking single pass collection check to keep hardware system ticking
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
    #endif
}
