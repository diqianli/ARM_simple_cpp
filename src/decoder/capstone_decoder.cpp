/// @file capstone_decoder.cpp
/// @brief Capstone-based AArch64 decoder implementation.
///
/// Maps Capstone disassembly output to the emulator's internal types.
/// The mnemonic mapping table covers ~200 ARM64 mnemonics including
/// SVE, SVE2, and SME extensions.

#include "arm_cpu/decoder/capstone_decoder.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

// Capstone headers
#include <capstone/capstone.h>
#include <capstone/arm64.h>

namespace arm_cpu::decoder {

// =====================================================================
// Mnemonic → OpcodeType mapping table
// =====================================================================

struct MnemonicMapping {
    const char* mnemonic;
    OpcodeType opcode;
};

/// Sorted array of mnemonic mappings for binary search.
/// Covers ARMv8.0-A baseline, NEON/FP, SVE, SVE2, and SME.
static constexpr MnemonicMapping kArm64Mappings[] = {
    // --- Integer arithmetic ---
    {"adc",  OpcodeType::Add},
    {"adcs", OpcodeType::Add},
    {"add",  OpcodeType::Add},
    {"adds", OpcodeType::Add},
    {"adr",  OpcodeType::Adr},
    {"adrp", OpcodeType::Adr},
    {"and",  OpcodeType::And},
    {"ands", OpcodeType::And},
    {"asr",  OpcodeType::Asr},
    {"b",    OpcodeType::Branch},
    {"b.al", OpcodeType::Branch},
    {"b.cc", OpcodeType::BranchCond},
    {"b.cs", OpcodeType::BranchCond},
    {"b.eq", OpcodeType::BranchCond},
    {"b.ge", OpcodeType::BranchCond},
    {"b.gt", OpcodeType::BranchCond},
    {"b.hi", OpcodeType::BranchCond},
    {"b.hs", OpcodeType::BranchCond},
    {"b.le", OpcodeType::BranchCond},
    {"b.lo", OpcodeType::BranchCond},
    {"b.ls", OpcodeType::BranchCond},
    {"b.lt", OpcodeType::BranchCond},
    {"b.mi", OpcodeType::BranchCond},
    {"b.ne", OpcodeType::BranchCond},
    {"b.pl", OpcodeType::BranchCond},
    {"b.vc", OpcodeType::BranchCond},
    {"b.vs", OpcodeType::BranchCond},
    {"bic",  OpcodeType::And},
    {"bics", OpcodeType::And},
    {"bl",   OpcodeType::Branch},
    {"blr",  OpcodeType::BranchReg},
    {"br",   OpcodeType::BranchReg},
    {"brk",  OpcodeType::Other},
    {"cbnz", OpcodeType::BranchCond},
    {"cbz",  OpcodeType::BranchCond},
    {"cinc", OpcodeType::Add},
    {"cinv", OpcodeType::Add},
    {"clrex",OpcodeType::Other},
    {"cls",  OpcodeType::Other},
    {"clz",  OpcodeType::Other},
    {"cmn",  OpcodeType::Cmp},
    {"cmp",  OpcodeType::Cmp},
    {"cneg", OpcodeType::Sub},
    {"cset", OpcodeType::Csel},
    {"csetm",OpcodeType::Csetm},
    {"csel", OpcodeType::Csel},
    {"csinc",OpcodeType::Csel},
    {"csinv",OpcodeType::Csel},
    {"csneg",OpcodeType::Csel},
    // --- Divide ---
    {"sdiv", OpcodeType::Div},
    {"udiv", OpcodeType::Div},
    // --- Exclusive/atomic ---
    {"cas",  OpcodeType::CompareSwap},
    {"casa", OpcodeType::CompareSwap},
    {"casab",OpcodeType::CompareSwap},
    {"casah",OpcodeType::CompareSwap},
    {"casb", OpcodeType::CompareSwap},
    {"cash", OpcodeType::CompareSwap},
    {"casl", OpcodeType::CompareSwap},
    {"casla",OpcodeType::CompareSwap},
    {"caslb",OpcodeType::CompareSwap},
    {"caslh",OpcodeType::CompareSwap},
    {"ldadd",OpcodeType::AtomicAdd},
    {"ldadda",OpcodeType::AtomicAdd},
    {"ldaddab",OpcodeType::AtomicAdd},
    {"ldaddah",OpcodeType::AtomicAdd},
    {"ldaddb",OpcodeType::AtomicAdd},
    {"ldaddh",OpcodeType::AtomicAdd},
    {"ldaddl",OpcodeType::AtomicAdd},
    {"ldaddal",OpcodeType::AtomicAdd},
    {"ldclr",OpcodeType::AtomicClr},
    {"ldclra",OpcodeType::AtomicClr},
    {"ldclrb",OpcodeType::AtomicClr},
    {"ldclrh",OpcodeType::AtomicClr},
    {"ldclrl",OpcodeType::AtomicClr},
    {"ldclral",OpcodeType::AtomicClr},
    {"ldaddr",OpcodeType::AtomicAdd},
    {"ldrexd",OpcodeType::Load},
    {"ldrex", OpcodeType::Load},
    {"ldset",OpcodeType::AtomicSet},
    {"ldseta",OpcodeType::AtomicSet},
    {"ldsetb",OpcodeType::AtomicSet},
    {"ldseth",OpcodeType::AtomicSet},
    {"ldsetl",OpcodeType::AtomicSet},
    {"ldsetal",OpcodeType::AtomicSet},
    {"ldswp",OpcodeType::AtomicSwp},
    {"ldxp", OpcodeType::LoadPair},
    {"ldxr", OpcodeType::Load},
    {"stlxp",OpcodeType::StorePair},
    {"stlxr",OpcodeType::Store},
    {"stxp", OpcodeType::StorePair},
    {"stxr", OpcodeType::Store},
    // --- EOR ---
    {"eon",  OpcodeType::Eor},
    {"eons", OpcodeType::Eor},
    {"eor",  OpcodeType::Eor},
    {"eors", OpcodeType::Eor},
    // --- Extend/shift ---
    {"extr", OpcodeType::Shift},
    {"lsl",  OpcodeType::Lsl},
    {"lslv", OpcodeType::Lsl},
    {"lsr",  OpcodeType::Lsr},
    {"lsrv", OpcodeType::Lsr},
    {"madd", OpcodeType::Mul},
    {"msub", OpcodeType::Mul},
    {"mul",  OpcodeType::Mul},
    {"mneg", OpcodeType::Mul},
    {"mov",  OpcodeType::Mov},
    {"movk", OpcodeType::Mov},
    {"movn", OpcodeType::Mov},
    {"movz", OpcodeType::Mov},
    {"mvn",  OpcodeType::Mov},
    {"neg",  OpcodeType::Sub},
    {"negs", OpcodeType::Sub},
    {"ngc",  OpcodeType::Sub},
    {"ngcs", OpcodeType::Sub},
    {"nop",  OpcodeType::Nop},
    {"orn",  OpcodeType::Orr},
    {"orns", OpcodeType::Orr},
    {"orr",  OpcodeType::Orr},
    {"orrs", OpcodeType::Orr},
    // --- Prefetch ---
    {"prfm", OpcodeType::Prefetch},
    {"prfum",OpcodeType::Prefetch},
    // --- RBIT/REV ---
    {"rbit", OpcodeType::Other},
    {"rev",  OpcodeType::Other},
    {"rev16",OpcodeType::Other},
    {"rev32",OpcodeType::Other},
    {"ret",  OpcodeType::BranchReg},
    // --- Rotate ---
    {"ror",  OpcodeType::Asr},
    // --- SBC ---
    {"sbc",  OpcodeType::Sub},
    {"sbcs", OpcodeType::Sub},
    // --- Sub ---
    {"sub",  OpcodeType::Sub},
    {"subs", OpcodeType::Sub},
    // --- System ---
    {"dc",   OpcodeType::Other},  // DC ZVA, CIVAC, etc. handled by disasm string
    {"dmb",  OpcodeType::Dmb},
    {"dsb",  OpcodeType::Dsb},
    {"eret", OpcodeType::Eret},
    {"hint", OpcodeType::Other},
    {"ic",   OpcodeType::Other},
    {"isb",  OpcodeType::Isb},
    {"msr",  OpcodeType::Msr},
    {"mrs",  OpcodeType::Mrs},
    {"sys",  OpcodeType::Sys},
    {"sysl", OpcodeType::Mrs},
    {"tlbi", OpcodeType::Other},
    {"wfe",  OpcodeType::Yield},
    {"wfi",  OpcodeType::Yield},
    {"yield",OpcodeType::Yield},
    // --- TBZ/TBNZ ---
    {"tbz",  OpcodeType::Tbz},
    {"tbnz", OpcodeType::Tbnz},
    // --- NEON/SIMD ---
    {"abs",  OpcodeType::Vadd},
    {"addp", OpcodeType::Vadd},
    {"addv", OpcodeType::Vadd},
    {"and",  OpcodeType::And},  // NEON AND (already covered above)
    {"bic",  OpcodeType::And},
    {"bsl",  OpcodeType::Vmov},
    {"cmeq", OpcodeType::Vadd},
    {"cmge", OpcodeType::Vadd},
    {"cmgt", OpcodeType::Vadd},
    {"cmhi", OpcodeType::Vadd},
    {"cmhs", OpcodeType::Vadd},
    {"cmle", OpcodeType::Vadd},
    {"cmlt", OpcodeType::Vadd},
    {"cnt",  OpcodeType::Vadd},
    {"dup",  OpcodeType::Vdup},
    {"ext",  OpcodeType::Vmov},
    {"fabs", OpcodeType::Fadd},
    {"fadd", OpcodeType::Fadd},
    {"faddp",OpcodeType::Fadd},
    {"fcmeq",OpcodeType::Fadd},
    {"fcmge",OpcodeType::Fadd},
    {"fcmgt",OpcodeType::Fadd},
    {"fcvt", OpcodeType::Fcvt},
    {"fcvtl",OpcodeType::Fcvt},
    {"fcvtn",OpcodeType::Fcvt},
    {"fcvtns",OpcodeType::Fcvt},
    {"fcvtps",OpcodeType::Fcvt},
    {"fcvtzu",OpcodeType::Fcvt},
    {"fdiv", OpcodeType::Fdiv},
    {"fmadd",OpcodeType::Fmadd},
    {"fmax", OpcodeType::Fadd},
    {"fmaxnm",OpcodeType::Fadd},
    {"fmin", OpcodeType::Fsub},
    {"fminnm",OpcodeType::Fsub},
    {"fmla", OpcodeType::Vmla},
    {"fmls", OpcodeType::Vmls},
    {"fmov", OpcodeType::Vmov},
    {"fmsub",OpcodeType::Fmsub},
    {"fmul", OpcodeType::Fmul},
    {"fmulp",OpcodeType::Fmul},
    {"fneg", OpcodeType::Fsub},
    {"fnmadd",OpcodeType::Fnmadd},
    {"fnmsub",OpcodeType::Fnmsub},
    {"fsqrt",OpcodeType::Fdiv},
    {"fsub", OpcodeType::Fsub},
    {"ins",  OpcodeType::Vmov},
    {"ld1",  OpcodeType::Vld},
    {"ld2",  OpcodeType::Vld},
    {"ld3",  OpcodeType::Vld},
    {"ld4",  OpcodeType::Vld},
    {"ld1r", OpcodeType::Vld},
    {"ld2r", OpcodeType::Vld},
    {"ld3r", OpcodeType::Vld},
    {"ld4r", OpcodeType::Vld},
    {"ldp",  OpcodeType::LoadPair},
    {"ldr",  OpcodeType::Load},
    {"ldrb", OpcodeType::Load},
    {"ldrh", OpcodeType::Load},
    {"ldrsb",OpcodeType::Load},
    {"ldrsh",OpcodeType::Load},
    {"ldrsw",OpcodeType::Load},
    {"ldur", OpcodeType::Load},
    {"ldurb",OpcodeType::Load},
    {"ldurh",OpcodeType::Load},
    {"movi", OpcodeType::Vmov},
    {"mrs",  OpcodeType::Mrs},
    {"mul",  OpcodeType::Vmul},
    {"mvni", OpcodeType::Vmov},
    {"neg",  OpcodeType::Vsub},
    {"not",  OpcodeType::Vmov},
    {"orn",  OpcodeType::Orr},
    {"orr",  OpcodeType::Orr},
    {"pminv",OpcodeType::Vadd},
    {"ptrue",OpcodeType::Other},
    {"qadd", OpcodeType::Vadd},
    {"qsub", OpcodeType::Vsub},
    {"rbit", OpcodeType::Vadd},
    {"rev16",OpcodeType::Vadd},
    {"rev32",OpcodeType::Vadd},
    {"rev64",OpcodeType::Vadd},
    {"saba", OpcodeType::Vadd},
    {"sabd", OpcodeType::Vadd},
    {"sadalp",OpcodeType::Vadd},
    {"saddl",OpcodeType::Vadd},
    {"saddlp",OpcodeType::Vadd},
    {"saddlv",OpcodeType::Vadd},
    {"saddw",OpcodeType::Vadd},
    {"scvtf",OpcodeType::Fcvt},
    {"shadd",OpcodeType::Vadd},
    {"shll", OpcodeType::Vmov},
    {"shl",  OpcodeType::Lsl},
    {"shrn", OpcodeType::Lsr},
    {"shsub",OpcodeType::Vsub},
    {"sli",  OpcodeType::Lsl},
    {"smull",OpcodeType::Vmul},
    {"smlal",OpcodeType::Vmla},
    {"smlsl",OpcodeType::Vmls},
    {"smov", OpcodeType::Vmov},
    {"sqabs",OpcodeType::Vadd},
    {"sqadd",OpcodeType::Vadd},
    {"sqdmulh",OpcodeType::Vmul},
    {"sqdmull",OpcodeType::Vmul},
    {"sqneg",OpcodeType::Vsub},
    {"sqrdmulh",OpcodeType::Vmul},
    {"sqsub",OpcodeType::Vsub},
    {"sqxtun",OpcodeType::Fcvt},
    {"sri",  OpcodeType::Lsr},
    {"srsra",OpcodeType::Vadd},
    {"sshl", OpcodeType::Lsl},
    {"ssra", OpcodeType::Vadd},
    {"st1",  OpcodeType::Vst},
    {"st2",  OpcodeType::Vst},
    {"st3",  OpcodeType::Vst},
    {"st4",  OpcodeType::Vst},
    {"stp",  OpcodeType::StorePair},
    {"str",  OpcodeType::Store},
    {"strb", OpcodeType::Store},
    {"strh", OpcodeType::Store},
    {"stur", OpcodeType::Store},
    {"sturb",OpcodeType::Store},
    {"sturh",OpcodeType::Store},
    {"sub",  OpcodeType::Vsub},
    {"sxtl", OpcodeType::Vmov},
    {"tbl",  OpcodeType::Vmov},
    {"tbx",  OpcodeType::Vmov},
    {"trn1", OpcodeType::Vmov},
    {"trn2", OpcodeType::Vmov},
    {"uaba", OpcodeType::Vadd},
    {"uabd", OpcodeType::Vadd},
    {"uadalp",OpcodeType::Vadd},
    {"uaddl",OpcodeType::Vadd},
    {"uaddlp",OpcodeType::Vadd},
    {"uaddlv",OpcodeType::Vadd},
    {"uaddw",OpcodeType::Vadd},
    {"ucvtf",OpcodeType::Fcvt},
    {"uhadd",OpcodeType::Vadd},
    {"uhsub",OpcodeType::Vsub},
    {"umull",OpcodeType::Vmul},
    {"umlal",OpcodeType::Vmla},
    {"umlsl",OpcodeType::Vmls},
    {"umov", OpcodeType::Vmov},
    {"uqadd",OpcodeType::Vadd},
    {"uqsub",OpcodeType::Vsub},
    {"ushll",OpcodeType::Vmov},
    {"ushr", OpcodeType::Lsr},
    {"usqadd",OpcodeType::Vadd},
    {"usra", OpcodeType::Vadd},
    {"usxtl",OpcodeType::Vmov},
    {"uzp1", OpcodeType::Vmov},
    {"uzp2", OpcodeType::Vmov},
    {"xtn",  OpcodeType::Fcvt},
    {"zip1", OpcodeType::Vmov},
    {"zip2", OpcodeType::Vmov},
    // --- Crypto ---
    {"aesd", OpcodeType::Aesd},
    {"aese", OpcodeType::Aese},
    {"aesimc",OpcodeType::Aesimc},
    {"aesmc", OpcodeType::Aesmc},
    {"pmull",OpcodeType::Pmull},
    {"sha1c",OpcodeType::Sha1H},
    {"sha1h", OpcodeType::Sha1H},
    {"sha1m",OpcodeType::Sha1H},
    {"sha1p",OpcodeType::Sha1H},
    {"sha1su0",OpcodeType::Sha1H},
    {"sha256h",OpcodeType::Sha256H},
    {"sha256h2",OpcodeType::Sha256H},
    {"sha256su0",OpcodeType::Sha256H},
    {"sha256su1",OpcodeType::Sha256H},
    {"sha512h",OpcodeType::Sha512H},
    {"sha512h2",OpcodeType::Sha512H},
    {"sha512su0",OpcodeType::Sha512H},
    {"sha512su1",OpcodeType::Sha512H},
    // --- SVE ---
    {"sve_add",OpcodeType::SveAdd},
    {"sve_sub",OpcodeType::SveSub},
    {"sve_mul",OpcodeType::SveMul},
    {"sve_fmul",OpcodeType::SveMul},
    {"sve_fadd",OpcodeType::SveAdd},
    {"sve_fsub",OpcodeType::SveSub},
    {"sve_fma",OpcodeType::SveFma},
    {"sve_fmla",OpcodeType::SveFma},
    {"sve_fmls",OpcodeType::SveFma},
    {"sve_cmp",OpcodeType::SveCmp},
    {"sve_fcmp",OpcodeType::SveFcmp},
    {"sve_cmpeq",OpcodeType::SveCmp},
    {"sve_cmpgt",OpcodeType::SveCmp},
    {"sve_cmpge",OpcodeType::SveCmp},
    {"sve_cmpne",OpcodeType::SveCmp},
    {"sve_cmplo",OpcodeType::SveCmp},
    {"sve_cmpls",OpcodeType::SveCmp},
    {"sve_ld1b",OpcodeType::SveLoad},
    {"sve_ld1h",OpcodeType::SveLoad},
    {"sve_ld1w",OpcodeType::SveLoad},
    {"sve_ld1d",OpcodeType::SveLoad},
    {"sve_ld1sb",OpcodeType::SveLoad},
    {"sve_ld1sh",OpcodeType::SveLoad},
    {"sve_ld1sw",OpcodeType::SveLoad},
    {"sve_ld2b",OpcodeType::SveLoad},
    {"sve_ld2h",OpcodeType::SveLoad},
    {"sve_ld2w",OpcodeType::SveLoad},
    {"sve_ld2d",OpcodeType::SveLoad},
    {"sve_ld3b",OpcodeType::SveLoad},
    {"sve_ld3h",OpcodeType::SveLoad},
    {"sve_ld3w",OpcodeType::SveLoad},
    {"sve_ld3d",OpcodeType::SveLoad},
    {"sve_ld4b",OpcodeType::SveLoad},
    {"sve_ld4h",OpcodeType::SveLoad},
    {"sve_ld4w",OpcodeType::SveLoad},
    {"sve_ld4d",OpcodeType::SveLoad},
    {"sve_ldnt1b",OpcodeType::SveLoad},
    {"sve_ldnt1h",OpcodeType::SveLoad},
    {"sve_ldnt1w",OpcodeType::SveLoad},
    {"sve_ldnt1d",OpcodeType::SveLoad},
    {"sve_st1b",OpcodeType::SveStore},
    {"sve_st1h",OpcodeType::SveStore},
    {"sve_st1w",OpcodeType::SveStore},
    {"sve_st1d",OpcodeType::SveStore},
    {"sve_st2b",OpcodeType::SveStore},
    {"sve_st2h",OpcodeType::SveStore},
    {"sve_st2w",OpcodeType::SveStore},
    {"sve_st2d",OpcodeType::SveStore},
    {"sve_st3b",OpcodeType::SveStore},
    {"sve_st3h",OpcodeType::SveStore},
    {"sve_st3w",OpcodeType::SveStore},
    {"sve_st3d",OpcodeType::SveStore},
    {"sve_st4b",OpcodeType::SveStore},
    {"sve_st4h",OpcodeType::SveStore},
    {"sve_st4w",OpcodeType::SveStore},
    {"sve_st4d",OpcodeType::SveStore},
    {"sve_ld1b_z",OpcodeType::SveLoadContiguous},
    {"sve_ld1h_z",OpcodeType::SveLoadContiguous},
    {"sve_ld1w_z",OpcodeType::SveLoadContiguous},
    {"sve_ld1d_z",OpcodeType::SveLoadContiguous},
    {"sve_st1b_z",OpcodeType::SveStoreContiguous},
    {"sve_st1h_z",OpcodeType::SveStoreContiguous},
    {"sve_st1w_z",OpcodeType::SveStoreContiguous},
    {"sve_st1d_z",OpcodeType::SveStoreContiguous},
    {"sve_sel",OpcodeType::SveSel},
    {"sve_merge",OpcodeType::SveMerge},
    {"sve_zip",OpcodeType::SveZip},
    {"sve_uzp",OpcodeType::SveUzip},
    {"sve_trn",OpcodeType::SveTrn},
    {"sve_cvt",OpcodeType::SveCvt},
    {"sve_fcvt",OpcodeType::SveFcvt},
    {"sve_fcvtzs",OpcodeType::SveFcvt},
    {"sve_fcvtzu",OpcodeType::SveFcvt},
    {"sve_scvt",OpcodeType::SveFcvt},
    {"sve_ucvt",OpcodeType::SveFcvt},
    {"sve_ptrue",OpcodeType::SvePtrue},
    {"sve_pfalse",OpcodeType::SvePtrue},
    {"sve_pfirst",OpcodeType::SvePfirst},
    {"sve_pnext",OpcodeType::SvePnext},
    {"sve_ptest",OpcodeType::SvePfirst},
    {"sve_and",OpcodeType::SvePfirst},
    {"sve_orr",OpcodeType::SvePfirst},
    {"sve_eor",OpcodeType::SvePfirst},
    {"sve_not",OpcodeType::SvePfirst},
    {"sve_dup",OpcodeType::SvePtrue},
    {"sve_abs",OpcodeType::SveAdd},
    {"sve_neg",OpcodeType::SveSub},
    {"sve_min",OpcodeType::SveCmp},
    {"sve_max",OpcodeType::SveCmp},
    {"sve_fmin",OpcodeType::SveFcmp},
    {"sve_fmax",OpcodeType::SveFcmp},
    {"sve_fminnm",OpcodeType::SveFcmp},
    {"sve_fmaxnm",OpcodeType::SveFcmp},
    {"sve_index",OpcodeType::SvePtrue},
    {"sve_whilelo",OpcodeType::SvePnext},
    {"sve_whilels",OpcodeType::SvePnext},
    {"sve_whilelt",OpcodeType::SvePnext},
    {"sve_whilele",OpcodeType::SvePnext},
    {"sve_whilege",OpcodeType::SvePnext},
    {"sve_whilegt",OpcodeType::SvePnext},
    {"sve_cntp",OpcodeType::SvePfirst},
    {"sve_brka",OpcodeType::SvePfirst},
    {"sve_brkb",OpcodeType::SvePfirst},
    {"sve_brkn",OpcodeType::SvePfirst},
    {"sve_brkpa",OpcodeType::SvePfirst},
    {"sve_brkpb",OpcodeType::SvePfirst},
    {"sve_revb",OpcodeType::SveZip},
    {"sve_revh",OpcodeType::SveZip},
    {"sve_revw",OpcodeType::SveZip},
    {"sve_tbl",OpcodeType::SveZip},
    {"sve_ext",OpcodeType::SveZip},
    {"sve_ins",OpcodeType::SveMerge},
    {"sve_splice",OpcodeType::SveMerge},
    {"sve_compact",OpcodeType::SveMerge},
    {"sve_sunpkhi",OpcodeType::SveCvt},
    {"sve_sunpklo",OpcodeType::SveCvt},
    {"sve_uunpkhi",OpcodeType::SveCvt},
    {"sve_uunpklo",OpcodeType::SveCvt},
    {"sve_sxtw",OpcodeType::SveCvt},
    {"sve_uzp1",OpcodeType::SveUzip},
    {"sve_uzp2",OpcodeType::SveUzip},
    {"sve_zip1",OpcodeType::SveZip},
    {"sve_zip2",OpcodeType::SveZip},
    {"sve_trn1",OpcodeType::SveTrn},
    {"sve_trn2",OpcodeType::SveTrn},
    {"sve_lasta",OpcodeType::SveMerge},
    {"sve_lastb",OpcodeType::SveMerge},
    {"sve_dot",OpcodeType::SveMul},
    {"sve_sdot",OpcodeType::SveMul},
    {"sve_udot",OpcodeType::SveMul},
    {"sve_smlal",OpcodeType::SveFma},
    {"sve_umlal",OpcodeType::SveFma},
    {"sve_ldff1b",OpcodeType::SveLoad},
    {"sve_ldff1h",OpcodeType::SveLoad},
    {"sve_ldff1w",OpcodeType::SveLoad},
    {"sve_ldff1d",OpcodeType::SveLoad},
    {"sve_ldnf1b",OpcodeType::SveLoad},
    {"sve_ldnf1h",OpcodeType::SveLoad},
    {"sve_ldnf1w",OpcodeType::SveLoad},
    {"sve_ldnf1d",OpcodeType::SveLoad},
    {"sve_prfb",OpcodeType::SveLoad},
    {"sve_prfh",OpcodeType::SveLoad},
    {"sve_prfw",OpcodeType::SveLoad},
    {"sve_prfd",OpcodeType::SveLoad},
    // --- SME ---
    {"smstart",OpcodeType::SmeStreamingMode},
    {"smstop", OpcodeType::SmeStreamingMode},
    {"sme_fmopa",OpcodeType::SmeOuterProduct},
    {"sme_smopa",OpcodeType::SmeOuterProduct},
    {"sme_umopa",OpcodeType::SmeOuterProduct},
    {"sme_usmopa",OpcodeType::SmeOuterProduct},
    {"sme_fmops",OpcodeType::SmeOuterProduct},
    {"sme_smops",OpcodeType::SmeOuterProduct},
    {"sve_ld1b_z",OpcodeType::SveLoadContiguous},
    {"sme_ld1b",OpcodeType::SmeTileLoad},
    {"sme_ld1h",OpcodeType::SmeTileLoad},
    {"sme_ld1w",OpcodeType::SmeTileLoad},
    {"sme_ld1d",OpcodeType::SmeTileLoad},
    {"sme_st1b",OpcodeType::SmeTileStore},
    {"sme_st1h",OpcodeType::SmeTileStore},
    {"sme_st1w",OpcodeType::SmeTileStore},
    {"sme_st1d",OpcodeType::SmeTileStore},
    {"sme_addha",OpcodeType::SmeOuterProduct},
    {"sme_addva",OpcodeType::SmeOuterProduct},
};

// =====================================================================
// MnemonicLookup implementation
// =====================================================================

MnemonicLookup::MnemonicLookup() {
    for (const auto& m : kArm64Mappings) {
        table.emplace(m.mnemonic, m.opcode);
    }
}

OpcodeType MnemonicLookup::lookup(std::string_view mnemonic) const {
    auto it = table.find(mnemonic);
    if (it != table.end()) return it->second;
    return OpcodeType::Other;
}

// Static instance
const MnemonicLookup CapstoneDecoder::lookup_{};

// =====================================================================
// CapstoneDecoder implementation
// =====================================================================

CapstoneDecoder::CapstoneDecoder() = default;

CapstoneDecoder::~CapstoneDecoder() {
    if (handle_) {
        cs_close(&handle_);
    }
}

CapstoneDecoder::CapstoneDecoder(CapstoneDecoder&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = 0;
}

CapstoneDecoder& CapstoneDecoder::operator=(CapstoneDecoder&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            cs_close(&handle_);
        }
        handle_ = other.handle_;
        other.handle_ = 0;
    }
    return *this;
}

