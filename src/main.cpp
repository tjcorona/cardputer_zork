#ifdef NATIVE_DESKTOP
    #include <iostream>
    #include <string>
    #include <chrono>
    #include <thread>
#else
    #include <Arduino.h>
    #include <M5Cardputer.h>
    #include <SD.h>
#endif

#include "zmachine.hpp"
#include "ui.hpp"

UI ui;
ZMachine zm;

void printLog(const char* message) {
    #ifdef NATIVE_DESKTOP
        std::cout << message << std::endl;
    #else
        Serial.println(message);
    #endif
}

void setup() {
    // 1. Core Boot Separation Channel
    #ifdef NATIVE_DESKTOP
        std::cout << "\n=============================================" << std::endl;
        std::cout << "[NATIVE TARGET] Booting Mac Desktop Testbed..." << std::endl;
        std::cout << "=============================================\n" << std::endl;
        ui.begin();
    #else
        // Physical Cardputer Hardware Initialization Path
        M5Cardputer.begin(M5.config(), true);
        Serial.begin(115200);

        M5Cardputer.Display.setRotation(1);
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(WHITE, BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(0, 10);
        M5Cardputer.Display.println("[DBG] Hardware initialized.");

        M5Cardputer.Display.setCursor(0, 25);
        M5Cardputer.Display.println("[DBG] Initializing UI layer...");
        ui.begin();
        M5Cardputer.Display.println("[DBG] UI layer OK.");
    #endif

    // 2. Storage System Mount Mapping
    printLog("[DBG] Checking for active storage layer...");
    bool storage_ok = false;

    #ifdef NATIVE_DESKTOP
        // Local desktop files require no manual filesystem partition mounting steps
        storage_ok = true;
    #else
        const int sd_sck  = 40;
        const int sd_miso = 39;
        const int sd_mosi = 14;
        const int sd_cs   = 12;

        if (SD.cardType() != CARD_NONE) {
            M5Cardputer.Display.println("[DBG] SD card already active from M5Launcher.");
            storage_ok = true;
        } else {
            M5Cardputer.Display.println("[DBG] Native SD init required. Starting SPI...");
            SPI.begin(sd_sck, sd_miso, sd_mosi, sd_cs);
            storage_ok = SD.begin(sd_cs, SPI, 25000000);
        }
    #endif

    if (!storage_ok) {
        printLog("[ERR] Storage layer could not be verified!");
        return;
    }
    printLog("[DBG] Storage system verification passed successfully.");

    // 3. Mount and read the ZORK1 engine file from disk
    printLog("[DBG] Attempting zm.load...");

    #ifdef NATIVE_DESKTOP
        // Pulls out of your local project repository directory's data/ folder
        bool load_success = zm.load("data/ZORK1.Z3", ui);
    #else
        // Pulls out of the physical microSD card root directory
        bool load_success = zm.load("/ZORK1.Z3", ui);
    #endif

    if (!load_success) {
        printLog("[ERR] ZORK1.Z3 file load failed!");
        return;
    }
    printLog("[DBG] Game binary loaded into memory vector.");
    printLog("[SUCCESS] Setup complete. Entering main engine game loop...\n");
}

void loop() {
      printLog("[DBG] in loop...\n");

    #ifndef NATIVE_DESKTOP
        M5Cardputer.update();
    #endif

      printLog("[DBG] querying needsInput...\n");
    if (zm.needsInput()) {
        #ifdef NATIVE_DESKTOP
      printLog("[DBG] in needsInput, calling render...\n");
            ui.render();

            // In a native terminal environment, force a blocking keyboard check
            std::string typed_input = ui.getInput();
            // Pass the string straight down into the emulator memory core registers
            zm.provideInput(typed_input);
        #else
            // On physical hardware, process the matrix keys asynchronously
            std::string typed_input = ui.getInput();
            if (!typed_input.empty()) {
                zm.provideInput(typed_input);
            }
        #endif
    } else {
        // Run Zork instructions until the engine requests the next command line string
        printLog("[DBG] calling run...\n");
        zm.run();
        printLog("[DBG] called run...\n");
    }

    #ifndef NATIVE_DESKTOP
        ui.render();
        delay(10);
    #else
        // Sleep cleanly for 10ms on desktop to keep host CPU usage minimal
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    #endif
}

// Native entry point for compiling execution files on macOS architecture
#ifdef NATIVE_DESKTOP
int main() {
    setup();
    while(true) {
        loop();
    }
    return 0;
}
#endif
