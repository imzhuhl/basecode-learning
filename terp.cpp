#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include "fmt/core.h"
#include "terp.h"
#include "hex_formatter.h"

namespace basecode {

    terp::terp(size_t heap_size) : _heap_size(heap_size) {
    }

    terp::~terp() {
        delete _heap;
        _heap = nullptr;
    }

    void terp::reset() {
        _registers.pc = 0;
        _registers.fr = 0;
        _registers.sr = 0;
        _registers.sp = _heap_size;

        for (size_t i = 0; i < 64; i++) {
            _registers.i[i] = 0;
            _registers.f[i] = 0.0;
        }

        _exited = false;
    }

    bool terp::initialize() {
        _heap = new uint8_t[_heap_size];
        reset();
        return true;
    }

    void terp::push(uint64_t value) {
        _registers.sp -= sizeof(uint64_t);
        *qword_ptr(_registers.sp) = value;
   }

    uint64_t terp::pop() {
        uint64_t value = *qword_ptr(_registers.sp);
        _registers.sp += sizeof(uint64_t);
        return value;
    }

    const register_file_t& terp::register_file() const {
        return _registers;
    }

    void terp::dump_state() {
        fmt::print("Basecode Interpreter State\n");
        fmt::print("-----------------------------------------------------------------\n");
        fmt::print(
            "I0=${:08x} | I1=${:08x} | I2=${:08x} | I3=${:08x}\n",
            _registers.i[0],
            _registers.i[1],
            _registers.i[2],
            _registers.i[3]);

        fmt::print(
            "I4=${:08x} | I5=${:08x} | I6=${:08x} | I7=${:08x}\n",
            _registers.i[4],
            _registers.i[5],
            _registers.i[6],
            _registers.i[7]);

        fmt::print("\n");

        fmt::print(
            "PC=${:08x} | SP=${:08x} | FR=${:08x} | SR=%$==${:08x}\n\n",
            _registers.pc,
            _registers.sp,
            _registers.fr,
            _registers.sr);
    }

    void terp::dump_heap(uint64_t offset, size_t size) {
        std::string program_memory = basecode::hex_formatter::dump_to_string(
            reinterpret_cast<const void*>(_heap + offset),
            size);
        fmt::print("{}\n", program_memory);
    }

    size_t terp::encode_instruction(result& r, uint64_t address, instruction_t instruction) {
        if (address % 8 != 0) {
            r.add_message("B003", "Instructions must be encoded on 8-byte boundaries.", true);
            return 0;
        }

        uint8_t size = 4;

        uint16_t* op_ptr = word_ptr(address + 1);
        *op_ptr = static_cast<uint16_t>(instruction.op);

        uint8_t* encoding_ptr = byte_ptr(address);
        *(encoding_ptr + 3) = static_cast<uint8_t>(instruction.size);
        *(encoding_ptr + 4) = instruction.operands_count;

        size_t offset = 5;
        for (size_t i = 0; i < instruction.operands_count; ++i) {
            *(encoding_ptr + offset) = static_cast<uint8_t>(instruction.operands[i].type);
            ++offset;
            ++size;

            *(encoding_ptr + offset) = instruction.operands[i].index;
            ++offset;
            ++size;

            switch (instruction.operands[i].type) {
                case operand_types::register_integer:
                case operand_types::register_floating_point:
                case operand_types::register_sp:
                case operand_types::register_pc:
                case operand_types::register_flags:
                case operand_types::register_status:
                case operand_types::increment_register_pre:
                case operand_types::increment_register_post:
                case operand_types::decrement_register_pre:
                case operand_types::decrement_register_post:
                    break;
                case operand_types::increment_constant_pre:
                case operand_types::increment_constant_post:
                case operand_types::decrement_constant_pre:
                case operand_types::decrement_constant_post:
                case operand_types::constant_integer: {
                    uint64_t* constant_value_ptr = reinterpret_cast<uint64_t*>(encoding_ptr + offset);
                    *constant_value_ptr = instruction.operands[i].value.u64;
                    offset += sizeof(uint64_t);
                    size += sizeof(uint64_t);
                    break;
                }
                case operand_types::constant_float:
                    double* constant_value_ptr = reinterpret_cast<double*>(encoding_ptr + offset);
                    *constant_value_ptr = instruction.operands[i].value.d64;
                    offset += sizeof(double);
                    size += sizeof(double);
                    break;
            }
        }

        if (instruction.operands_count > 0)
            ++size;

        if (size < 8)
            size = 8;
        
        size = static_cast<uint8_t>(align(size, sizeof(uint64_t)));
        *encoding_ptr = size;

        return size;

    }