bool CapstoneDecoder::init() {
    if (handle_) return true;

    cs_mode mode = CS_MODE_LITTLE_ENDIAN;
    if (cs_open(CS_ARCH_ARM64, mode, &handle_) != CS_ERR_OK) {
        handle_ = 0;
        std::fprintf(stderr, "Capstone: failed to initialize AArch64 disassembler\n");
        return false;
    }

    // Enable detailed mode for operand extraction
    cs_option(handle_, CS_OPT_DETAIL, CS_OPT_ON);

    std::fprintf(stderr, "Capstone: initialized AArch64 disassembler (SVE/SME enabled)\n");
    return true;
}

OpcodeType CapstoneDecoder::map_mnemonic(std::string_view mnemonic) const {
    auto result = lookup_.lookup(mnemonic);
    if (result != OpcodeType::Other) return result;

    // Fallback: pattern-based classification for mnemonics not in the table
    if (mnemonic.starts_with("sve_")) return OpcodeType::SveAdd;
    if (mnemonic.starts_with("sme_")) return OpcodeType::SmeOuterProduct;

    return OpcodeType::Other;
}

bool CapstoneDecoder::is_conditional_branch(std::string_view mnemonic) const {
    // B.cond forms: b.eq, b.ne, b.lt, etc.
    if (mnemonic.starts_with("b.")) return true;
    return mnemonic == "cbnz" || mnemonic == "cbz" ||
           mnemonic == "tbz"  || mnemonic == "tbnz";
}

