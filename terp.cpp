#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include "fmt/core.h"
#include "terp.h"
#include "hex_formatter.h"

namespace basecode {
    terp::terp(uint32_t heap_size) : _heap_size(heap_size) {
    }

    terp::~terp() {
        delete _heap;
        _heap = nullptr;
    }

    bool terp::initialize() {
        _heap = new uint64_t[heap_size_in_qwords()];
        _registers.pc = 0;
        _registers.fr = 0;
        _registers.sr = 0;
        _registers.sp = heap_size_in_qwords();

        for (size_t i = 0; i < 64; i++) {
            _registers.i[i] = 0;
            _registers.f[i] = 0.0;
        }
        return true;
    }

    void terp::push(uint64_t value) {
        _registers.sp -= sizeof(uint64_t);
        _heap[_registers.sp] = value;
        return;
    }

    uint64_t terp::pop() {
        uint64_t value = _heap[_registers.sp];
        _registers.sp -= sizeof(uint64_t);
        return value;
    }

    size_t terp::heap_size() const {
        return _heap_size;
    }
    size_t terp::heap_size_in_qwords() const {
        return _heap_size / sizeof(uint64_t);
    }
    const register_file_t& terp::register_file() const {
        return _registers;
    }

    void terp::dump_state() {
        fmt::print("Basecode Interpreter State\n");
        fmt::print("-----------------------------------------------------------------\n");
        fmt::print(
            "I0={:08x} | I1={:08x} | I2={:08x} | I3={:08x}\n",
            _registers.i[0],
            _registers.i[1],
            _registers.i[2],
            _registers.i[3]);

        fmt::print(
            "I4={:08x} | I5={:08x} | I6={:08x} | I7={:08x}\n",
            _registers.i[4],
            _registers.i[5],
            _registers.i[6],
            _registers.i[7]);

        fmt::print("\n");

        fmt::print(
            "PC={:08x} | SP={:08x} | FR={:08x} | SR={:08x}\n\n",
            _registers.pc,
            _registers.sp,
            _registers.fr,
            _registers.sr);
    }

    void terp::dump_heap(uint64_t address, size_t size) {
        std::string program_memory = basecode::hex_formatter::dump_to_string(
            reinterpret_cast<const void*>(_heap),
            size);
        fmt::print("{}\n", program_memory);
    }

