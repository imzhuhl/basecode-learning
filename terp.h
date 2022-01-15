#pragma once

#include <cstdint>
#include <string>
#include <map>
#include "result.h"

namespace basecode {
    // basecode interpreter, which consumes base IR
    //
    //
    // register-based machine
    // with a generic stack
    //
    // register file:
    //
    // general purpose: data or address
    // I0-I63: integer registers 64-bit
    //
    // data only:
    // F0-F63: floating point registers (double precision)
    //
    // stack pointer: sp (like an IXX register)
    // program counter: pc (can be read, but not changed)
    // flags: fr (definitely read; maybe write)
    // status: sr (definitely read; maybe write)
    //
    //
    // instructions:
    //
    // memory access
    // --------------
    //
    // load{.b|.w|.dw|.qw}
    //                  ^ default
    // .b  = 8-bit
    // .w  = 16-bit
    // .dw = 32-bit
    // .qw = 64-bit
    //
    // non-used bits are zero-extended
    //
    // store{.b|.w|.dw|.qw}
    //
    // non-used bits are zero-extended
    //
    // addressing modes (loads & stores):
    //      {target-register}, [{source-register}]
    //          "      "     , [{source-register}, offset constant]
    //          "      "     , [{source-register}, {offset-register}]
    //          "      "     , {source-register}, post increment constant++
    //          "      "     , {source-register}, post increment register++
    //          "      "     , {source-register}, ++pre increment constant
    //          "      "     , {source-register}, ++pre increment register
    //          "      "     , {source-register}, post decrement constant--
    //          "      "     , {source-register}, post decrement register--
    //          "      "     , {source-register}, --pre decrement constant
    //          "      "     , {source-register}, --pre decrement register
    //
    // copy {source-register}, {target-register}, {length constant}
    // copy {source-register}, {target-register}, {length-register}
    //
    // fill {constant}, {target-register}, {length constant}
    // fill {constant}, {target-register}, {length-register}
    //
    // fill {source-register}, {target-register}, {length constant}
    // fill {source-register}, {target-register}, {length register}
    //
    // register/constant
    // -------------------
    //
    // move{.b|.w|.dw|.qw}  {source constant}, {target register}
    //                      {source register}, {target register}
    //
    // move.b #$06, I3
    // move I3, I16
    //
    // stack
    // --------
    //
    //  push{.b|.w|.dw|.qw}
    //  pop{.b|.w|.dw|.qw}
    //
    //  sp register behaves like IXX register.
    //
    // ALU
    // -----
    //
    //  size applicable to all: {.b|.w|.dw|.qw}
    //
    // add
    // addc
    //
    // sub
    // subc
    //
    // mul
    // div
    // mod
    // neg
    //
    // shr
    // shl
    // ror
    // rol
    //
    // and
    // or
    // xor
    //
    // not
    // bis (bit set)
    // bic (bit clear)
    // test
    //
    // cmp (compare register to register, or register to constant)
    //
    // branch/conditional execution
    // ----------------------------------
    //
    // bz   (branch if zero)
    // bnz  (branch if not-zero)
    //
    // tbz  (test & branch if not set)
    // tbnz (test & branch if set)
    //
    // bne
    // beq
    // bae
    // ba
    // ble
    // bl
    // bo
    // bcc
    // bcs
    //
    // jsr  - equivalent to call (encode tail flag?)
    //          push current PC + sizeof(instruction)
    //          jmp to address
    //
    // ret  - jump to address on stack
    //
    // jmp
    //      - absolute constant: jmp #$ffffffff0
    //      - indirect register: jmp [I4]
    //      - direct: jmp I4
    //
    // nop
    //

    struct register_file_t {
        uint64_t i[64];
        double f[64];
        uint64_t pc;
        uint64_t sp;
        uint64_t fr;
        uint64_t sr;
    };

    enum class op_codes : uint16_t {
        nop = 1,
        load,
        store,
        copy,
        fill,
        move,
        push,
        pop,
        inc,
        dec,
        add,
        sub,
        mul,
        div,
        mod,
        neg,
        shr,
        shl,
        ror,
        rol,
        and_op,
        or_op,
        xor_op,
        not_op,
        bis,
        bic,
        test,
        cmp,
        bz,
        bnz,
        tbz,
        tbnz,
        bne,
        beq,
        bae,
        ba,
        ble,
        bl,
        bo,
        bcc,
        bcs,
        jsr,
        rts,
        jmp,
        meta,
        debug,
        exit,
    };

    enum class op_sizes : uint8_t {
        none,
        byte,
        word,       // 2 bytes
        dword,      // 4 bytes 
        qword,      // 8 bytes
    };

    enum class operand_types : uint8_t {
        register_integer,
        register_floating_point,
        register_sp,
        register_pc,
        register_flags,
        register_status,
        constant_integer,
        constant_float,
        increment_constant_pre,
        increment_constant_post,
        increment_register_pre,
        increment_register_post,
        decrement_constant_pre,
        decrement_constant_post,
        decrement_register_pre,
        decrement_register_post,
    };

    struct operand_encoding_t {
        operand_types type = operand_types::register_integer;
        uint8_t index;
        union {
            uint64_t u64;
            double d64;
        } value;
    };

