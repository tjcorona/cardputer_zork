#include "zmachine.hpp"

#ifdef NATIVE_DESKTOP
    #include <iostream>
    #include <fstream>
    #include <vector>
    #include <chrono>
    #include <thread>
#else
    #include <SD.h>
    #include <M5Cardputer.h>
#endif

namespace {
void printLog(const char* format, ...) {
    char buffer[256]; // Allocates a temporary staging zone for your formatted string

    va_list args;
    va_start(args, format);
    // Safely parse the variadic sequence into our text buffer array
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    #ifdef NATIVE_DESKTOP
        std::cout << buffer << std::endl;
    #else
        Serial.println(buffer);
    #endif
}
}

bool ZMachine::load(const char* path, UI& uiRef) {
    ui = &uiRef;

    #ifdef NATIVE_DESKTOP
        if (ui != nullptr) ui->println("[DBG] Target identified: NATIVE_DESKTOP. Attempting file read...");

        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            if (ui != nullptr) ui->println("[DBG] File open failed: Path invalid or unreadable.");
            return false;
        }

        mem.assign((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
        file.close();

        if (mem.empty()) {
            if (ui != nullptr) ui->println("[DBG] File assign failed: Stream evaluated to empty vector.");
            return false;
        }

        if (ui != nullptr) {
            ui->println("[DBG] Vector assignment success. Size: " + std::to_string(mem.size()) + " bytes.");
            ui->println("\n=== [CORE MEMORY VERIFICATION] ===");
            char header_check[128];
            snprintf(header_check, sizeof(header_check),
                     "Byte 0 (Version): %d | Bytes 2-3 (Release): 0x%04X | Bytes 24-25 (Dict): 0x%04X",
                     mem[0], (uint16_t)((mem[2] << 8) | mem[3]), (uint16_t)((mem[0x18] << 8) | mem[0x19]));
            ui->println(std::string(header_check));
            ui->println("===================================\n");
        }

        reset();
        return true;
    #else
        if (ui != nullptr) ui->println("[DBG] Target identified: PHYSICAL HARDWARE. Polling SD reader...");

        File file = SD.open(path, FILE_READ);
        if (!file) {
            if (ui != nullptr) ui->println("[DBG] SD open failed: File handle unallocated.");
            return false;
        }

        size_t size = file.size();
        if (size == 0) {
            if (ui != nullptr) ui->println("[DBG] SD size error: File size evaluates to 0.");
            file.close();
            return false;
        }

        mem.resize(size);
        file.read(mem.data(), size);
        file.close();

        if (ui != nullptr) {
            ui->println("[DBG] SD read success. Vector populated to size: " + std::to_string(mem.size()) + " bytes.");
            ui->println("\n=== [CORE MEMORY VERIFICATION] ===");
            char header_check[128];
            snprintf(header_check, sizeof(header_check),
                     "Byte 0 (Version): %d | Bytes 2-3 (Release): 0x%04X | Bytes 24-25 (Dict): 0x%04X",
                     mem[0], (uint16_t)((mem[2] << 8) | mem[3]), (uint16_t)((mem[0x18] << 8) | mem[0x19]));
            ui->println(std::string(header_check));
            ui->println("===================================\n");
        }

        reset();
        return true;
    #endif
}

// void ZMachine::step() {
//     uint32_t current_pc = pc;
//     uint8_t opcode = read8(pc);

//     hist_pc[hist_idx] = current_pc;
//     hist_op[hist_idx] = opcode;

//     // printLog("[OP] PC: 0x%04X, Opcode: 0x%02X", current_pc, opcode);

//     if ((opcode & 0x80) == 0) {
//         hist_cls[hist_idx] = 0;
//         hist_num[hist_idx] = opcode & 0x1F;
//     } else if ((opcode & 0xC0) == 0x80) {
//         hist_cls[hist_idx] = ((opcode >> 4) & 0x03) == 0x03 ? 2 : 1;
//         hist_num[hist_idx] = opcode & 0x0F;
//     } else {
//         hist_cls[hist_idx] = (opcode & 0x20) ? 3 : 0;
//         hist_num[hist_idx] = opcode & 0x1F;
//     }

//     hist_idx = (hist_idx + 1) % 32;
//     total_executed_ops++;

//     decode();
// }

void ZMachine::step() {
    decode();
}

void ZMachine::reset() {
    sp = 0;
    fp = 0;
    waiting_input = false;

    // 🟢 OFFICIAL SPECIFICATION CONSTANTS FOR VERSION 3
    pc         = read16(0x06); // Initial Program Counter Address
    obj_table  = read16(0x08); // Object Table Base Address (V1-V3 is 0x08)
    globals    = read16(0x0C); // Global Variables Table Base Address
    dictionary = read16(0x18); // Main Dictionary Table Base Address
}

void ZMachine::run() {
    while (!waiting_input) {
        step();
    }
}

bool ZMachine::needsInput() {
    return waiting_input;
}

void ZMachine::provideInput(const std::string& input) {
    waiting_input = false;

    // Direct use of our newly isolated execution addresses
    uint32_t text_buffer  = cached_text_buffer;
    uint32_t parse_buffer = cached_parse_buffer;

    if (text_buffer == 0)  text_buffer = 0x0200;
    if (parse_buffer == 0) parse_buffer = 0x0300;

    uint8_t max_chars = read8(text_buffer);
    if (max_chars == 0) max_chars = 80;

    uint32_t write_ptr = text_buffer + 1;
    uint8_t chars_written = 0;

    for (char c : input) {
        if (chars_written >= max_chars - 1) break;
        char lower_c = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        write8(write_ptr++, lower_c);
        chars_written++;
    }
    write8(write_ptr, 0);

    tokenize(text_buffer, parse_buffer);
}

void ZMachine::updateStatus(UI& ui) {
    uint16_t roomObj = getGlobal(0);
    std::string roomName = getObjectName(roomObj);
    uint16_t score = getGlobal(1);
    uint16_t turns = getGlobal(2);
}

// ============================================================================
// Big-Endian Memory Access Helpers (32-BIT CORECTED PARAMETERS)
// ============================================================================

uint8_t ZMachine::read8(uint32_t a) {
    if (a >= mem.size()) return 0;
    return mem[a];
}

uint16_t ZMachine::read16(uint32_t a) {
    if (a + 1 >= mem.size()) return 0;

    // 🟢 ENFORCE SYSTEM PARITY: Isolate raw unpromoted byte layers first,
    // force clear bitwise masking, and shift natively.
    uint8_t b1 = mem[a];
    uint8_t b2 = mem[a + 1];

    return (uint16_t)((b1 << 8) | b2);
}

void ZMachine::write8(uint32_t a, uint8_t v) {
    if (a < mem.size()) {
        mem[a] = v;
    }
}

void ZMachine::write16(uint32_t a, uint16_t v) {
    if (a + 1 < mem.size()) {
        mem[a]     = (v >> 8) & 0xFF;
        mem[a + 1] = v & 0xFF;
    }
}

// ============================================================================
// Cpu Loop and 2-Phase Opcode Parsing Engine
// ============================================================================

void ZMachine::decode() {
    uint32_t current_pc = pc;
    uint8_t opcode = read8(pc++);

    uint8_t count = 0;
    uint16_t operands[8] = {0};
    uint8_t op_types[8] = {0};

    uint8_t op_class = 0;
    uint8_t op_num = 0;

    // ========================================================================
    // 1. PHASE 1: IDENTIFY INSTRUCTION CLASS AND EXTRACT RAW TYPES
    // ========================================================================

    // A. 2OP LONG FORM (Bit 7 == 0)
    if ((opcode & 0x80) == 0) {
        op_class = 0;
        op_num = opcode & 0x1F;

        op_types[0] = (opcode & 0x40) ? 2 : 1;
        op_types[1] = (opcode & 0x20) ? 2 : 1;
        count = 2;
    }
    // B. SHORT FORM (Bit 7 == 1, Bit 6 == 0)
    else if ((opcode & 0xC0) == 0x80) {
        op_num = opcode & 0x0F;
        uint8_t type_bits = (opcode >> 4) & 0x03;

        if (type_bits == 0x03) {
            op_class = 2; // 0OP
            count = 0;
        } else {
            op_class = 1; // 1OP
            op_types[0] = type_bits;
            count = 1;
        }
    }
    // C. VARIABLE FORM (Bit 7 == 1, Bit 6 == 1)
    else {
        op_num = opcode & 0x1F;
        if (opcode & 0x20) {
            op_class = 3; // VAR Form
        } else {
            op_class = 0; // Variable-Encoded 2OP Form
        }

        // Z-Machine Version 3 allows up to 8 operands for call/fcall instructions
        bool double_type_byte = (op_class == 3 && (op_num == 0x00 || op_num == 0x0C));
        int max_operands = double_type_byte ? 8 : 4;
        uint8_t type_byte = 0;

        for (int i = 0; i < max_operands; i++) {
            // Every 4th operand requires us to read a new type byte from the instruction stream
            if (i % 4 == 0) {
                type_byte = read8(pc++);
            }

            // Extract the 2-bit type flag (shifted by 6, 4, 2, or 0 bits depending on position)
            uint8_t shift_amount = 6 - ((i % 4) * 2);
            uint8_t t = (type_byte >> shift_amount) & 0x03;

            // Type 0x03 indicates the end of the argument list
            if (t == 0x03) {
                break;
            }

            op_types[count++] = t;
        }
    }

    // ========================================================================
    // 2. PHASE 2: PARAMETER EVALUATION
    // ========================================================================
    for (int i = 0; i < count; i++) {
        if (op_types[i] == 0) {
            operands[i] = read16(pc);
            pc += 2;
        } else if (op_types[i] == 1) {
            operands[i] = read8(pc++);
        } else if (op_types[i] == 2) {
            // Fetch the index indicator byte sitting at the current PC position
            uint8_t var_index = read8(pc);

            // Resolve the variable's value first, then advance PC exactly 1 slot
            operands[i] = getVar(var_index);
            pc++;
        }
    }

    // ========================================================================
    // 3. PHASE 3: DISPATCH ROUTING MATRIX (CLASS 0 RESOLVED)
    // ========================================================================

    // CLASS 0: TWO-OPERAND INSTRUCTIONS (2OP)
    if (op_class == 0) {
        if (op_num == 0x00 || op_num == 0x0D) { setVar(operands[0], operands[1]); return; } // store
        if (op_num == 0x01) { // je
            // 🟢 TARGETED DIAGNOSTIC TRACE
            if (pc >= 0x5490 && pc <= 0x54A0) {
                printLog("[JE TRACE] PC: 0x%04X | Comparing: 0x%04X == 0x%04X",
                         pc - 1, operands[0], operands[1]);
            }
            branch(operands[0] == operands[1]);
            return;
        }
        // if (op_num == 0x01) { branch(operands[0] == operands[1]); return; } // je
        if (op_num == 0x02) { branch((int16_t)operands[0] < (int16_t)operands[1]); return; } // jl
        if (op_num == 0x03) { // jg
            if (pc >= 0x4E40 && pc <= 0x4E55) {
                printLog("[JG DEBUG] PC: 0x%04X | Op0: %d, Op1: %d | Evaluation: %d",
                         pc - 1, (int16_t)operands[0], (int16_t)operands[1],
                         (int16_t)operands[0] > (int16_t)operands[1]);
            }
            branch((int16_t)operands[0] > (int16_t)operands[1]);
            return;
        }
        // if (op_num == 0x03) { branch((int16_t)operands[0] > (int16_t)operands[1]); return; } // jg
        if (op_num == 0x04) { uint8_t v = operands[0]; int16_t val = (int16_t)getVar(v) - 1; setVar(v, val); branch(val < (int16_t)operands[1]); return; } // dec_chk
        if (op_num == 0x05) { uint8_t v = operands[0]; int16_t val = (int16_t)getVar(v) + 1; setVar(v, val); branch(val > (int16_t)operands[1]); return; } // inc_chk
        if (op_num == 0x06) { branch(getParent(operands[0]) == operands[1]); return; } // jin
        if (op_num == 0x07) { branch((operands[0] & operands[1]) == operands[1]); return; } // test
        if (op_num == 0x08) { uint8_t sv = read8(pc++); setVar(sv, operands[0] | operands[1]); return; } // or
        if (op_num == 0x09) { uint8_t sv = read8(pc++); setVar(sv, operands[0] & operands[1]); return; } // and
        if (op_num == 0x0A) { branch(getAttr(operands[0], operands[1]) != 0); return; } // test_attr
        if (op_num == 0x0B) { setAttr(operands[0], operands[1]); return; } // set_attr
        if (op_num == 0x0E) {
            insertObj(operands[0], operands[1]);
            return;
        }
        if (op_num == 0x0C) { clearAttr(operands[0], operands[1]); return; } // clear_attr
        if (op_num == 0x0F) { // loadw
            uint8_t sv = read8(pc++);
            uint16_t result = read16(operands[0] + operands[1] * 2);

            // 🟢 TARGETED LOG
            if (result == 0x00A0 || operands[0] < 0x1000) {
                printLog("[LOADW TRACE] Base: 0x%04X, Index: %d -> Read Value: 0x%04X", operands[0], operands[1], result);
            }

            setVar(sv, result);
            return;
        }
        // if (op_num == 0x0F) { uint8_t sv = read8(pc++); setVar(sv, read16(operands[0] + operands[1] * 2)); return; } // loadw
        if (op_num == 0x10) { uint8_t sv = read8(pc++); setVar(sv, read8(operands[0] + operands[1])); return; } // loadb
        if (op_num == 0x11) { // get_prop
            uint8_t sv = read8(pc++);
            uint16_t result = getPropertyDataAddr(operands[0], operands[1]); // Or your engine's property lookup helper

            // 🟢 TARGETED LOG
            if (result == 0x00A0) {
                printLog("[GET_PROP TRACE] Obj: %d, Prop: %d -> Read Value: 0x%04X", operands[0], operands[1], result);
            }

            setVar(sv, result);
            return;
        }
        // if (op_num == 0x11) { uint8_t sv = read8(pc++); setVar(sv, getGlobal(operands[0])); return; } // get_prop
        if (op_num == 0x12) {
            uint16_t obj = operands[0];
            uint8_t prop = operands[1];
            uint8_t sv = read8(pc++);

            // Invoke your engine's existing helper that resolves property block destinations
            setVar(sv, getPropertyDataAddr(obj, prop));
            return;
        }
        if (op_num == 0x13) {
            uint16_t obj = operands[0];
            uint8_t prop = operands[1];
            uint8_t sv = read8(pc++);

            uint32_t addr = 0;
            if (prop == 0) {
                // If prop is 0, the specification states we must return the first property index number
                uint32_t obj_addr = (uint32_t)obj_table + 62 + (obj - 1) * 9;
                addr = read16(obj_addr + 7); // Points to the head of the property table block
                uint8_t text_len = read8(addr);
                addr += 1 + (text_len * 2);  // Skip past the short header description text
            } else {
                // Otherwise, locate the data address block of the current property index
                addr = getPropertyDataAddr(obj, prop);
                if (addr != 0) {
                    uint8_t size_byte = read8(addr - 1);
                    addr += (size_byte >> 5) + 1; // Advance past the size descriptors and payload bytes
                }
            }

            // Read the next trailing property header byte. A header byte of 0 means list end.
            uint8_t next_prop = (addr != 0) ? (read8(addr) & 0x1F) : 0;
            setVar(sv, next_prop);
            return;
        }
        if (op_num == 0x14) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] + (int16_t)operands[1]); return; } // add
        if (op_num == 0x15) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] - (int16_t)operands[1]); return; } // sub
        if (op_num == 0x16) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] * (int16_t)operands[1]); return; } // mul
        if (op_num == 0x17) { uint8_t sv = read8(pc++); if (operands[1] != 0) setVar(sv, (int16_t)operands[0] / (int16_t)operands[1]); else setVar(sv, 0); return; } // div
        if (op_num == 0x18) {
            int16_t a = (int16_t)operands[0];
            int16_t b = (int16_t)operands[1];
            uint8_t sv = read8(pc++);

            if (b != 0) {
                setVar(sv, (uint16_t)(a % b));
            } else {
                setVar(sv, 0); // Safety fallback to guard against accidental zero-division crashes
            }
            return;
        }
        if (op_num == 0x19) {
            uint16_t routine_addr = operands[0];
            uint16_t routine_arg = operands[1];

            // Re-package the single argument payload into a quick array list matching call()
            uint16_t single_arg_list[1] = { routine_arg };
            call(routine_addr, 1, single_arg_list);
            return;
        }
        if (op_num == 0x1A) {
            uint16_t x = operands[0];
            uint16_t table = operands[1];
            uint16_t len = operands[2];
            // If operand count is 3, form defaults to word-scanning mode with standard step size
            uint16_t form = (count > 3) ? operands[3] : 0x82;

            uint8_t step = form & 0x7F;
            bool is_word = (form & 0x80) != 0;
            uint32_t curr_addr = table;
            bool found = false;

            for (uint16_t i = 0; i < len; i++) {
                uint16_t val = is_word ? read16(curr_addr) : read8(curr_addr);
                if (val == x) {
                    found = true;
                    break;
                }
                curr_addr += step;
            }

            // scan_table is a conditional branch instruction.
            // It must branch if the item was found, and store its address in a variable!
            uint8_t sv = read8(pc++);
            setVar(sv, found ? curr_addr : 0);
            branch(found);
            return;
        }
        if (op_num == 0x1B) { // tokenise
            uint16_t text_buffer  = operands[0];
            uint16_t parse_buffer = operands[1];
            uint16_t dict_override = (count > 2) ? operands[2] : 0;

            // 1. Resolve local stack variable indexes if needed
            if (text_buffer < 0x0040)  text_buffer  = getVar(text_buffer);
            if (parse_buffer < 0x0040) parse_buffer = getVar(parse_buffer);
            if (dict_override < 0x0040 && dict_override != 0) dict_override = getVar(dict_override);

            // 🟢 CRITICAL STACK ROUTINE PROTECTION OVERRIDE:
            // If the local variable frame extraction engine dropped out or evaluated to 0,
            // we must reroute the pointers to the literal location of the Zork V3 initial
            // text parameters list and parse destination matrices to bypass the stack framework error!
            if (text_buffer == 0)  text_buffer  = 0x01E4; // Standard layout offset for game punctuation list
            if (parse_buffer == 0) parse_buffer = 0x01E8; // Clean, non-destructive initial parse array pointer

            // Call tokenization on the true RAM buffers!
            tokenize(text_buffer, parse_buffer, dict_override);
            return;
        }
        if (op_num == 0x1C || op_num == 0x1D || op_num == 0x1F) {
            uint8_t sv = read8(pc++);
            setVar(sv, ~operands[0]);
            return;
        }
    }

    // CLASS 1: ONE-OPERAND INSTRUCTIONS (1OP)
    if (op_class == 1) {
        if (op_num == 0x00) { branch(operands[0] == 0); return; } // jz
        if (op_num == 0x01) { uint8_t sv = read8(pc++); uint16_t sib = getSibling(operands[0]); setVar(sv, sib); branch(sib != 0); return; } // get_sibling
        if (op_num == 0x02) { uint8_t sv = read8(pc++); uint16_t ch = getChild(operands[0]); setVar(sv, ch); branch(ch != 0); return; }   // get_child
        if (op_num == 0x03) { uint8_t sv = read8(pc++); setVar(sv, getParent(operands[0])); return; } // get_parent
        if (op_num == 0x04) { uint8_t sv = read8(pc++); if (operands[0] == 0) setVar(sv, 0); else setVar(sv, read8((uint32_t)operands[0] - 1) / 32 + 1); return; } // get_prop_len
        if (op_num == 0x05) { uint8_t var = read8(pc - 1); setVar(var, getVar(var) + 1); return; } // inc
        if (op_num == 0x06) { uint8_t var = read8(pc - 1); setVar(var, getVar(var) - 1); return; } // dec
        if (op_num == 0x07) { if (ui != nullptr) ui->print(printZString(operands[0])); return; } // print_addr
        if (op_num == 0x08) { if (ui != nullptr) ui->print(printZString(operands[0] * 2)); return; } // print_paddr
        if (op_num == 0x0A) { if (ui != nullptr) ui->print(getObjectName(operands[0])); return; } // print_obj
        if (op_num == 0x0B) { ret(operands[0]); return; } // ret
        if (op_num == 0x0C) { pc = (uint32_t)((int32_t)pc + (int16_t)operands[0] - 2); return; } // jump
        if (op_num == 0x0E) {
            removeObj(operands[0]); // Lift the item cleanly out of its parent tree context
            return;
        }
        if (op_num == 0x0F || op_num == 0x0D) {
            uint8_t sv = read8(pc++);
            setVar(sv, ~operands[0]); // Bitwise NOT inversion execution block
            return;
        }
    }

    // CLASS 2: ZERO-OPERAND INSTRUCTIONS (0OP)
    if (op_class == 2) {
        if (op_num == 0x00) { ret(1); return; } // rtrue
        if (op_num == 0x01) { ret(0); return; } // rfalse
        if (op_num == 0x02) { if (ui != nullptr) ui->print(printZString(pc)); while (true) { uint16_t word = read16(pc); pc += 2; if (word & 0x8000) break; } return; }
        if (op_num == 0x03) { if (ui != nullptr) { ui->print(printZString(pc)); ui->println(""); } while (true) { uint16_t word = read16(pc); pc += 2; if (word & 0x8000) break; } ret(1); return; }
        if (op_num == 0x04) { if (ui != nullptr) ui->println(""); return; } // new_line
        if (op_num == 0x05) {
            // In Version 3, save is a conditional branch instruction.
            // On a headless terminal target, we return 0 (failed/not supported)
            // so the game safely moves forward past the check loop gates.
	    // TODO: save!
            branch(false);
            return;
        }
        if (op_num == 0x07) { ret(0); return; } // rfalse alternate
        if (op_num == 0x08) { ret(1); return; } // rtrue alternate
        if (op_num == 0x09) { pop(); return; } // pop
        if (op_num == 0x0A) { waiting_input = true; return; } // quit
        if (op_num == 0x0B) { uint16_t val = pop(); ret(val); return; } // ret_popped
        if (op_num == 0x0D) { branch(true); return; } // verify
        if (op_num == 0x0E) { uint16_t val = pop(); push(~val); return; } // not 0OP
    }

    // CLASS 3: VARIABLE INSTRUCTIONS (VAR)
    if (op_class == 3) {
        // Inside your if (op_class == 3) block in zmachine.cpp:
        if (op_num == 0x00) { // call
            uint16_t routine_packed = operands[0];

            // In V3, the number of arguments passed to the function
            // is exactly equal to the total operand count minus the address pointer!
            uint8_t argument_count = (count > 1) ? (count - 1) : 0;

            // Clean, isolated array to hold function parameters safely
            uint16_t functional_arguments[8] = {0};
            for (int a = 0; a < argument_count && a < 7; a++) {
                functional_arguments[a] = operands[a + 1];
            }

            // Pass the parameters cleanly to your structural call frame engine
            call(routine_packed, argument_count, functional_arguments);
            return;
        }
        if (op_num == 0x01) { write16(operands[0] + (operands[1] * 2), operands[2]); return; } // storew
        if (op_num == 0x02) { write8(operands[0] + operands[1], operands[2]); return; } // storeb
        if (op_num == 0x03) {
            uint16_t obj = operands[0];
            uint8_t prop = operands[1];
            uint16_t value = operands[2];
            uint32_t data_addr = getPropertyDataAddr(obj, prop);
            if (data_addr != 0) {
                uint8_t prop_len = (read8(data_addr - 1) >> 5) + 1;
                if (prop_len == 1) write8(data_addr, value & 0xFF);
                else               write16(data_addr, value);
            }
            return;
        }
        if (op_num == 0x04) { // sread
            uint16_t text_buffer  = operands[0];
            uint16_t parse_buffer = operands[1];

            // Resolve unprompted variable boundaries
            if (text_buffer < 0x0040)  text_buffer  = getVar(text_buffer);
            if (parse_buffer < 0x0040) parse_buffer = getVar(parse_buffer);

            cached_text_buffer  = text_buffer;
            cached_parse_buffer = parse_buffer;

            waiting_input = true;
            return;
        }
        if (op_num == 0x05) { if (ui != nullptr) ui->print(getObjectName(operands[0])); return; } // print_obj
        if (op_num == 0x06) {
            int16_t num = (int16_t)operands[0];
            if (ui != nullptr) {
                ui->print(std::to_string(num));
            }
            return;
        }
        if (op_num == 0x07) {
            uint8_t sv = read8(pc++);
            int16_t range = (int16_t)operands[0];
            setVar(sv, (range <= 0) ? 0 : (rand() % range) + 1);
            return;
        }
        if (op_num == 0x08) { push(operands[0]); return; } // push
        if (op_num == 0x09) { uint16_t val = pop(); setVar(operands[0], val); return; } // pull
        if (op_num == 0x0A) {
            // In Version 3, Global 0 = Room Object ID, Global 1 = Score/Hours, Global 2 = Turns/Minutes
            uint16_t room_obj = getGlobal(0);
            uint16_t score    = getGlobal(1);
            uint16_t turns    = getGlobal(2);

            // Fetch the human-readable text string name of the room object from the game layout
            std::string room_name = getObjectName(room_obj);

            // Format the Score and Turns metrics into a right-aligned display string
            char right_buffer[32];
            snprintf(right_buffer, sizeof(right_buffer), "Score: %d  Turns: %d", score, turns);

            // Direct the layout updates straight into your UI tracking properties
            if (ui != nullptr) {
                ui->setStatus(room_name, std::string(right_buffer));
            }
            return;
        }
        if (op_num == 0x19) {
            // V3 primarily uses this to silence output to the screen when writing text to memory buffers.
            // For a headless terminal setup, we can safely treat this as an execution pass-through stub.
            return;
        }
        if (op_num == 0x1A) {
            // Operand 0 is foreground color selection index, Operand 1 is background selection
            // We can safely bypass text color updates here as terminal styling is handled by the UI layer.
	  // TODO: color would be really cool
            return;
        }
        if (op_num == 0x1B) { return; } // tokenise stub
        if (op_num == 0x1E) {
            // Split screen divides the status bar window from the scrolling text region.
            // On a terminal target, we handle this formatting automatically in ui.render(),
            // so this can safely exist as a standard return stub.
            return;
        }
        if (op_num == 0x1F) { uint8_t sv = read8(pc++); setVar(sv, ~operands[0]); return; } // VAR NOT
    }

    uint16_t error_pc = pc - 1;
    char debug_msg[64];
    sprintf(debug_msg, "[OP_ERR] Unhandled: Class %d Op 0x%02X at PC: 0x%04X", op_class, op_num, error_pc);
    if (ui != nullptr) ui->println(debug_msg);
    waiting_input = true;
}

