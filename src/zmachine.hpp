#pragma once
#include <stdint.h>
#include <vector>
#include <string>

#include "ui.hpp"

class ZMachine {
public:
    // Core Execution Control
    bool load(const char* path, UI& uiRef);
    void reset();
    void run();

    // UI and IO Hooks
    bool needsInput();
    void provideInput(const std::string& input);
    void updateStatus(UI& ui);

    // Dictionary and Text Tokenization
    std::string decodeDictWord(uint32_t entry_addr);
    void encodeWord(const std::string& word, uint16_t& out_w1, uint16_t& out_w2);
    uint16_t findWord(const std::string& word);
    void tokenize(uint16_t textBuf, uint16_t parseBuf);

    // Z-Machine Object Table Engine (Version 3 Specs)
    uint16_t getObjectAddr(uint16_t obj);
    uint8_t getAttr(uint16_t obj, int attr);
    void setAttr(uint16_t obj, int attr);
    void clearAttr(uint16_t obj, int attr);

    uint16_t getParent(uint16_t obj);
    uint16_t getSibling(uint16_t obj);
    uint16_t getChild(uint16_t obj);

    void insertObj(uint16_t obj, uint16_t dest);
    void removeObj(uint16_t obj);

    std::string getObjectName(uint16_t obj);
    uint16_t getGlobal(uint8_t index);

private:
    // Virtual Memory Space
    std::vector<uint8_t> mem;

    // Registers and Stack pointer
    uint32_t pc;               // 🟢 32-bit Program Counter
    uint16_t stack[1024];
    int sp = 0;

    struct CallFrame {
        uint32_t return_pc;    // 🟢 32-bit Return Address
        uint8_t store_var;
        uint16_t locals[15];
        uint8_t num_locals;
        int saved_sp;
    };
    CallFrame frames[32];   // Maximum nesting depth for V3 routines
    int fp = 0;             // Frame Pointer index tracker

    // 🟢 TRACE HISTORY ARRAYS
    uint32_t hist_pc[32] = {0};
    uint8_t hist_op[32] = {0};
    uint8_t hist_cls[32] = {0};
    uint8_t hist_num[32] = {0};
    int hist_idx = 0;
    uint32_t total_executed_ops = 0;

    uint32_t last_pc = 0;      // 🟢 UPGRADED: 32-bit tracker to cleanly match pc register
    int loop_counter = 0;

    bool waiting_input = false;

    uint32_t cached_text_buffer = 0;
    uint32_t cached_parse_buffer = 0;

    UI* ui = nullptr;

    // V3 Dynamic Header Offsets
    uint32_t globals;          // 🟢 32-bit Pointer Table Address
    uint32_t dictionary;       // 🟢 32-bit Pointer Table Address
    uint32_t obj_table;        // 🟢 32-bit Pointer Table Address

    // Memory Access Subs (Big Endian)
    uint8_t read8(uint32_t a);   // 🟢 UPGRADED: Accepts 32-bit address
    uint16_t read16(uint32_t a); // 🟢 UPGRADED: Accepts 32-bit address
    void write8(uint32_t a, uint8_t v);   // 🟢 UPGRADED: Accepts 32-bit address
    void write16(uint32_t a, uint16_t v); // 🟢 UPGRADED: Accepts 32-bit address

    // Processing Subsystem
    void step();
    void decode();
    uint16_t getVar(uint8_t var);
    void setVar(uint8_t var, uint16_t val);

    // Stack Controls
    void push(uint16_t v);
    uint16_t pop();

    // Logic Helpers
    void branch(bool cond);
    void call(uint16_t addr, uint8_t argc, uint16_t* args);
    void ret(uint16_t val);

    uint32_t unpackAddress(uint16_t packed_addr);

    // Z-String Processing
    std::string printZString(uint16_t addr);

    uint16_t getPropertyDataAddr(uint16_t obj, uint8_t prop);
};
