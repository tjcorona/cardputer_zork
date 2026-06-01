#include "zmachine.hpp"
#include <SD.h>
#include <M5Cardputer.h>

bool ZMachine::load(const char* path, UI& uiRef) {
    ui = &uiRef;

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }

    size_t size = file.size();
    if (size == 0) {
        file.close();
        return false;
    }

    mem.resize(size);

    size_t bytesRead = file.read(mem.data(), size);
    file.close();

    if (bytesRead != size) {
        return false;
    }

    reset();
    return true;
}

void ZMachine::reset() {
    sp = 0;
    fp = 0; 
    waiting_input = false;

    // 🟢 SPEC FIX: Cast literal offsets to explicit uint32_t parameters
    // to guarantee clean 32-bit memory vector address lookups on the ESP32-S3!
    pc         = read16((uint32_t)0x06); 
    dictionary = read16((uint32_t)0x08); 
    obj_table  = read16((uint32_t)0x0A); 
    globals    = read16((uint32_t)0x0C); 
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

    // Retrieve the target buffer addresses we cached during the sread opcode pass
    uint32_t text_buffer = read16((uint32_t)globals + 48);
    uint32_t parse_buffer = read16((uint32_t)globals + 50);
    
    if (text_buffer == 0) text_buffer = 0x0200;
    if (parse_buffer == 0) parse_buffer = 0x0300;

    uint8_t max_chars = read8(text_buffer);
    uint32_t write_ptr = text_buffer + 1;
    uint8_t chars_written = 0;

    for (char c : input) {
        if (chars_written >= max_chars - 1) break;
        char lower_c = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
        write8(write_ptr++, lower_c);
        chars_written++;
    }
    write8(write_ptr, 0); 

    // Parse the typed text immediately before the engine resumes
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
    return (mem[a] << 8) | mem[a + 1];
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

void ZMachine::step() {
    uint32_t current_pc = pc;
    uint8_t opcode = read8(pc); // Peek the upcoming instruction byte

    // 1. Save execution data into our rolling ring history buffer
    hist_pc[hist_idx] = current_pc;
    hist_op[hist_idx] = opcode;
    
    // Quick peek classification logic for logging purposes
    if ((opcode & 0x80) == 0) {
        hist_cls[hist_idx] = 0; // 2OP
        hist_num[hist_idx] = opcode & 0x1F;
    } else if ((opcode & 0xC0) == 0x80) {
        hist_cls[hist_idx] = ((opcode >> 4) & 0x03) == 0x03 ? 2 : 1; // 0OP or 1OP
        hist_num[hist_idx] = opcode & 0x0F;
    } else {
        hist_cls[hist_idx] = (opcode & 0x20) ? 3 : 0; // VAR or 2OP Variable
        hist_num[hist_idx] = opcode & 0x1F;
    }

    // Step the index forward in our 32-slot ring buffer
    hist_idx = (hist_idx + 1) % 32;
    total_executed_ops++;

    // 2. FORCE CORE DUMP AT STEP 5000 TO CATCH EXTENDED LOOP DEADLOCKS
    if (total_executed_ops == 5000) {
        if (ui != nullptr) {
            ui->println("--- 32-STEP CORE DUMP ---");
            
            // Print the rolling history in chronological order (oldest to newest)
            int print_idx = hist_idx;
            for (int i = 0; i < 32; i++) {
                char dump_buf[64];
                snprintf(dump_buf, sizeof(dump_buf), "[%d] PC:0x%04X OP:0x%02X CL:%d NUM:0x%02X", 
                         i, hist_pc[print_idx], hist_op[print_idx], 
                         hist_cls[print_idx], hist_num[print_idx]);
                ui->println(dump_buf);
                print_idx = (print_idx + 1) % 32;
            }
            
            char summary_buf[64];
            snprintf(summary_buf, sizeof(summary_buf), "[DIAG] Halted at total ops: %d", total_executed_ops);
            ui->println(summary_buf);
            ui->render(); // Push pixels directly to LCD screen panel
        }

        // Freeze the ESP32 hardware entirely right here so nothing gets overwritten
        while (true) { delay(100); }
    }

    // Pass control to your main parsing execution matrix
    decode();
}

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
        
        uint8_t type_byte = read8(pc++);
        for (int i = 0; i < 4; i++) {
            uint8_t t = (type_byte >> (6 - (i * 2))) & 0x03;
            if (t == 0x03) break; 
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
            operands[i] = getVar(read8(pc++));
        }
    }

    // ========================================================================
    // 3. PHASE 3: DISPATCH ROUTING MATRIX (CLASS 0 RESOLVED)
    // ========================================================================

    // CLASS 0: TWO-OPERAND INSTRUCTIONS (2OP)
    if (op_class == 0) {
        if (op_num == 0x00 || op_num == 0x0D) { setVar(operands[0], operands[1]); return; } // store
        if (op_num == 0x01) { branch(operands[0] == operands[1]); return; } // je
        if (op_num == 0x02) { branch((int16_t)operands[0] < (int16_t)operands[1]); return; } // jl
        if (op_num == 0x03) { branch((int16_t)operands[0] > (int16_t)operands[1]); return; } // jg
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
        if (op_num == 0x0F) { uint8_t sv = read8(pc++); setVar(sv, read16(operands[0] + operands[1] * 2)); return; } // loadw
        if (op_num == 0x10) { uint8_t sv = read8(pc++); setVar(sv, read8(operands[0] + operands[1])); return; } // loadb
        if (op_num == 0x11) { uint8_t sv = read8(pc++); setVar(sv, getGlobal(operands[0])); return; } // get_prop
        if (op_num == 0x14) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] + (int16_t)operands[1]); return; } // add
        if (op_num == 0x15) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] - (int16_t)operands[1]); return; } // sub
        if (op_num == 0x16) { uint8_t sv = read8(pc++); setVar(sv, (int16_t)operands[0] * (int16_t)operands[1]); return; } // mul
        if (op_num == 0x17) { uint8_t sv = read8(pc++); if (operands[1] != 0) setVar(sv, (int16_t)operands[0] / (int16_t)operands[1]); else setVar(sv, 0); return; } // div
        if (op_num == 0x19) {
            uint16_t routine_addr = operands[0];
            uint16_t routine_arg = operands[1];
            
            // Re-package the single argument payload into a quick array list matching call()
            uint16_t single_arg_list[1] = { routine_arg };
            call(routine_addr, 1, single_arg_list);
            return;
        }
        if (op_num == 0x1F || op_num == 0x1D) { uint8_t sv = read8(pc++); setVar(sv, ~operands[0]); return; } // not 2OP
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
        // Inside your if (op_class == 3) block in zmachine.cpp:
        if (op_num == 0x04) { // sread
            // Save the active buffer addresses directly to your global offset registers.
            // This safely yields execution back to your cooperative zork.ino loop!
            write16((uint32_t)globals + 48, operands[0]); // Save text buffer address
            write16((uint32_t)globals + 50, operands[1]); // Save parse buffer address

            waiting_input = true; 
            return;
        }
        if (op_num == 0x05) { if (ui != nullptr) ui->print(getObjectName(operands[0])); return; } // print_obj
        if (op_num == 0x08) { push(operands[0]); return; } // push
        if (op_num == 0x09) { uint16_t val = pop(); setVar(operands[0], val); return; } // pull
        if (op_num == 0x0C) {
            uint8_t sv = read8(pc++);
            int16_t range = (int16_t)operands[0];
            setVar(sv, (range <= 0) ? 0 : (rand() % range) + 1);
            return;
        }
        if (op_num == 0x1B) { return; } // tokenise stub
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
    // 🟢 SPEC FIX: Explicitly cast calculations to uint32_t to protect the table boundaries
    return read16((uint32_t)globals + ((uint32_t)(var - 0x10) * 2));
}