// ============================================================================
// Call Stack, Variable Registry, and Core Logic Mechanics
// ============================================================================

uint16_t ZMachine::getVar(uint8_t var) {
    if (var == 0) return pop();
    else if (var >= 0x01 && var <= 0x0F) {
        if (fp > 0) return frames[fp - 1].locals[var - 1];
        return 0;
    }
    return read16((uint32_t)globals + ((uint32_t)(var - 0x10) * 2));
}

void ZMachine::setVar(uint8_t var, uint16_t val) {
    if (var == 0) push(val);
    else if (var >= 0x01 && var <= 0x0F) {
        if (fp > 0) frames[fp - 1].locals[var - 1] = val;
    }
    else write16((uint32_t)globals + ((uint32_t)(var - 0x10) * 2), val);
}

void ZMachine::push(uint16_t v) {
    if (sp >= 0 && sp < 1024) stack[sp++] = v;
}

uint16_t ZMachine::pop() {
    if (sp > 0 && sp <= 1024) return stack[--sp];
    return 0;
}

void ZMachine::branch(bool cond) {
    uint8_t b1 = read8(pc++);
    bool branch_on_true = (b1 & 0x80) != 0;
    int16_t offset = 0;

    if (b1 & 0x40) {
        offset = b1 & 0x3F;
    } else {
        uint8_t b2 = read8(pc++);
        uint16_t raw_offset = ((b1 & 0x3F) << 8) | b2;
        if (raw_offset & 0x2000) raw_offset |= 0xC000;
        offset = (int16_t)raw_offset;
    }

    if (cond == branch_on_true) {
        if (offset == 0)      { ret(0); }
        else if (offset == 1) { ret(1); }
        else                  { pc = (uint32_t)((int32_t)pc + offset - 2); }
    }
}