    struct instruction_t {
        size_t align(uint64_t value, size_t size) const {
            auto offset = value % size;
            return offset ? value + (size - offset) : value;
        }

        size_t encoding_size() const {
            size_t size = 5;
            for (size_t i = 0; i < operands_count; ++i) {
                size += 2;
                switch (operands[i].type) {
                    case operand_types::increment_constant_pre:
                    case operand_types::increment_constant_post:
                    case operand_types::decrement_constant_pre:
                    case operand_types::decrement_constant_post:
                    case operand_types::constant_integer: {
                        size += sizeof(uint64_t);
                        break;
                    }
                    case operand_types::constant_float: {
                        size += sizeof(double);
                        break;
                    }
                    default:
                        break;
                }
            }

            if (size < 8)
                size = 8;

            size = static_cast<uint8_t>(align(size, sizeof(uint64_t)));
            return size;
        }

        void patch_branch_address(uint64_t address) {
            operands[0].value.u64 = address;
        }

        op_codes op = op_codes::nop;
        op_sizes size = op_sizes::none;
        uint8_t operands_count = 0;
        operand_encoding_t operands[4];
    };

    struct debug_information_t {
        uint32_t line_number;
        uint16_t column_number;
        std::string symbol;
        std::string source_file;
    };


    class terp {
    public:
        explicit terp(size_t heap_size);  // `heap_size` Bytes

        virtual ~terp();

        void reset();

        uint64_t pop();

        void dump_state();

        bool step(result& r);

        bool has_exited() const;

        size_t encode_instruction(result& r, uint64_t address, instruction_t instruction);

        size_t heap_size() const;

        void push(uint64_t value);

        bool initialize();

        const register_file_t& register_file() const;

        void dump_heap(uint64_t offset, size_t size = 256);

        std::string disassemble(result& r, uint64_t address);

        std::string disassemble(const instruction_t& inst) const;

    protected:
        size_t decode_instruction(result& r, uint64_t address, instruction_t& instruction);

        bool set_target_operand_value(
                result& r, const instruction_t& instruction, uint8_t operand_index, uint64_t value);

        bool set_target_operand_value(
                result& r, const instruction_t& instruction, uint8_t operand_index, double value);

        bool get_operand_value(
                result& r, const instruction_t& instruction, uint8_t operand_index, uint64_t& value) const;

        bool get_operand_value(
                result& r, const instruction_t& instruction, uint8_t operand_index, double& value) const;

        size_t align(uint64_t value, size_t size) const;

    private:
        inline uint8_t* byte_ptr(uint64_t address) const {
            return _heap + address;
        }
        inline uint16_t* word_ptr(uint64_t address) const {
            return reinterpret_cast<uint16_t*>(_heap + address);
        }
        inline uint32_t* dword_ptr(uint64_t address) const {
            return reinterpret_cast<uint32_t*>(_heap + address);
        }
        inline uint64_t* qword_ptr(uint64_t address) const {
            return reinterpret_cast<uint64_t*>(_heap + address);
        }


    private:
        inline static std::map<op_codes, std::string> s_op_code_names = {
            {op_codes::nop,    "NOP"},
            {op_codes::load,   "LOAD"},
            {op_codes::store,  "STORE"},
            {op_codes::copy,   "COPY"},
            {op_codes::fill,   "FILL"},
            {op_codes::move,   "MOVE"},
            {op_codes::push,   "PUSH"},
            {op_codes::pop,    "POP"},
            {op_codes::inc,    "INC"},
            {op_codes::dec,    "DEC"},
            {op_codes::add,    "ADD"},
            {op_codes::sub,    "SUB"},
            {op_codes::mul,    "MUL"},
            {op_codes::div,    "DIV"},
            {op_codes::mod,    "MOD"},
            {op_codes::neg,    "NEG"},
            {op_codes::shr,    "SHR"},
            {op_codes::shl,    "SHL"},
            {op_codes::ror,    "ROR"},
            {op_codes::rol,    "ROL"},
            {op_codes::and_op, "AND"},
            {op_codes::or_op,  "OR"},
            {op_codes::xor_op, "XOR"},
            {op_codes::not_op, "NOT"},
            {op_codes::bis,    "BIS"},
            {op_codes::bic,    "BIC"},
            {op_codes::test,   "TEST"},
            {op_codes::cmp,    "CMP"},
            {op_codes::bz,     "BZ"},
            {op_codes::bnz,    "BNZ"},
            {op_codes::tbz,    "TBZ"},
            {op_codes::tbnz,   "TBNZ"},
            {op_codes::bne,    "BNE"},
            {op_codes::beq,    "BEQ"},
            {op_codes::bae,    "BAE"},
            {op_codes::ba,     "BA"},
            {op_codes::ble,    "BLE"},
            {op_codes::bl,     "BL"},
            {op_codes::bo,     "BO"},
            {op_codes::bcc,    "BCC"},
            {op_codes::bcs,    "BCS"},
            {op_codes::jsr,    "JSR"},
            {op_codes::rts,    "RTS"},
            {op_codes::jmp,    "JMP"},
            {op_codes::meta,   "META"},
            {op_codes::debug,  "DEBUG"},
            {op_codes::exit,   "EXIT"},
        };
        bool _exited = false;
        size_t _heap_size = 0;
        uint8_t* _heap = nullptr;
        register_file_t _registers {};

    };



};