void ZMachine::setVar(uint8_t var, uint16_t val) {
    if (var == 0) push(val);
    else if (var >= 0x01 && var <= 0x0F) {
        if (fp > 0) frames[fp - 1].locals[var - 1] = val;
    } 
    // 🟢 SPEC FIX: Explicitly cast calculations to uint32_t to protect the table boundaries
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
    uint8_t local_count = read8(target_pc++);

    CallFrame& frame = frames[fp++];
    frame.return_pc = pc;
    frame.store_var = store_var;
    frame.num_locals = local_count;
    frame.saved_sp = sp;

    for (uint8_t i = 0; i < local_count; i++) {
        uint16_t initial_val = read16(target_pc);
        target_pc += 2;
        frame.locals[i] = (i < argc) ? args[i] : initial_val;
    }

    pc = target_pc;
}

void ZMachine::ret(uint16_t val) {
    if (fp == 0) return;
    CallFrame& frame = frames[--fp];
    pc = frame.return_pc;
    sp = frame.saved_sp;
    setVar(frame.store_var, val);
}

// ============================================================================
// Text and Abbreviation Processing Subsystems
// ============================================================================

std::string ZMachine::printZString(uint16_t addr) {
    // 🟢 FIXED STRING ARRAY CHAR ESCAPES
    const char* alphabets[3] = {
        "abcdefghijklmnopqrstuvwxyz",
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        " \n0123456789.,!?_#'\"/\\-:()"
    };

    int alphabet = 0;
    std::string outputStr = "";

    while (true) {
        uint16_t word = read16((uint32_t)addr);
        addr += 2;

        uint8_t chars[3];
        chars[0] = (word >> 10) & 0x1F;
        chars[1] = (word >> 5)  & 0x1F;
        chars[2] = word         & 0x1F;

        for (int i = 0; i < 3; i++) {
            uint8_t c = chars[i];
            if (alphabet >= 0 && alphabet <= 2) {
                if (c == 0) {
                    outputStr += " ";
                }
                else if (c >= 1 && c <= 3) {
                    if (i < 2) {
                        uint8_t next_char = chars[i + 1];
                        i++;
                        uint16_t abbr_index = 32 * (c - 1) + next_char;
                        uint16_t table_base = read16(0x18);
                        uint16_t abbr_addr = read16((uint32_t)table_base + (abbr_index * 2)) * 2;
                        outputStr += printZString(abbr_addr);
                    }
                }
                else if (c == 4) {
                    alphabet = 1;
                    continue;
                } else if (c == 5) {
                    alphabet = 2;
                    continue;
                } else if (c >= 6 && c <= 31) {
                    outputStr += alphabets[alphabet][c - 6];
                }
            }
            alphabet = 0;
        }
        if (word & 0x8000) break;
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
        if (size_byte == 0) break;
        uint8_t prop_num = size_byte & 0x1F;
        uint8_t prop_len = (size_byte >> 5) + 1;
        if (prop_num == prop) {
            return (uint16_t)prop_ptr;
        }
        prop_ptr += prop_len;
    }
    return 0;
}