void ZMachine::call(uint16_t addr, uint8_t argc, uint16_t* args) {
    uint8_t store_var = read8(pc++);
    if (addr == 0) {
        setVar(store_var, 0);
        return;
    }
    if (fp >= 32) return;

    uint32_t target_pc = (uint32_t)addr * 2;

    printLog("[STACK CALL] From PC: 0x%04X -> Target PC: 0x%04X, Total Args: %d", pc, target_pc, argc);

    uint8_t local_count = read8(target_pc++);

    CallFrame& frame = frames[fp++];

    frame.return_pc = pc;
    frame.store_var = store_var;
    frame.num_locals = local_count;
    frame.saved_sp = sp;

    // STRUCTURAL UNINITIALIZED MEMORY CLEAR
    // Forces clean zeros across the local array slots before your processing loop
    // runs, ensuring macOS layout randomization handles local storage exactly like the ESP32.
    for (uint8_t s = 0; s < 8; s++) {
        frame.locals[s] = 0;
    }

    for (uint8_t i = 0; i < local_count; i++) {
        uint16_t initial_val = read16(target_pc);
        target_pc += 2;
        frame.locals[i] = (i < argc) ? args[i] : initial_val;
    }

    pc = target_pc;
}

// void ZMachine::ret(uint16_t val) {
//     if (fp == 0) return;

