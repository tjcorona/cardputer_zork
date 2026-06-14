#include <M5Cardputer.h>
#include <SD.h>
#include "zmachine.hpp"
#include "ui.hpp"

UI ui;
ZMachine zm;
String inputBuf;

void setup() {
    // 1. Core configuration boot
    M5Cardputer.begin(M5.config(), true);
    
    // 2. Immediate raw hardware screen wake-up override
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(BLACK);
    M5Cardputer.Display.setTextColor(WHITE, BLACK);
    M5Cardputer.Display.setTextSize(1);
    
    M5Cardputer.Display.setCursor(0, 10);
    M5Cardputer.Display.println("[DBG] Hardware initialized.");

    // 3. Start the UI subsystem
    M5Cardputer.Display.setCursor(0, 25);
    M5Cardputer.Display.println("[DBG] Initializing UI layer...");
    ui.begin();
    M5Cardputer.Display.println("[DBG] UI layer OK.");

    // 4. M5Launcher Safe SD Card Hook using explicit ESP32-S3 hardware pins
    M5Cardputer.Display.println("[DBG] Checking for active SD card mount...");
    bool sd_ok = false;

    // Hardcoded pin definitions for Cardputer SD Slot matrix
    const int sd_sck  = 40;
    const int sd_miso = 39;
    const int sd_mosi = 14;
    const int sd_cs   = 12;

    // Check if the SD card filesystem is already running from M5Launcher
    if (SD.cardType() != CARD_NONE) {
        M5Cardputer.Display.println("[DBG] SD card already active from M5Launcher.");
        sd_ok = true;
    } else {
        M5Cardputer.Display.println("[DBG] Native SD init required. Starting SPI...");
        // Explicitly start the SPI bus using the correct hardware assignments
        SPI.begin(sd_sck, sd_miso, sd_mosi, sd_cs);
        // Mount the filesystem on CS Pin 12 at standard 25MHz frequency
        sd_ok = SD.begin(sd_cs, SPI, 25000000);
    }

    if (!sd_ok) {
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.println("[ERR] SD Card could not be mounted!");
        return;
    }
    M5Cardputer.Display.println("[DBG] SD Card verification passed.");

    // 5. Try to mount and read the ZORK1 file from disk
    M5Cardputer.Display.println("[DBG] Attempting zm.load(/ZORK1.Z3)...");
    if (!zm.load("/ZORK1.Z3", ui)) {
        M5Cardputer.Display.setTextColor(RED, BLACK);
        M5Cardputer.Display.println("[ERR] ZORK1.Z3 file load failed!");
        return;
    }
    M5Cardputer.Display.println("[DBG] Game binary loaded into memory vector.");

    // 6. Final setup checkpoint
    M5Cardputer.Display.setTextColor(GREEN, BLACK);
    M5Cardputer.Display.println("[SUCCESS] Setup complete. Entering loop...");
    delay(2000); // 2 second pause to read the successful load lines!
}

void loop() {
    M5Cardputer.update();

    // Run the engine. The core dump tracker will lock the screen at step 5000.
    zm.run(); 

    ui.render();
    delay(10);
}