void CapstoneDecoder::extract_operands(const cs_insn* insn, DecodedInstruction& out) const {
    if (!insn || !insn->detail) return;

    auto* detail = insn->detail;
    auto* arch = &detail->arm64;

    // Helper: map Capstone ARM64 register ID to our Reg number (0-31).
    // Capstone v5 uses non-contiguous IDs: X0=218..X28=246, FP/X29=2, LR/X30=3,
    // W0=187..W28=215, WZR=8, SP=34, WSP=7, XZR=1.
    auto to_gpr = [](unsigned int reg_id) -> std::optional<uint8_t> {
        if (reg_id >= ARM64_REG_X0 && reg_id <= ARM64_REG_X28)
            return static_cast<uint8_t>(reg_id - ARM64_REG_X0);
        if (reg_id == ARM64_REG_FP || reg_id == ARM64_REG_X29) return 29;
        if (reg_id == ARM64_REG_LR || reg_id == ARM64_REG_X30) return 30;
        if (reg_id >= ARM64_REG_W0 && reg_id <= ARM64_REG_W28)
            return static_cast<uint8_t>(reg_id - ARM64_REG_W0);
        if (reg_id == ARM64_REG_XZR || reg_id == ARM64_REG_WZR ||
            reg_id == ARM64_REG_SP  || reg_id == ARM64_REG_WSP)
            return 31;
        return std::nullopt;
    };

    // Extract operand count and iterate
    int op_count = detail->arm64.op_count;
    for (int i = 0; i < op_count && i < 8; ++i) {
        const auto& op = arch->operands[i];

        switch (op.type) {
            case ARM64_OP_REG: {
                auto reg_id = op.reg;
                bool is_write = (op.access & CS_AC_WRITE) != 0;
                bool is_read  = (op.access & CS_AC_READ) != 0;

                // GPR (X/W registers)
                if (auto gpr = to_gpr(reg_id); gpr.has_value()) {
                    Reg r(*gpr);
                    if (is_write) {
                        out.dst_regs.push_back(r);
                        if (is_read) out.src_regs.push_back(r);
                    } else {
                        out.src_regs.push_back(r);
                    }
                } else if (reg_id >= ARM64_REG_V0 && reg_id <= ARM64_REG_V31) {
                    VReg r(static_cast<uint8_t>(reg_id - ARM64_REG_V0));
                    if (is_write) {
                        out.dst_vregs.push_back(r);
                        if (is_read) out.src_vregs.push_back(r);
                    } else {
                        out.src_vregs.push_back(r);
                    }
                } else if (reg_id >= ARM64_REG_Z0 && reg_id <= ARM64_REG_Z31) {
                    VReg r(static_cast<uint8_t>(reg_id - ARM64_REG_Z0));
                    if (is_write) {
                        out.dst_vregs.push_back(r);
                        if (is_read) out.src_vregs.push_back(r);
                    } else {
                        out.src_vregs.push_back(r);
                    }
                } else if (reg_id >= ARM64_REG_P0 && reg_id <= ARM64_REG_P15) {
                    PReg p(static_cast<uint8_t>(reg_id - ARM64_REG_P0));
                    if (is_write) {
                        out.dst_pregs.push_back(p);
                        if (is_read) out.src_pregs.push_back(p);
                    } else {
                        out.src_pregs.push_back(p);
                    }
                }
                break;
            }
            case ARM64_OP_MEM: {
                // Memory operand — extract base register and offset
                const auto& mem = op.mem;
                if (mem.base != ARM64_REG_INVALID) {
                    if (mem.base >= ARM64_REG_X0 && mem.base <= ARM64_REG_X30) {
                        out.src_regs.push_back(Reg(static_cast<uint8_t>(mem.base - ARM64_REG_X0)));
                    } else if (mem.base == ARM64_REG_SP) {
                        out.src_regs.push_back(Reg(31));
                    }
                }
                if (mem.index != ARM64_REG_INVALID) {
                    if (mem.index >= ARM64_REG_X0 && mem.index <= ARM64_REG_X30) {
                        out.src_regs.push_back(Reg(static_cast<uint8_t>(mem.index - ARM64_REG_X0)));
                    }
                }
                out.mem_access = MemAccess{0, 8, false};
                break;
            }
            case ARM64_OP_IMM: {
                out.immediate = static_cast<int64_t>(op.imm);
                break;
            }
            case ARM64_OP_CIMM: {
                out.immediate = static_cast<int64_t>(op.imm);
                break;
            }
            default:
                break;
        }
    }
}