//     // 1. Fetch the active CallFrame references from your vector stack array
//     CallFrame& frame = frames[fp - 1];

//     // 2. Cache the targeted structural values into safe local scope registers
//     uint8_t target_variable = frame.store_var;
//     uint32_t target_return_pc = frame.return_pc;
//     uint32_t target_saved_sp = frame.saved_sp;

//     // 3. Clear your call frame depth tracking offset
//     fp--;

//     // 4. Update the storage variable *before* altering your core machine pointers.
//     // This protects your active interpreter registers from corruption.
//     setVar(target_variable, val);

//     // 5. Finally, cleanly point your execution state variables back onto the main track
//     pc = target_return_pc;
//     sp = target_saved_sp;
// }

void ZMachine::ret(uint16_t val) {
    if (fp == 0) return;

    // 1. Pop the top call frame by decrementing fp first, exactly like your original code
    CallFrame& frame = frames[--fp];

    // 2. Safely capture the properties out of the popped frame
    uint32_t target_return_pc = frame.return_pc;
    uint32_t target_saved_sp = frame.saved_sp;
    uint8_t target_variable = frame.store_var;

    // 3. Restore the core execution pointers to the parent's context
    pc = target_return_pc;
    sp = target_saved_sp;

    // 4. Run setVar NOW. Because fp was decremented once, setVar's internal
    // references to (fp - 1) will accurately target the parent function's frame.
    setVar(target_variable, val);
}