    size_t terp::encode_instruction(result& r, uint64_t address, instruction_t instruction) {
        if (address % 8 != 0) {
            r.add_message("B003", "Instructions must be encoded on 8-byte boundaries.", true);
            return 0;
        }

        auto qword_address = address / sizeof(uint64_t);
        uint8_t size = 4;

        uint8_t* encoding_ptr = reinterpret_cast<uint8_t*>(_heap + qword_address);
        uint16_t* op_ptr = reinterpret_cast<uint16_t*>(encoding_ptr + 1);
        *op_ptr = static_cast<uint16_t>(instruction.op);

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

            if (instruction.operands[i].type == operand_types::constant) {
                uint64_t* constant_value_ptr = reinterpret_cast<uint64_t*>(encoding_ptr + offset);
                *constant_value_ptr = instruction.operands[i].value;
                offset += sizeof(uint64_t);
                size += sizeof(uint64_t);
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

        uint8_t* encoding_ptr = reinterpret_cast<uint8_t*>(_heap + (_registers.pc / sizeof(uint64_t)));
        uint8_t size = *encoding_ptr;

        uint16_t* op_ptr = reinterpret_cast<uint16_t*>(encoding_ptr + 1);
        instruction.op = static_cast<op_codes>(*op_ptr);
        instruction.size = static_cast<op_sizes>(static_cast<uint8_t>(*(encoding_ptr + 3)));
        instruction.operands_count = static_cast<uint8_t>(*(encoding_ptr + 4));

        size_t offset = 5;
        for (size_t i = 0; i < instruction.operands_count; i++) {
            instruction.operands[i].type = static_cast<operand_types>(*(encoding_ptr + offset));
            ++offset;

            instruction.operands[i].index = *(encoding_ptr + offset);
            ++offset;

            instruction.operands[i].value = 0;
            if (instruction.operands[i].type == operand_types::constant) {
                uint64_t* constant_value_ptr = reinterpret_cast<uint64_t*>(encoding_ptr + offset);
                instruction.operands[i].value = *constant_value_ptr;
                offset += sizeof(uint64_t);
            }
        }

        _registers.pc += size;

        return size;
    }


    size_t terp::align(uint64_t value, size_t size) const {
        auto offset = value % size;
        return offset ? value + (size - offset) : value;
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
            case op_codes::load:
                break;
            case op_codes::store:
                break;
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
                break;
            }
            case op_codes::pop: {
                fmt::print("pop\n");
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
            case op_codes::sub:
            case op_codes::mul:
            case op_codes::div:
            case op_codes::mod:
            case op_codes::neg:
            case op_codes::shr:
            case op_codes::shl:
            case op_codes::ror:
            case op_codes::rol:
            case op_codes::and_op:
            case op_codes::or_op:
            case op_codes::xor_op:
            case op_codes::not_op:
            case op_codes::bis:
            case op_codes::bic:
            case op_codes::test:
            case op_codes::cmp:
            case op_codes::bz:
            case op_codes::bnz:
            case op_codes::tbz:
            case op_codes::tbnz:
            case op_codes::bne:
            case op_codes::beq:
            case op_codes::bae:
            case op_codes::ba:
            case op_codes::ble:
            case op_codes::bl:
            case op_codes::bo:
            case op_codes::bcc:
            case op_codes::bcs:
            case op_codes::jsr:
            case op_codes::ret:
            case op_codes::jmp:
            case op_codes::meta:
            case op_codes::debug:
            break;
        }
        return !r.is_failed();
    }

    bool terp::get_operand_value(
            result& r,
            const instruction_t& instruction,
            uint8_t operand_index,
            uint64_t& value) const {

        switch (instruction.operands[operand_index].type) {
            case operand_types::register_integer:
                value = _registers.i[instruction.operands[operand_index].index];
                break;
            case operand_types::register_floating_point:
                break;
            case operand_types::register_sp:
                value = _registers.sp;
                break;
            case operand_types::register_pc:
                value = _registers.pc;
                break;
            case operand_types::register_flags:
                value = _registers.fr;
                break;
            case operand_types::register_status:
                value = _registers.sr;
                break;
            case operand_types::constant:
                value = instruction.operands[operand_index].value;
                break;
        }

        return true;
    }

    bool terp::get_operand_value( 
            result& r,
            const instruction_t& instruction,
            uint8_t operand_index,
            double& value) const {
        switch (instruction.operands[operand_index].type) {
            case operand_types::register_integer:
                break;
            case operand_types::register_floating_point: {
                value = _registers.f[instruction.operands[operand_index].index];
                break;
            }
            case operand_types::register_sp:
            case operand_types::register_pc:
            case operand_types::register_flags:
            case operand_types::register_status: {
                r.add_message(
                    "B005",
                    "integer registers cannot be used for floating point operands.",
                    true);
                break;
            }
            case operand_types::constant: {
                value = instruction.operands[operand_index].value;
                break;
            }
        }
        return true;
    }
    
    bool terp::set_target_operand_value(
            result& r,
            const instruction_t& instruction,
            uint8_t operand_index,
            uint64_t value) {
        switch (instruction.operands[operand_index].type) {
            case operand_types::register_integer:
                _registers.i[instruction.operands[operand_index].index] = value;
                return true;
            case operand_types::register_floating_point:
                r.add_message(
                    "B009",
                    "floating point registers cannot be the target for integer values.",
                    true);
                break;
            case operand_types::register_sp:
                break;
            case operand_types::register_pc:
                break;
            case operand_types::register_flags:
                break;
            case operand_types::register_status:
                break;
            case operand_types::constant:
                r.add_message(
                    "B006",
                    "constant cannot be a target operand type.",
                    true);
                break;
        }

        return false;
    }

}