DecodedInstruction CapstoneDecoder::decode(uint64_t pc, uint32_t raw) const {
    DecodedInstruction result(pc, raw);

    if (!handle_) {
        std::fprintf(stderr, "CapstoneDecoder::decode called without initialization\n");
        return result;
    }

    auto h = handle_;
    cs_insn* insn = nullptr;
    size_t count = cs_disasm(h, reinterpret_cast<const uint8_t*>(&raw), 4,
                             0x1000, 1, &insn);

    if (count == 0 || !insn) {
        return result;
    }

    // Store disassembly string
    result.disasm = insn->mnemonic;
    if (insn->op_str && insn->op_str[0]) {
        result.disasm += " ";
        result.disasm += insn->op_str;
    }

    // Map mnemonic to OpcodeType
    result.opcode = map_mnemonic(insn->mnemonic);

    // Handle special cases based on disasm text for system instructions
    if (result.opcode == OpcodeType::Other) {
        std::string_view disasm = result.disasm;
        // Cache maintenance: dc zva, dc cvau, dc civac, etc.
        if (disasm.starts_with("dc ")) {
            if (disasm.find("zva") != std::string_view::npos) {
                result.opcode = OpcodeType::DcZva;
            } else if (disasm.find("civac") != std::string_view::npos) {
                result.opcode = OpcodeType::DcCivac;
            } else if (disasm.find("cvac") != std::string_view::npos) {
                result.opcode = OpcodeType::DcCvac;
            } else if (disasm.find("csw") != std::string_view::npos) {
                result.opcode = OpcodeType::DcCsw;
            }
        } else if (disasm.starts_with("ic ")) {
            if (disasm.find("ivau") != std::string_view::npos) {
                result.opcode = OpcodeType::IcIvau;
            } else if (disasm.find("iallu") != std::string_view::npos) {
                result.opcode = OpcodeType::IcIallu;
            } else if (disasm.find("ialluis") != std::string_view::npos) {
                result.opcode = OpcodeType::IcIalluis;
            }
        }
    }

    // Set memory access direction based on opcode classification
    if (result.mem_access.has_value()) {
        result.mem_access->is_load = is_memory_op(result.opcode) && !is_store_op(result.opcode);
    }

    // Set branch info
    if (is_branch(result.opcode)) {
        bool is_cond = is_conditional_branch(insn->mnemonic);
        // Extract branch target from immediate if available
        uint64_t target = 0;
        if (insn->detail) {
            auto& arch = insn->detail->arm64;
            for (int i = 0; i < arch.op_count; ++i) {
                if (arch.operands[i].type == ARM64_OP_IMM) {
                    target = static_cast<uint64_t>(arch.operands[i].imm);
                    break;
                }
            }
        }
        result.branch_info = BranchInfo{is_cond, target, true};
    }

    // Report unmapped mnemonics to stderr for diagnostics
    if (result.opcode == OpcodeType::Other) {
        reported_undefined_.emplace(insn->mnemonic);
        if (reported_undefined_.size() <= 10) {
            std::fprintf(stderr,
                "Warning: Unmapped mnemonic \"%s\" at PC=0x%llx (raw=0x%08x)\n",
                insn->mnemonic, (unsigned long long)pc, (unsigned)raw);
        }
    }

    // Extract register operands
    extract_operands(insn, result);

    cs_free(insn, count);
    return result;
}

} // namespace arm_cpu::decoder