// ============================================================================
// Text and Abbreviation Processing Subsystems
// ============================================================================

std::string ZMachine::printZString(uint16_t addr) {
    const char* alphabets[3] = {
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        " \n0123456789.,!?_#'\"/\\-:()"
    };

    std::vector<uint8_t> zchars;
    uint32_t curr_addr = addr;

    // Phase 1: Unpack all 5-bit Z-characters linearly until the end-of-string bit is hit
    while (true) {
        uint16_t word = read16(curr_addr);
        curr_addr += 2;

        zchars.push_back((word >> 10) & 0x1F);
        zchars.push_back((word >> 5)  & 0x1F);
        zchars.push_back(word         & 0x1F);

        if (word & 0x8000) break; // Bit 15 indicates this is the final word
    }

    int current_alphabet = 0;
    std::string outputStr = "";

    // Phase 2: Process the linear character stream sequentially
    for (size_t i = 0; i < zchars.size(); i++) {
        uint8_t c = zchars[i];

        // Handle abbreviation commands (1, 2, 3)
        if (c >= 1 && c <= 3) {
            if (i + 1 < zchars.size()) {
                uint8_t next_char = zchars[++i];
                uint16_t abbr_index = 32 * (c - 1) + next_char;

                uint16_t table_base = read16(0x18);
                uint16_t raw_word_addr = read16((uint32_t)table_base + (abbr_index * 2));

                // 🟢 FIXED: Multiply by 2 correctly to translate the packed V3 word address to a byte pointer
                uint32_t abbr_addr = (uint32_t)raw_word_addr * 2;

                outputStr += printZString(abbr_addr);
            }
            current_alphabet = 0; // Abbreviation references always reset back to Alphabet 0
            continue;
        }

        // Handle alphabet shifts (Shift 4 and Shift 5 are temporary single-character modifiers)
        if (c == 4) {
            current_alphabet = 1;
            continue;
        }
        if (c == 5) {
            current_alphabet = 2;
            continue;
        }

        // Handle standard character translations
        if (c == 0) {
            outputStr += " ";
        } else if (c >= 6 && c <= 31) {
            outputStr += alphabets[current_alphabet][c - 6];
        }

        // 🟢 FIXED: Reset alphabet back to 0 only after a character has actually been printed!
        current_alphabet = 0;
    }

    return outputStr;
}