    size_t terp::decode_instruction(result& r, instruction_t& instruction) {
        if (_registers.pc % 8 != 0) {
            r.add_message("B003", "Instructions must be decoded on 8-byte boundaries.", true);
            return 0;
        }

        uint16_t* op_ptr = word_ptr(_registers.pc + 1);
        instruction.op = static_cast<op_codes>(*op_ptr);

        uint8_t* encoding_ptr = byte_ptr(_registers.pc);
        uint8_t size = *encoding_ptr;
        instruction.size = static_cast<op_sizes>(static_cast<uint8_t>(*(encoding_ptr + 3)));
        instruction.operands_count = static_cast<uint8_t>(*(encoding_ptr + 4));

        size_t offset = 5;
        for (size_t i = 0; i < instruction.operands_count; i++) {
            instruction.operands[i].type = static_cast<operand_types>(*(encoding_ptr + offset));
            ++offset;

            instruction.operands[i].index = *(encoding_ptr + offset);
            ++offset;

            switch (instruction.operands[i].type) {
                case operand_types::register_sp:
                case operand_types::register_pc:
                case operand_types::register_flags:
                case operand_types::register_status:
                case operand_types::register_integer:
                case operand_types::increment_register_pre:
                case operand_types::decrement_register_pre:
                case operand_types::increment_register_post:
                case operand_types::decrement_register_post:
                case operand_types::register_floating_point:
                    break;
                case operand_types::increment_constant_pre:
                case operand_types::decrement_constant_pre:
                case operand_types::increment_constant_post:
                case operand_types::decrement_constant_post:
                case operand_types::constant_integer: {
                    uint64_t* constant_value_ptr = reinterpret_cast<uint64_t*>(encoding_ptr + offset);
                    instruction.operands[i].value.u64 = *constant_value_ptr;
                    offset += sizeof(uint64_t);
                    break;
                }
                case operand_types::constant_float: {
                    double* constant_value_ptr = reinterpret_cast<double*>(encoding_ptr + offset);
                    instruction.operands[i].value.d64 = *constant_value_ptr;
                    offset += sizeof(double);
                    break;
                }
            }
        }

        _registers.pc += size;

        return size;
    }

    size_t terp::align(uint64_t value, size_t size) const {
        auto offset = value % size;
        return offset ? value + (size - offset) : value;
    }

    bool terp::has_exited() const {
        return _exited;
    }

