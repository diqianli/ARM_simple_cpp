/// @file binary_format.cpp
/// @brief Binary trace format implementation.

#include "arm_cpu/trace/binary_format.hpp"

#include <algorithm>
#include <cstring>

namespace arm_cpu {

bool FileHeader::validate() const {
    if (magic != MAGIC) return false;
    if (version > VERSION) return false;
    return true;
}

uint8_t encode_opcode(OpcodeType opcode) {
    switch (opcode) {
        case OpcodeType::Add: return 0;
        case OpcodeType::Sub: return 1;
        case OpcodeType::Mul: return 2;
        case OpcodeType::Div: return 3;
        case OpcodeType::And: return 4;
        case OpcodeType::Orr: return 5;
        case OpcodeType::Eor: return 6;
        case OpcodeType::Lsl: return 7;
        case OpcodeType::Lsr: return 8;
        case OpcodeType::Asr: return 9;
        case OpcodeType::Load: return 10;
        case OpcodeType::Store: return 11;
        case OpcodeType::LoadPair: return 12;
        case OpcodeType::StorePair: return 13;
        case OpcodeType::Branch: return 14;
        case OpcodeType::BranchCond: return 15;
        case OpcodeType::BranchReg: return 16;
        case OpcodeType::Msr: return 17;
        case OpcodeType::Mrs: return 18;
        case OpcodeType::Sys: return 19;
        case OpcodeType::Nop: return 20;
        case OpcodeType::Fadd: return 21;
        case OpcodeType::Fsub: return 22;
        case OpcodeType::Fmul: return 23;
        case OpcodeType::Fdiv: return 24;
        case OpcodeType::DcZva: return 25;
        case OpcodeType::DcCivac: return 26;
        case OpcodeType::DcCvac: return 27;
        case OpcodeType::DcCsw: return 28;
        case OpcodeType::IcIvau: return 29;
        case OpcodeType::IcIallu: return 30;
        case OpcodeType::IcIalluis: return 31;
        case OpcodeType::Aesd: return 32;
        case OpcodeType::Aese: return 33;
        case OpcodeType::Aesimc: return 34;
        case OpcodeType::Aesmc: return 35;
        case OpcodeType::Sha1H: return 36;
        case OpcodeType::Sha256H: return 37;
        case OpcodeType::Sha512H: return 38;
        case OpcodeType::Vadd: return 39;
        case OpcodeType::Vsub: return 40;
        case OpcodeType::Vmul: return 41;
        case OpcodeType::Vmla: return 42;
        case OpcodeType::Vmls: return 43;
        case OpcodeType::Vld: return 44;
        case OpcodeType::Vst: return 45;
        case OpcodeType::Vdup: return 46;
        case OpcodeType::Vmov: return 47;
        case OpcodeType::Fmadd: return 48;
        case OpcodeType::Fmsub: return 49;
        case OpcodeType::Fnmadd: return 50;
        case OpcodeType::Fnmsub: return 51;
        case OpcodeType::Mov: return 52;
        case OpcodeType::Cmp: return 53;
        case OpcodeType::Shift: return 54;
        case OpcodeType::Fcvt: return 74;
        case OpcodeType::Dmb: return 75;
        case OpcodeType::Dsb: return 76;
        case OpcodeType::Isb: return 77;
        case OpcodeType::Eret: return 78;
        case OpcodeType::Yield: return 79;
        case OpcodeType::Adr: return 80;
        case OpcodeType::Pmull: return 81;
        case OpcodeType::Other: return 255;
    }
    return 255;
}

OpcodeType decode_opcode(uint8_t code) {
    switch (code) {
        case 0:  return OpcodeType::Add;
        case 1:  return OpcodeType::Sub;
        case 2:  return OpcodeType::Mul;
        case 3:  return OpcodeType::Div;
        case 4:  return OpcodeType::And;
        case 5:  return OpcodeType::Orr;
        case 6:  return OpcodeType::Eor;
        case 7:  return OpcodeType::Lsl;
        case 8:  return OpcodeType::Lsr;
        case 9:  return OpcodeType::Asr;
        case 10: return OpcodeType::Load;
        case 11: return OpcodeType::Store;
        case 12: return OpcodeType::LoadPair;
        case 13: return OpcodeType::StorePair;
        case 14: return OpcodeType::Branch;
        case 15: return OpcodeType::BranchCond;
        case 16: return OpcodeType::BranchReg;
        case 17: return OpcodeType::Msr;
        case 18: return OpcodeType::Mrs;
        case 19: return OpcodeType::Sys;
        case 20: return OpcodeType::Nop;
        case 21: return OpcodeType::Fadd;
        case 22: return OpcodeType::Fsub;
        case 23: return OpcodeType::Fmul;
        case 24: return OpcodeType::Fdiv;
        case 25: return OpcodeType::DcZva;
        case 26: return OpcodeType::DcCivac;
        case 27: return OpcodeType::DcCvac;
        case 28: return OpcodeType::DcCsw;
        case 29: return OpcodeType::IcIvau;
        case 30: return OpcodeType::IcIallu;
        case 31: return OpcodeType::IcIalluis;
        case 32: return OpcodeType::Aesd;
        case 33: return OpcodeType::Aese;
        case 34: return OpcodeType::Aesimc;
        case 35: return OpcodeType::Aesmc;
        case 36: return OpcodeType::Sha1H;
        case 37: return OpcodeType::Sha256H;
        case 38: return OpcodeType::Sha512H;
        case 39: return OpcodeType::Vadd;
        case 40: return OpcodeType::Vsub;
        case 41: return OpcodeType::Vmul;
        case 42: return OpcodeType::Vmla;
        case 43: return OpcodeType::Vmls;
        case 44: return OpcodeType::Vld;
        case 45: return OpcodeType::Vst;
        case 46: return OpcodeType::Vdup;
        case 47: return OpcodeType::Vmov;
        case 48: return OpcodeType::Fmadd;
        case 49: return OpcodeType::Fmsub;
        case 50: return OpcodeType::Fnmadd;
        case 51: return OpcodeType::Fnmsub;
        case 52: return OpcodeType::Mov;
        case 53: return OpcodeType::Cmp;
        case 54: return OpcodeType::Shift;
        case 74: return OpcodeType::Fcvt;
        case 75: return OpcodeType::Dmb;
        case 76: return OpcodeType::Dsb;
        case 77: return OpcodeType::Isb;
        case 78: return OpcodeType::Eret;
        case 79: return OpcodeType::Yield;
        case 80: return OpcodeType::Adr;
        case 81: return OpcodeType::Pmull;
        default: return OpcodeType::Other;
    }
}

} // namespace arm_cpu