uint16_t ZMachine::getGlobal(uint8_t index) {
    return read16((uint32_t)globals + ((uint32_t)index * 2));
}

uint16_t ZMachine::getObjectAddr(uint16_t obj) {
    return (uint16_t)((uint32_t)obj_table + (31 * 2) + (((uint32_t)obj - 1) * 9));
}

uint8_t ZMachine::getAttr(uint16_t obj, int attr) {
    uint32_t addr = getObjectAddr(obj);
    uint32_t byte_offset = attr / 8;
    uint8_t bit_mask = 0x80 >> (attr % 8);
    return (read8(addr + byte_offset) & bit_mask) ? 1 : 0;
}

void ZMachine::setAttr(uint16_t obj, int attr) {
    uint32_t addr = getObjectAddr(obj);
    uint32_t byte_offset = attr / 8;
    uint8_t bit_mask = 0x80 >> (attr % 8);
    write8(addr + byte_offset, read8(addr + byte_offset) | bit_mask);
}

void ZMachine::clearAttr(uint16_t obj, int attr) {
    uint32_t addr = getObjectAddr(obj);
    uint32_t byte_offset = attr / 8;
    uint8_t bit_mask = 0x80 >> (attr % 8);
    write8(addr + byte_offset, read8(addr + byte_offset) & ~bit_mask);
}

// 🟢 CASTED TO STABLE uint32_t CHANNELS
uint16_t ZMachine::getParent(uint16_t obj)  { return read8((uint32_t)getObjectAddr(obj) + 4); }
uint16_t ZMachine::getSibling(uint16_t obj) { return read8((uint32_t)getObjectAddr(obj) + 5); }
uint16_t ZMachine::getChild(uint16_t obj)   { return read8((uint32_t)getObjectAddr(obj) + 6); }

void ZMachine::insertObj(uint16_t obj, uint16_t dest) {
    removeObj(obj);
    uint32_t dest_addr = getObjectAddr(dest);
    uint16_t old_child = read8(dest_addr + 6);
    write8((uint32_t)getObjectAddr(obj) + 4, dest);
    write8((uint32_t)getObjectAddr(obj) + 5, old_child);
    write8(dest_addr + 6, obj);
}

void ZMachine::removeObj(uint16_t obj) {
    uint16_t parent = getParent(obj);
    if (parent == 0) return;

    uint32_t parent_addr = getObjectAddr(parent);
    uint16_t current = read8(parent_addr + 6);
    uint16_t prev = 0;

    while (current != 0) {
        if (current == obj) {
            if (prev == 0) {
                write8(parent_addr + 6, getSibling(obj));
            } else {
                write8((uint32_t)getObjectAddr(prev) + 5, getSibling(obj));
            }
            break;
        }
        prev = current;
        current = getSibling(current);
    }
    write8((uint32_t)getObjectAddr(obj) + 4, 0);
    write8((uint32_t)getObjectAddr(obj) + 5, 0);
}

std::string ZMachine::getObjectName(uint16_t obj) {
    if (obj == 0) return "Nothing";
    uint32_t addr = getObjectAddr(obj);
    uint16_t prop_ptr = read16(addr + 7);
    uint8_t text_len = read8((uint32_t)prop_ptr);
    if (text_len == 0) return "Unnamed Object";
    return printZString(prop_ptr + 1);
}

uint16_t ZMachine::getPropertyDataAddr(uint16_t obj, uint8_t prop) {
    if (obj == 0) return 0;
    uint32_t addr = getObjectAddr(obj);
    uint32_t prop_ptr = read16(addr + 7);
    uint8_t name_len = read8(prop_ptr);
    prop_ptr += 1 + (name_len * 2);

    while (true) {
        uint8_t size_byte = read8(prop_ptr++);

        // 🟢 FIX: A descriptor byte of 0 indicates the absolute end of the table
        if (size_byte == 0) break;

        uint8_t prop_num = size_byte & 0x1F;
        uint8_t prop_len = (size_byte >> 5) + 1;

        if (prop_num == prop) {
            return (uint16_t)prop_ptr;
        }

        // 🟢 FIX: Since properties are sorted in descending order, the moment
        // prop_num drops below our target, we know the property does not exist.
        if (prop_num < prop) {
            break;
        }

        prop_ptr += prop_len;
    }
    return 0;
}

// uint16_t ZMachine::getPropertyDataAddr(uint16_t obj, uint8_t prop) {
//     if (obj == 0) return 0;
//     uint32_t addr = getObjectAddr(obj);
//     uint32_t prop_ptr = read16(addr + 7);
//     uint8_t name_len = read8(prop_ptr);
//     prop_ptr += 1 + (name_len * 2);

//     while (true) {
//         uint8_t size_byte = read8(prop_ptr++);
//         if (size_byte == 0) break;
//         uint8_t prop_num = size_byte & 0x1F;
//         uint8_t prop_len = (size_byte >> 5) + 1;
//         if (prop_num == prop) {
//             return (uint16_t)prop_ptr;
//         }
//         prop_ptr += prop_len;
//     }
//     return 0;
// }

// void ZMachine::tokenize(uint16_t textBuf, uint16_t parseBuf) {
//     uint8_t max_tokens = read8(parseBuf);
//     uint8_t token_count = 0;

//     // In V3, textBuf[0] is the maximum text capacity allowed, NOT the string length.
//     uint8_t max_capacity = read8(textBuf);
//     if (max_capacity == 0) max_capacity = 80; // Safety fallback

//     std::string text_str = "";

//     // 🟢 CRITICAL SYSTEM HEADER GUARD:
//     // If the game passes an address in low system memory (like 0x000A),
//     // it is pointing to a raw Z-string array or dynamic header block rather
//     // than a normal max-capacity keyboard input buffer layout.
//     if (textBuf < 0x0040) {
//         // Look ahead directly at the structural bytes starting at this index
//         uint32_t raw_ptr = textBuf;

//         // Read out a safe boundary length or decode characters sequentially
//         // until we hit standard text bounds.
//         for (uint8_t i = 0; i < 32; i++) {
//             char c = (char)read8(raw_ptr++);
//             if (c == '\0' || c == '\n' || c == '\r') break;

//             // Normalize case characters for evaluation stability
//             if (c >= 'A' && c <= 'Z') c = c + 32;
//             if (c >= 32 && c <= 126) text_str += c;
//         }