    bool terp::step(result& r) {
        instruction_t inst;
        auto inst_size = decode_instruction(r, inst);
        if (inst_size == 0)
            return false;
        
        switch (inst.op) {
            case op_codes::nop: {
                fmt::print("nop\n");
                break;
            }
            case op_codes::load: {
                fmt::print("load\n");
                uint64_t address;
                if (!get_operand_value(r, inst, 1, address))
                    return false;
                if (inst.operands_count > 2) {
                    uint64_t offset;
                    if (!get_operand_value(r, inst, 2, offset))
                        return false;
                    address += offset;
                }
                uint64_t value = *qword_ptr(address);
                if (!set_target_operand_value(r, inst, 0, value))
                    return false;
                break;
            }
            case op_codes::store: {
                fmt::print("store\n");
                uint64_t value;
                if (!get_operand_value(r, inst, 0, value))
                    return false;

                uint64_t address;
                if (!get_operand_value(r, inst, 1, address))
                    return false;
                if (inst.operands_count > 2) {
                    uint64_t offset;
                    if (!get_operand_value(r, inst, 2, offset))
                        return false;
                    address += offset;
                }

                *qword_ptr(address) = value;
                break;
            }
            case op_codes::move: {
                fmt::print("move\n");
                uint64_t source_value;
                if (!get_operand_value(r, inst, 0, source_value))
                    return false;
                if (!set_target_operand_value(r, inst, 1, source_value))
                    return false;
                break;
            }
            case op_codes::push: {
                fmt::print("push\n");
                uint64_t source_value;
                if (!get_operand_value(r, inst, 0, source_value))
                    return false;
                push(source_value);
                break;
            }
            case op_codes::pop: {
                fmt::print("pop\n");
                uint64_t value = pop();
                if (!set_target_operand_value(r, inst, 0, value))
                    return false;
                break;
            }
            case op_codes::add: {
                fmt::print("add\n");
                uint64_t lhs_value, rhs_value;
                if (!get_operand_value(r, inst, 1, lhs_value))
                    return false;
                if (!get_operand_value(r, inst, 2, rhs_value))
                    return false;
                if (!set_target_operand_value(r, inst, 0, lhs_value + rhs_value))
                    return false;
                break;
            }
            case op_codes::sub: {
                fmt::print("sub\n");
                uint64_t lhs_value, rhs_value;
                if (!get_operand_value(r, inst, 1, lhs_value))
                    return false;
                if (!get_operand_value(r, inst, 2, rhs_value))
                    return false;
                if (!set_target_operand_value(r, inst, 0, lhs_value - rhs_value))
                    return false;
                break;
            }
            case op_codes::mul: {
                fmt::print("mul\n");
                uint64_t lhs_value, rhs_value;
                if (!get_operand_value(r, inst, 1, lhs_value))
                    return false;
                if (!get_operand_value(r, inst, 2, rhs_value))
                    return false;
                if (!set_target_operand_value(r, inst, 0, lhs_value * rhs_value))
                    return false;
                break;
            }
            case op_codes::div: {
                fmt::print("div\n");
                uint64_t lhs_value, rhs_value;
                if (!get_operand_value(r, inst, 1, lhs_value))
                    return false;
                if (!get_operand_value(r, inst, 2, rhs_value))
                    return false;
                uint64_t result = 0;
                if (rhs_value != 0)
                    result = lhs_value / rhs_value;
                if (!set_target_operand_value(r, inst, 0, result))
                    return false;
                break;
            }
            case op_codes::mod: {
                fmt::print("mod\n");
                uint64_t lhs_value, rhs_value;
                if (!get_operand_value(r, inst, 1, lhs_value))
                    return false;
                if (!get_operand_value(r, inst, 2, rhs_value))
                    return false;
                if (!set_target_operand_value(r, inst, 0, lhs_value % rhs_value))
                    return false;
                break;
            }
            case op_codes::neg: {
                break;
            }
            case op_codes::shr: {
                break;
            }
            case op_codes::shl: {
                break;
            }
            case op_codes::ror: {
                break;
            }
            case op_codes::rol: {
                break;
            }
            case op_codes::and_op: {
                break;
            }
            case op_codes::or_op: {
                break;
            }
            case op_codes::xor_op: {
                break;
            }
            case op_codes::not_op: {
                break;
            }
            case op_codes::bis: {
                break;
            }
            case op_codes::bic: {
                break;
            }
            case op_codes::test: {
                break;
            }
            case op_codes::cmp: {
                break;
            }
            case op_codes::bz: {
                break;
            }
            case op_codes::bnz: {
                break;
            }
            case op_codes::tbz: {
                break;
            }
            case op_codes::tbnz: {
                break;
            }
            case op_codes::bne: {
                break;
            }
            case op_codes::beq: {
                break;
            }
            case op_codes::bae: {
                break;
            }
            case op_codes::ba: {
                break;
            }
            case op_codes::ble: {
                break;
            }
            case op_codes::bl: {
                break;
            }
            case op_codes::bo: {
                break;
            }
            case op_codes::bcc: {
                break;
            }
            case op_codes::bcs: {
                break;
            }
            case op_codes::jsr: {
                fmt::print("jsr\n");
                push(_registers.pc);
                uint64_t address;
                if (!get_operand_value(r, inst, 0, address))
                    return false;
                _registers.pc = address;
                break;
            }
            case op_codes::rts: {
                fmt::print("rts\n");
                uint64_t address = pop();
                _registers.pc = address;
                break;
            }
            case op_codes::jmp: {
                fmt::print("jmp\n");
                uint64_t address;
                if (!get_operand_value(r, inst, 0, address))
                    return false;
                _registers.pc = address;
                break;
            }
            case op_codes::meta: {
                break;
            }
            case op_codes::debug: {
                break;
            }
            case op_codes::exit: {
                fmt::print("exit\n");
                _exited = true;
                break;
            }
        }
        return !r.is_failed();
    }

    bool terp::get_operand_value(result& r, const instruction_t& instruction, uint8_t operand_index,
                                 uint64_t& value) const {

        switch (instruction.operands[operand_index].type) {
            case operand_types::increment_register_pre:
            case operand_types::decrement_register_pre:
            case operand_types::increment_register_post:
            case operand_types::decrement_register_post:
            case operand_types::register_integer: {
                value = _registers.i[instruction.operands[operand_index].index];
                break;
            }
            case operand_types::register_floating_point: {
                value = static_cast<uint64_t>(_registers.f[instruction.operands[operand_index].index]);
                break;
            }
            case operand_types::register_sp: {
                value = _registers.sp;
                break;
            }
            case operand_types::register_pc: {
                value = _registers.pc;
                break;
            }
            case operand_types::register_flags: {
                value = _registers.fr;
                break;
            }
            case operand_types::register_status: {
                value = _registers.sr;
                break;
            }
            case operand_types::increment_constant_pre:
            case operand_types::decrement_constant_pre:
            case operand_types::increment_constant_post:
            case operand_types::decrement_constant_post:
            case operand_types::constant_integer: {
                value = instruction.operands[operand_index].value.u64;
                break;
            }
            case operand_types::constant_float: {
                value = static_cast<uint64_t>(instruction.operands[operand_index].value.d64);
                break;
            }
        }

        switch (instruction.size) {
            case op_sizes::byte:
                break;
            case op_sizes::word:
                break;
            case op_sizes::dword:
                break;
            case op_sizes::qword:
                break;
            default: {
                r.add_message("B005", "unsupported size of 'none' for operand.", true);
                return false;
            }
        }

        return true;
    }