void ZMachine::tokenize(uint16_t textBuf, uint16_t parseBuf) {
    uint8_t max_tokens = read8(parseBuf);
    uint8_t token_count = 0;
    
    // Read raw text characters from textBuf (starts at byte 1)
    std::string text_str = "";
    uint32_t text_ptr = textBuf + 1;
    while (true) {
        char c = (char)read8(text_ptr++);
        if (c == '\0') break;
        text_str += c;
    }

    // 🟢 DIAGNOSTIC LOG 1: PRINT EXACTLY WHAT THE KEYBOARD WROTE TO RAM
    if (ui != nullptr) {
        std::string raw_log = "[DBG] Tokenizer read text string: '" + text_str + "'";
        ui->println(raw_log);
    }

    std::string current_word = "";
    uint8_t word_start_pos = 1;
    uint32_t parse_ptr = parseBuf + 2; // Token data entries start at byte 2

    // Helper lambda to process a single word
    auto process_word = [&](const std::string& word, uint8_t pos) {
        if (word.empty() || token_count >= max_tokens) return;
        
        uint16_t dict_word_addr = findWord(word);
        
        // 🟢 DIAGNOSTIC LOG 2: LOG EACH WORD MATCH OPERATION
        if (ui != nullptr) {
            char lookup_buf[64];
            snprintf(lookup_buf, sizeof(lookup_buf), "  Word: '%s' -> DictAddr: 0x%04X", word.c_str(), dict_word_addr);
            ui->println(lookup_buf);
        }

        write16(parse_ptr, dict_word_addr);
        write8(parse_ptr + 2, (uint8_t)word.length());
        write8(parse_ptr + 3, pos);
        
        parse_ptr += 4;
        token_count++;
    };

    // Parse loop using character checks
    for (size_t i = 0; i < text_str.length(); i++) {
        char c = text_str[i];
        if (c == ' ' || c == ',' || c == '.') {
            if (!current_word.empty()) {
                process_word(current_word, word_start_pos);
                current_word = "";
            }
            word_start_pos = i + 2; // 1-indexed relative to text buffer start
        } else {
            if (current_word.empty()) {
                word_start_pos = i + 1;
            }
            current_word += c;
        }
    }
    if (!current_word.empty()) {
        process_word(current_word, word_start_pos);
    }

    // Byte 1 stores total parsed word count
    write8(parseBuf + 1, token_count);

    // 🟢 DIAGNOSTIC LOG 3: PRINT THE TOTAL TOKEN COUNT WRITTEN
    if (ui != nullptr) {
        char final_buf[64];
        snprintf(final_buf, sizeof(final_buf), "[DBG] Token Count written back to RAM: %d", token_count);
        ui->println(final_buf);
    }
}

uint16_t ZMachine::findWord(const std::string& word) {
    uint32_t dict_ptr = dictionary;
    
    uint8_t num_separators = read8(dict_ptr++);
    dict_ptr += num_separators; 
    
    uint8_t entry_length = read8(dict_ptr++); 
    uint16_t num_entries = read16(dict_ptr);
    dict_ptr += 2; 

    std::string clamp_word = word.substr(0, 6);

    int low = 0;
    int high = num_entries - 1;
    
    while (low <= high) {
        int mid = (low + high) / 2;
        uint32_t entry_addr = dict_ptr + (mid * entry_length);
        
        std::string dict_str = printZString(entry_addr);
        while(!dict_str.empty() && dict_str.back() == ' ') {
            dict_str.pop_back();
        }

        if (dict_str == clamp_word) {
            return (uint16_t)entry_addr; 
        } else if (dict_str < clamp_word) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    
    return 0; 
}
