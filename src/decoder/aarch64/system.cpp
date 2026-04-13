/// @file system.cpp
/// @brief AArch64 system instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/system.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;

// =====================================================================
// decode_sys_reg  (MRS / MSR)
// =====================================================================

Result<DecodedInstruction> decode_sys_reg(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool l = ((raw >> 21) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t crm = decode_crm(raw);
    uint8_t crn = decode_crn(raw);
    uint8_t op1 = decode_op1(raw);
    uint8_t op2 = decode_op2(raw);

    d.opcode = l ? OpcodeType::Mrs : OpcodeType::Msr;

    if (rt != 31) {
        if (l) d.dst_regs.push_back(Reg(rt));
        else   d.src_regs.push_back(Reg(rt));
    }

    char sysreg[32];
    std::snprintf(sysreg, sizeof(sysreg), "s%u_%u_c%u_c%u", op1, crn, crm, op2);

    char buf[80];
    if (l) {
        std::snprintf(buf, sizeof(buf), "mrs x%u, %s", rt, sysreg);
    } else {
        std::snprintf(buf, sizeof(buf), "msr %s, x%u", sysreg, rt);
    }
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_sys_instr  (SYS / SYSL)
// =====================================================================

Result<DecodedInstruction> decode_sys_instr(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool l = ((raw >> 21) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t crm = decode_crm(raw);
    uint8_t crn = decode_crn(raw);
    uint8_t op1 = decode_op1(raw);
    uint8_t op2 = decode_op2(raw);

    d.opcode = OpcodeType::Sys;

    const char* mnemonic = l ? "sysl" : "sys";
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%s #%u, #%u, #%u, #%u, x%u",
                  mnemonic, op1, crn, crm, op2, rt);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_hint
// =====================================================================

Result<DecodedInstruction> decode_hint(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t crm = decode_crm(raw);
    uint8_t op2 = decode_op2(raw);
    uint8_t imm = (crm << 3) | op2;

    d.opcode = OpcodeType::Nop;

    const char* mnemonic = (imm == 0) ? "nop" : (imm == 1) ? "yield" :
                           (imm == 2) ? "wfe" : (imm == 3) ? "wfi" :
                           (imm == 4) ? "sev" : (imm == 5) ? "sevl" : "hint";

    d.disasm = mnemonic;

    return d;
}

// =====================================================================
// decode_barrier  (DMB / DSB / ISB)
// =====================================================================

Result<DecodedInstruction> decode_barrier(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t crm = decode_crm(raw);
    uint8_t op2 = decode_op2(raw);
    uint8_t rt = decode_rt(raw);

    // Determine opcode
    if ((crm == 0b0010 && op2 == 0b100) || (crm == 0b0011 && op2 == 0b100)) {
        d.opcode = OpcodeType::Dmb;
    } else if ((crm == 0b0010 && op2 == 0b101) || (crm == 0b0011 && op2 == 0b101)) {
        d.opcode = OpcodeType::Dsb;
    } else if (crm == 0b0010 && op2 == 0b110) {
        d.opcode = OpcodeType::Isb;
    } else {
        d.opcode = OpcodeType::Dmb;
    }

    // Domain
    const char* domain;
    switch (rt) {
        case 0b1111: domain = "sy";  break;
        case 0b1110: domain = "ish"; break;
        case 0b1101: domain = "nsh"; break;
        case 0b1011: domain = "osh"; break;
        default: {
            static char buf[8];
            std::snprintf(buf, sizeof(buf), "#%u", rt);
            domain = buf;
            break;
        }
    }

    // Mnemonic
    const char* mnemonic;
    if ((crm == 0b0010 && op2 == 0b100) || (crm == 0b0011 && op2 == 0b100)) {
        mnemonic = "dmb";
    } else if ((crm == 0b0010 && op2 == 0b101) || (crm == 0b0011 && op2 == 0b101)) {
        mnemonic = "dsb";
    } else if (crm == 0b0010 && op2 == 0b110) {
        mnemonic = "isb";
    } else {
        mnemonic = "barrier";
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %s", mnemonic, domain);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_dc  (data cache maintenance)
// =====================================================================

Result<DecodedInstruction> decode_dc(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t crm = decode_crm(raw);
    uint8_t op2 = decode_op2(raw);
    uint8_t rt = decode_rt(raw);

    // Opcode
    if (crm == 0b0001 && op2 == 0b000) d.opcode = OpcodeType::DcCivac;
    else if (crm == 0b0001 && op2 == 0b001) d.opcode = OpcodeType::DcCivac;
    else if (crm == 0b0001 && op2 == 0b010) d.opcode = OpcodeType::DcCivac;
    else if (crm == 0b0001 && op2 == 0b011) d.opcode = OpcodeType::DcCsw;
    else if (crm == 0b0010 && op2 == 0b000) d.opcode = OpcodeType::DcCvac;
    else if (crm == 0b0010 && op2 == 0b001) d.opcode = OpcodeType::DcCvac;
    else if (crm == 0b0010 && op2 == 0b010) d.opcode = OpcodeType::DcCivac;
    else if (crm == 0b0011 && op2 == 0b000) d.opcode = OpcodeType::DcZva;
    else d.opcode = OpcodeType::DcCvac;

    if (rt != 31) d.src_regs.push_back(Reg(rt));

    // Mnemonic
    const char* mnemonic;
    if (crm == 0b0001 && op2 == 0b000) mnemonic = "dc ivac";
    else if (crm == 0b0001 && op2 == 0b001) mnemonic = "dc isw";
    else if (crm == 0b0001 && op2 == 0b010) mnemonic = "dc csw";
    else if (crm == 0b0001 && op2 == 0b011) mnemonic = "dc cisw";
    else if (crm == 0b0010 && op2 == 0b000) mnemonic = "dc cvac";
    else if (crm == 0b0010 && op2 == 0b001) mnemonic = "dc cvau";
    else if (crm == 0b0010 && op2 == 0b010) mnemonic = "dc civac";
    else if (crm == 0b0011 && op2 == 0b000) mnemonic = "dc zva";
    else mnemonic = "dc";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, x%u", mnemonic, rt);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_ic  (instruction cache maintenance)
// =====================================================================

Result<DecodedInstruction> decode_ic(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t crm = decode_crm(raw);
    uint8_t op2 = decode_op2(raw);
    uint8_t rt = decode_rt(raw);

    // Opcode
    if (crm == 0b0001 && op2 == 0b000) d.opcode = OpcodeType::IcIalluis;
    else if (crm == 0b0001 && op2 == 0b001) d.opcode = OpcodeType::IcIallu;
    else if (crm == 0b0010 && op2 == 0b001) d.opcode = OpcodeType::IcIvau;
    else d.opcode = OpcodeType::IcIvau;

    if (rt != 31) d.src_regs.push_back(Reg(rt));

    // Mnemonic
    const char* mnemonic;
    if (crm == 0b0001 && op2 == 0b000) mnemonic = "ic ialluis";
    else if (crm == 0b0001 && op2 == 0b001) mnemonic = "ic iallu";
    else if (crm == 0b0010 && op2 == 0b001) mnemonic = "ic ivau";
    else mnemonic = "ic";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s, x%u", mnemonic, rt);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_exception_return  (ERET)
// =====================================================================

Result<DecodedInstruction> decode_exception_return(uint64_t pc, uint32_t raw) {
    (void)raw;
    DecodedInstruction d(pc, raw);

    d.opcode = OpcodeType::Sys;
    d.disasm = "eret";

    return d;
}

// =====================================================================
// decode_exception_gen  (SVC / HVC / SMC / BRK)
// =====================================================================

Result<DecodedInstruction> decode_exception_gen(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t opc = (raw >> 21) & 0x3;
    uint16_t imm16 = static_cast<uint16_t>((raw >> 5) & 0xFFFF);

    d.opcode = (opc == 0 || opc == 1 || opc == 2) ? OpcodeType::Sys : OpcodeType::Nop;

    const char* mnemonic = (opc == 0) ? "svc" : (opc == 1) ? "hvc" :
                           (opc == 2) ? "smc" : "brk";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s #%u", mnemonic, imm16);
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