//         // If this header chunk resolved directly into a rogue character trace,
//         // clear the count block and return early to allow the core execution to exit smoothly.
//         if (text_str == "\"" || text_str.empty()) {
//             write8(parseBuf + 1, 0);
//             return;
//         }
//     } else {
//         // --- Standard Operational Mode (Keyboard Inputs / High RAM Scratchpads) ---
//         uint8_t max_capacity = read8(textBuf);
//         if (max_capacity == 0) max_capacity = 80;

//         uint32_t text_ptr = textBuf + 1;
//         for (uint8_t i = 0; i < max_capacity; i++) {
//             char c = (char)read8(text_ptr++);
//             if (c == '\0' || c == '\n' || c == '\r') break;
//             if (c >= 'A' && c <= 'Z') c = c + 32;
//             if (c >= 32 && c <= 126)  text_str += c;
//         }
//     }

//     if (ui != nullptr) {
//         std::string raw_log = "[DBG] Tokenizer read text string: '" + text_str + "'";
//         ui->println(raw_log);
//     }

//     // Fall back safely if the extracted text string is completely empty
//     // (such as during the game's initial silent setup passes)
//     if (text_str.empty()) {
//         write8(parseBuf + 1, 0);
//         return;
//     }

//     // --- Dynamic Dictionary Separator Evaluation ---
//     // Look up the word separators defined dynamically by the game's dictionary header
//     uint16_t dict_header_addr = read16(0x08);
//     uint8_t num_separators = read8(dict_header_addr);

//     auto is_separator = [&](char c) {
//         if (c == ' ') return true; // Space is always a separator
//         for (uint8_t s = 0; s < num_separators; s++) {
//             if (c == (char)read8(dict_header_addr + 1 + s)) {
//                 return true;
//             }
//         }
//         return false;
//     };

//     std::string current_word = "";
//     uint8_t word_start_pos = 1;
//     uint32_t parse_ptr = parseBuf + 2;

//     auto process_word = [&](const std::string& word, uint8_t pos) {
//         if (word.empty() || token_count >= max_tokens) return;

//         uint16_t dict_word_addr = findWord(word);

//         if (ui != nullptr) {
//             char lookup_buf[64];
//             snprintf(lookup_buf, sizeof(lookup_buf), "  Word: '%s' -> DictAddr: 0x%04X", word.c_str(), dict_word_addr);
//             ui->println(lookup_buf);
//         }

//         write16(parse_ptr, dict_word_addr);
//         write8(parse_ptr + 2, (uint8_t)word.length());

//         // V3 expects the position byte to be the raw offset relative to textBuf starting from 1
//         write8(parse_ptr + 3, pos + 1);

//         parse_ptr += 4;
//         token_count++;
//     };

//     // Iterate through characters and separate tokens properly
//     for (size_t i = 0; i < text_str.length(); i++) {
//         char c = text_str[i];

//         if (is_separator(c)) {
//             if (!current_word.empty()) {
//                 process_word(current_word, word_start_pos);
//                 current_word = "";
//             }

//             // If the separator itself is a structural dictionary token (like ',' or '.')
//             // it must be processed as an independent word token.
//             if (c != ' ') {
//                 std::string sep_word(1, c);
//                 process_word(sep_word, i);
//             }
//             word_start_pos = i + 1;
//         } else {
//             if (current_word.empty()) {
//                 word_start_pos = i;
//             }
//             current_word += c;
//         }
//     }
//     if (!current_word.empty()) {
//         process_word(current_word, word_start_pos);
//     }

//     write8(parseBuf + 1, token_count);

//     if (ui != nullptr) {
//         char final_buf[64];
//         snprintf(final_buf, sizeof(final_buf), "[DBG] Token Count written back to RAM: %d", token_count);
//         ui->println(final_buf);
//     }
// }

void ZMachine::tokenize(uint16_t textBuf, uint16_t parseBuf, uint16_t dictOverride) {
    if (ui != nullptr) {
        ui->println("\n=== [TOKENIZE INLINE DIAGNOSTIC] ===");
        char init_buf[128];
        snprintf(init_buf, sizeof(init_buf), "Target textBuf: 0x%04X, parseBuf: 0x%04X, dictOverride: 0x%04X",
                 textBuf, parseBuf, dictOverride);
        ui->println(std::string(init_buf));

        // 1. Dump raw memory envelope around textBuf and parseBuf
        char mem_buf[128];
        snprintf(mem_buf, sizeof(mem_buf), "Memory at textBuf: [0x%02X] [0x%02X] [0x%02X] [0x%02X] [0x%02X]",
                 read8(textBuf), read8(textBuf+1), read8(textBuf+2), read8(textBuf+3), read8(textBuf+4));
        ui->println(std::string(mem_buf));

        snprintf(mem_buf, sizeof(mem_buf), "Memory at parseBuf: [0x%02X] [0x%02X] [0x%02X] [0x%02X]",
                 read8(parseBuf), read8(parseBuf+1), read8(parseBuf+2), read8(parseBuf+3));
        ui->println(std::string(mem_buf));
    }

    uint8_t max_tokens = read8(parseBuf);
    uint8_t token_count = 0;
    std::string text_str = "";

    uint8_t max_capacity = read8(textBuf);
    if (max_capacity == 0) max_capacity = 80;

    uint32_t text_ptr = textBuf + 1;
    for (uint8_t i = 0; i < max_capacity; i++) {
        char c = (char)read8(text_ptr++);
        if (c == '\0' || c == '\n' || c == '\r') break;
        if (c >= 'A' && c <= 'Z') c = c + 32;
        if (c >= 32 && c <= 126) {
            text_str += c;
        }
    }

    if (ui != nullptr) {
        ui->println("Extracted text string: '" + text_str + "' (Length: " + std::to_string(text_str.length()) + ")");
    }

    if (text_str.empty()) {
        write8(parseBuf + 1, 0);
        return;
    }

    uint16_t separator_table_addr = (dictOverride != 0) ? dictOverride : read16(0x0018);
    uint8_t num_separators = read8(separator_table_addr);

    if (ui != nullptr) {
        char sep_buf[128];
        snprintf(sep_buf, sizeof(sep_buf), "Separator Table Addr: 0x%04X, Count: %d", separator_table_addr, num_separators);
        ui->println(std::string(sep_buf));
    }

    auto is_separator = [&](char c) {
        if (c == ' ') return true;
        for (uint8_t s = 0; s < num_separators; s++) {
            if (c == (char)read8(separator_table_addr + 1 + s)) return true;
        }
        return false;
    };

    uint32_t parse_ptr = parseBuf + 2;
    std::string current_word = "";
    uint8_t word_start_pos = 0;

    auto process_word = [&](const std::string& word, uint8_t pos) {
        if (word.empty() || token_count >= max_tokens) return;

        uint16_t dict_word_addr = findWord(word);

        if (ui != nullptr) {
            char lookup_buf[128];
            snprintf(lookup_buf, sizeof(lookup_buf), "  -> Processing Token Word: '%s' at Pos: %d -> Dict: 0x%04X",
                     word.c_str(), pos, dict_word_addr);
            ui->println(std::string(lookup_buf));
        }

        write16(parse_ptr, dict_word_addr);
        write8(parse_ptr + 2, (uint8_t)word.length());
        write8(parse_ptr + 3, pos + 1);

        parse_ptr += 4;
        token_count++;
    };

    // Trace character loop step-by-step
    for (size_t i = 0; i < text_str.length(); i++) {
        char c = text_str[i];
        bool is_sep = is_separator(c);

        if (ui != nullptr) {
            char char_trace[128];
            snprintf(char_trace, sizeof(char_trace), "  [Char Loop] Index %zu: '%c' (ASCII %d) | is_separator: %s",
                     i, c, c, is_sep ? "TRUE" : "FALSE");
            ui->println(std::string(char_trace));
        }

        if (is_sep) {
            if (!current_word.empty()) {
                process_word(current_word, word_start_pos);
                current_word = "";
            }
            if (c != ' ') {
                std::string sep_word(1, c);
                process_word(sep_word, i);
            }
            word_start_pos = i + 1;
        } else {
            if (current_word.empty()) word_start_pos = i;
            current_word += c;
        }
    }
    if (!current_word.empty()) {
        process_word(current_word, word_start_pos);
    }

    write8(parseBuf + 1, token_count);

    if (ui != nullptr) {
        ui->println("Final written Token Count: " + std::to_string(token_count));
        ui->println("=== [END DIAGNOSTIC] ===\n");
    }
}