    bool terp::get_operand_value(result& r, const instruction_t& instruction, uint8_t operand_index,
                                 double& value) const {
        switch (instruction.operands[operand_index].type) {
            case operand_types::increment_register_pre:
            case operand_types::decrement_register_pre:
            case operand_types::increment_register_post:
            case operand_types::decrement_register_post:
            case operand_types::register_floating_point: {
                value = _registers.f[instruction.operands[operand_index].index];
                break;
            }
            case operand_types::register_sp:
            case operand_types::register_pc:
            case operand_types::register_flags:
            case operand_types::register_status:
            case operand_types::register_integer: {
                r.add_message(
                        "B005",
                        "integer registers cannot be used for floating point operands.",
                        true);
                break;
            }
            case operand_types::increment_constant_pre:
            case operand_types::decrement_constant_pre:
            case operand_types::increment_constant_post:
            case operand_types::decrement_constant_post:
            case operand_types::constant_integer: {
                value = instruction.operands[operand_index].value.u64;
                break;
            }
            case operand_types::constant_float: {
                value = instruction.operands[operand_index].value.d64;
                break;
            }
        }

        return true;

    }
    
    bool terp::set_target_operand_value(result& r, const instruction_t& instruction, uint8_t operand_index,
                                        uint64_t value) {
        switch (instruction.operands[operand_index].type) {
            case operand_types::increment_register_pre:
            case operand_types::decrement_register_pre:
            case operand_types::increment_register_post:
            case operand_types::decrement_register_post:
            case operand_types::register_integer: {
                _registers.i[instruction.operands[operand_index].index] = value;
                return true;
            }
            case operand_types::register_floating_point:
                _registers.f[instruction.operands[operand_index].index] = value;
                break;
            case operand_types::register_sp: {
                _registers.sp = value;
                break;
            }
            case operand_types::register_pc: {
                _registers.pc = value;
                break;
            }
            case operand_types::register_flags: {
                _registers.fr = value;
                break;
            }
            case operand_types::register_status: {
                _registers.sr = value;
                break;
            }
            case operand_types::constant_float:
            case operand_types::constant_integer:
            case operand_types::increment_constant_pre:
            case operand_types::decrement_constant_pre:
            case operand_types::increment_constant_post:
            case operand_types::decrement_constant_post: {
                r.add_message(
                        "B006",
                        "constant cannot be a target operand type.",
                        true);
                break;
            }
        }

        return false;
    }

    bool terp::set_target_operand_value(result& r, const instruction_t& instruction, uint8_t operand_index,
                                        double value) {
        switch (instruction.operands[operand_index].type) {
            case operand_types::increment_register_pre:
            case operand_types::decrement_register_pre:
            case operand_types::increment_register_post:
            case operand_types::decrement_register_post:
            case operand_types::register_integer: {
                _registers.i[instruction.operands[operand_index].index] = static_cast<uint64_t>(value);
                break;
            }
            case operand_types::register_floating_point: {
                _registers.f[instruction.operands[operand_index].index] = value;
                break;
            }
            case operand_types::register_sp: {
                _registers.sp = static_cast<uint64_t>(value);
                break;
            }
            case operand_types::register_pc: {
                _registers.pc = static_cast<uint64_t>(value);
                break;
            }
            case operand_types::register_flags: {
                _registers.fr = static_cast<uint64_t>(value);
                break;
            }
            case operand_types::register_status: {
                _registers.sr = static_cast<uint64_t>(value);
                break;
            }
            case operand_types::constant_float:
            case operand_types::constant_integer:
            case operand_types::increment_constant_pre:
            case operand_types::increment_constant_post:
            case operand_types::decrement_constant_pre:
            case operand_types::decrement_constant_post: {
                r.add_message(
                        "B006",
                        "constant cannot be a target operand type.",
                        true);
                break;
            }
        }

        return false;
    }





}