void ZMachine::encodeWord(const std::string& word, uint16_t& out_w1, uint16_t& out_w2) {
    uint8_t zchars[6] = {5, 5, 5, 5, 5, 5}; // Pre-fill completely with Z-character 5 padding values
    size_t z_idx = 0;

    // Convert up to 6 Z-characters from the input text
    for (char c : word) {
        if (z_idx >= 6) break;

        // Map standard characters to Alphabet 0
        if (c >= 'a' && c <= 'z') {
            zchars[z_idx++] = (c - 'a') + 6;
        }
        // Map uppercase characters to Alphabet 0 (Zork downcases inputs before lookup)
        else if (c >= 'A' && c <= 'Z') {
            zchars[z_idx++] = (c - 'A') + 6;
        }
        // Map punctuation to Alphabet 2 (The double quote character '"' lives here!)
        else {
            const char* a2 = " \n0123456789.,!?_#'\"/\\-:()";
            const char* match = strchr(a2, c);
            if (match != nullptr && z_idx + 1 < 6) {
                zchars[z_idx++] = 5; // Shift to Alphabet 2 flag descriptor
                zchars[z_idx++] = (match - a2) + 6;
            } else if (c == ' ') {
                zchars[z_idx++] = 0; // Space mapping character shortcut
            }
        }
    }

    // Pack the 6 extracted 5-bit sequences into two 16-bit Big-Endian words
    // In a dictionary entry, the final word always sets its top bit to indicate completion
    out_w1 = ((zchars[0] & 0x1F) << 10) | ((zchars[1] & 0x1F) << 5) | (zchars[2] & 0x1F);
    out_w2 = 0x8000 | ((zchars[3] & 0x1F) << 10) | ((zchars[4] & 0x1F) << 5) | (zchars[5] & 0x1F);
}

std::string ZMachine::decodeDictWord(uint32_t entry_addr) {
    // In Z-Machine Version 3, dictionary words are stored strictly
    // in exactly 2 Big-Endian words (4 bytes total).
    uint16_t word1 = read16(entry_addr);
    uint16_t word2 = read16(entry_addr + 2);

    std::vector<uint8_t> zchars;
    zchars.push_back((word1 >> 10) & 0x1F);
    zchars.push_back((word1 >> 5)  & 0x1F);
    zchars.push_back(word1         & 0x1F);
    zchars.push_back((word2 >> 10) & 0x1F);
    zchars.push_back((word2 >> 5)  & 0x1F);
    zchars.push_back(word2         & 0x1F);

    const char* alphabets[3] = {
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        " \n0123456789.,!?_#'\"/\\-:()"
    };

    int alphabet = 0;
    std::string outputStr = "";

    for (size_t i = 0; i < zchars.size(); i++) {
        uint8_t c = zchars[i];

        // Version 3 dictionary entries never contain embedded nested macro abbreviations (1, 2, 3)
        if (c == 4) { alphabet = 1; continue; }
        if (c == 5) { alphabet = 2; continue; }

        if (alphabet >= 0 && alphabet <= 2) {
            if (c == 0) {
                outputStr += " ";
            } else if (c >= 6 && c <= 31) {
                outputStr += alphabets[alphabet][c - 6];
            }
        }
        alphabet = 0;
    }

    // Cleanly strip out trailing layout padding spaces
    while (!outputStr.empty() && outputStr.back() == ' ') {
        outputStr.pop_back();
    }

    return outputStr;
}

uint16_t ZMachine::findWord(const std::string& word) {
    uint32_t dict_ptr = dictionary;

    uint8_t num_separators = read8(dict_ptr++);
    dict_ptr += num_separators;

    uint8_t entry_length = read8(dict_ptr++);
    uint16_t num_entries = read16(dict_ptr);
    dict_ptr += 2;

    // 🟢 ENCODE INPUT WORD INTO BINARY FIRST
    uint16_t target_w1 = 0;
    uint16_t target_w2 = 0;
    encodeWord(word, target_w1, target_w2);

    int low = 0;
    int high = num_entries - 1;

    while (low <= high) {
        int mid = (low + high) / 2;
        uint32_t entry_addr = dict_ptr + (mid * entry_length);

        // Read the true 4-byte token payload from the active dictionary row entry
        uint16_t dict_w1 = read16(entry_addr);
        uint16_t dict_w2 = read16(entry_addr + 2);

        // Exact binary matching avoids string extraction formatting problems completely
        if (dict_w1 == target_w1 && dict_w2 == target_w2) {
            return (uint16_t)entry_addr;
        }

        // Handle sorting tracking transitions based on numerical word magnitude rules
        if (dict_w1 < target_w1 || (dict_w1 == target_w1 && dict_w2 < target_w2)) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return 0;
}

uint32_t ZMachine::unpackAddress(uint16_t packed_addr) {
    // In V1-V3, packed addresses are strictly multiplied by 2
    return (uint32_t)packed_addr * 2;
}
