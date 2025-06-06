/*
 * Copyright (C) 2015-2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "B3Opcode.h"

#if ENABLE(B3_JIT)

#include <wtf/PrintStream.h>

#if !ASSERT_ENABLED
IGNORE_RETURN_TYPE_WARNINGS_BEGIN
#endif

namespace JSC { namespace B3 {

std::optional<Opcode> invertedCompare(Opcode opcode, Type type)
{
    switch (opcode) {
    case Equal:
        return NotEqual;
    case NotEqual:
        return Equal;
    case LessThan:
        if (type.isInt())
            return GreaterEqual;
        return std::nullopt;
    case GreaterThan:
        if (type.isInt())
            return LessEqual;
        return std::nullopt;
    case LessEqual:
        if (type.isInt())
            return GreaterThan;
        return std::nullopt;
    case GreaterEqual:
        if (type.isInt())
            return LessThan;
        return std::nullopt;
    case Above:
        return BelowEqual;
    case Below:
        return AboveEqual;
    case AboveEqual:
        return Below;
    case BelowEqual:
        return Above;
    default:
        return std::nullopt;
    }
}

Opcode storeOpcode(Bank bank, Width width)
{
    switch (bank) {
    case GP:
        switch (width) {
        case Width8:
            return Store8;
        case Width16:
            return Store16;
        default:
            return Store;
        }
    case FP:
        return Store;
    }
    ASSERT_NOT_REACHED();
}

} } // namespace JSC::B3

namespace WTF {

using namespace JSC::B3;

void printInternal(PrintStream& out, Opcode opcode)
{
    switch (opcode) {
    case Nop:
        out.print("Nop");
        return;
    case Identity:
        out.print("Identity");
        return;
    case Opaque:
        out.print("Opaque");
        return;
    case Const32:
        out.print("Const32");
        return;
    case Const64:
        out.print("Const64");
        return;
    case Const128:
        out.print("Const128");
        return;
    case ConstDouble:
        out.print("ConstDouble");
        return;
    case ConstFloat:
        out.print("ConstFloat");
        return;
    case BottomTuple:
        out.print("BottomTuple");
        return;
    case Get:
        out.print("Get");
        return;
    case Set:
        out.print("Set");
        return;
    case SlotBase:
        out.print("SlotBase");
        return;
    case ArgumentReg:
        out.print("ArgumentReg");
        return;
    case FramePointer:
        out.print("FramePointer");
        return;
    case Add:
        out.print("Add");
        return;
    case Sub:
        out.print("Sub");
        return;
    case Mul:
        out.print("Mul");
        return;
    case MulHigh:
        out.print("MulHigh");
        return;
    case UMulHigh:
        out.print("UMulHigh");
        return;
    case Div:
        out.print("Div");
        return;
    case UDiv:
        out.print("UDiv");
        return;
    case Mod:
        out.print("Mod");
        return;
    case UMod:
        out.print("UMod");
        return;
    case FMin:
        out.print("FMin");
        return;
    case FMax:
        out.print("FMax");
        return;
    case Neg:
        out.print("Neg");
        return;
    case PurifyNaN:
        out.print("PurifyNaN");
        return;
    case BitAnd:
        out.print("BitAnd");
        return;
    case BitOr:
        out.print("BitOr");
        return;
    case BitXor:
        out.print("BitXor");
        return;
    case Shl:
        out.print("Shl");
        return;
    case SShr:
        out.print("SShr");
        return;
    case ZShr:
        out.print("ZShr");
        return;
    case RotR:
        out.print("RotR");
        return;
    case RotL:
        out.print("RotL");
        return;
    case Clz:
        out.print("Clz");
        return;
    case Abs:
        out.print("Abs");
        return;
    case Ceil:
        out.print("Ceil");
        return;
    case Floor:
        out.print("Floor");
        return;
    case FTrunc:
        out.print("FTrunc");
        return;
    case Sqrt:
        out.print("Sqrt");
        return;
    case BitwiseCast:
        out.print("BitwiseCast");
        return;
    case SExt8:
        out.print("SExt8");
        return;
    case SExt16:
        out.print("SExt16");
        return;
    case SExt8To64:
        out.print("SExt8To64");
        return;
    case SExt16To64:
        out.print("SExt16To64");
        return;
    case SExt32:
        out.print("SExt32");
        return;
    case ZExt32:
        out.print("ZExt32");
        return;
    case Trunc:
        out.print("Trunc");
        return;
    case TruncHigh:
        out.print("TruncHigh");
        return;
    case Stitch:
        out.print("Stitch");
        return;
    case IToD:
        out.print("IToD");
        return;
    case IToF:
        out.print("IToF");
        return;
    case FloatToDouble:
        out.print("FloatToDouble");
        return;
    case DoubleToFloat:
        out.print("DoubleToFloat");
        return;
    case Equal:
        out.print("Equal");
        return;
    case NotEqual:
        out.print("NotEqual");
        return;
    case LessThan:
        out.print("LessThan");
        return;
    case GreaterThan:
        out.print("GreaterThan");
        return;
    case LessEqual:
        out.print("LessEqual");
        return;
    case GreaterEqual:
        out.print("GreaterEqual");
        return;
    case Above:
        out.print("Above");
        return;
    case Below:
        out.print("Below");
        return;
    case AboveEqual:
        out.print("AboveEqual");
        return;
    case BelowEqual:
        out.print("BelowEqual");
        return;
    case EqualOrUnordered:
        out.print("EqualOrUnordered");
        return;
    case Select:
        out.print("Select");
        return;
    case Load8Z:
        out.print("Load8Z");
        return;
    case Load8S:
        out.print("Load8S");
        return;
    case Load16Z:
        out.print("Load16Z");
        return;
    case Load16S:
        out.print("Load16S");
        return;
    case Load:
        out.print("Load");
        return;
    case Store8:
        out.print("Store8");
        return;
    case Store16:
        out.print("Store16");
        return;
    case Store:
        out.print("Store");
        return;
    case AtomicWeakCAS:
        out.print("AtomicWeakCAS");
        return;
    case AtomicStrongCAS:
        out.print("AtomicStrongCAS");
        return;
    case AtomicXchgAdd:
        out.print("AtomicXchgAdd");
        return;
    case AtomicXchgAnd:
        out.print("AtomicXchgAnd");
        return;
    case AtomicXchgOr:
        out.print("AtomicXchgOr");
        return;
    case AtomicXchgSub:
        out.print("AtomicXchgSub");
        return;
    case AtomicXchgXor:
        out.print("AtomicXchgXor");
        return;
    case AtomicXchg:
        out.print("AtomicXchg");
        return;
    case Depend:
        out.print("Depend");
        return;
    case WasmAddress:
        out.print("WasmAddress");
        return;
    case Fence:
        out.print("Fence");
        return;
    case CCall:
        out.print("CCall");
        return;
    case Patchpoint:
        out.print("Patchpoint");
        return;
    case Extract:
        out.print("Extract");
        return;
    case CheckAdd:
        out.print("CheckAdd");
        return;
    case CheckSub:
        out.print("CheckSub");
        return;
    case CheckMul:
        out.print("CheckMul");
        return;
    case Check:
        out.print("Check");
        return;
    case WasmBoundsCheck:
        out.print("WasmBoundsCheck");
        return;
    case VectorExtractLane:
        out.print("VectorExtractLane");
        return;
    case VectorReplaceLane:
        out.print("VectorReplaceLane");
        return;
    case VectorDupElement:
        out.print("VectorDupElement");
        return;
    case VectorEqual:
        out.print("VectorEqual");
        return;
    case VectorNotEqual:
        out.print("VectorNotEqual");
        return;
    case VectorLessThan:
        out.print("VectorLessThan");
        return;
    case VectorLessThanOrEqual:
        out.print("VectorLessThanOrEqual");
        return;
    case VectorBelow:
        out.print("VectorBelow");
        return;
    case VectorBelowOrEqual:
        out.print("VectorBelowOrEqual");
        return;
    case VectorGreaterThan:
        out.print("VectorGreaterThan");
        return;
    case VectorGreaterThanOrEqual:
        out.print("VectorGreaterThanOrEqual");
        return;
    case VectorAbove:
        out.print("VectorAbove");
        return;
    case VectorAboveOrEqual:
        out.print("VectorAboveOrEqual");
        return;
    case VectorAdd:
        out.print("VectorAdd");
        return;
    case VectorSub:
        out.print("VectorSub");
        return;
    case VectorAddSat:
        out.print("VectorAddSat");
        return;
    case VectorSubSat:
        out.print("VectorSubSat");
        return;
    case VectorMul:
        out.print("VectorMul");
        return;
    case VectorDotProduct:
        out.print("VectorDotProduct");
        return;
    case VectorDiv:
        out.print("VectorDiv");
        return;
    case VectorMin:
        out.print("VectorMin");
        return;
    case VectorMax:
        out.print("VectorMax");
        return;
    case VectorPmin:
        out.print("VectorPmin");
        return;
    case VectorPmax:
        out.print("VectorPmax");
        return;
    case VectorNarrow:
        out.print("VectorNarrow");
        return;
    case VectorNot:
        out.print("VectorNot");
        return;
    case VectorAnd:
        out.print("VectorAnd");
        return;
    case VectorAndnot:
        out.print("VectorAndnot");
        return;
    case VectorOr:
        out.print("VectorOr");
        return;
    case VectorXor:
        out.print("VectorXor");
        return;
    case VectorShl:
        out.print("VectorShl");
        return;
    case VectorShr:
        out.print("VectorShr");
        return;
    case VectorAbs:
        out.print("VectorAbs");
        return;
    case VectorNeg:
        out.print("VectorNeg");
        return;
    case VectorPopcnt:
        out.print("VectorPopcnt");
        return;
    case VectorCeil:
        out.print("VectorCeil");
        return;
    case VectorFloor:
        out.print("VectorFloor");
        return;
    case VectorTrunc:
        out.print("VectorTrunc");
        return;
    case VectorTruncSat:
        out.print("VectorTruncSat");
        return;
    case VectorConvert:
        out.print("VectorConvert");
        return;
    case VectorConvertLow:
        out.print("VectorConvertLow");
        return;
    case VectorNearest:
        out.print("VectorNearest");
        return;
    case VectorSqrt:
        out.print("VectorSqrt");
        return;
    case VectorExtendLow:
        out.print("VectorExtendLow");
        return;
    case VectorExtendHigh:
        out.print("VectorExtendHigh");
        return;
    case VectorPromote:
        out.print("VectorPromote");
        return;
    case VectorDemote:
        out.print("VectorDemote");
        return;
    case VectorSplat:
        out.print("VectorSplat");
        return;
    case VectorAnyTrue:
        out.print("VectorAnyTrue");
        return;
    case VectorAllTrue:
        out.print("VectorAllTrue");
        return;
    case VectorAvgRound:
        out.print("VectorAvgRound");
        return;
    case VectorBitmask:
        out.print("VectorBitmask");
        return;
    case VectorBitwiseSelect:
        out.print("VectorBitwiseSelect");
        return;
    case VectorExtaddPairwise:
        out.print("VectorExtaddPairwise");
        return;
    case VectorMulSat:
        out.print("VectorMulSat");
        return;
    case VectorSwizzle:
        out.print("VectorSwizzle");
        return;
    case VectorMulByElement:
        out.print("VectorMulByElement");
        return;
    case VectorShiftByVector:
        out.print("VectorShiftByVector");
        return;
    case VectorRelaxedSwizzle:
        out.print("VectorRelaxedSwizzle");
        return;
    case VectorRelaxedTruncSat:
        out.print("VectorRelaxedTruncSat");
        return;
    case VectorRelaxedMAdd:
        out.print("VectorRelaxedMAdd");
        return;
    case VectorRelaxedNMAdd:
        out.print("VectorRelaxedNMAdd");
        return;
    case VectorRelaxedLaneSelect:
        out.print("VectorRelaxedLaneSelect");
        return;
    case Upsilon:
        out.print("Upsilon");
        return;
    case Phi:
        out.print("Phi");
        return;
    case Jump:
        out.print("Jump");
        return;
    case Branch:
        out.print("Branch");
        return;
    case Switch:
        out.print("Switch");
        return;
    case EntrySwitch:
        out.print("EntrySwitch");
        return;
    case Return:
        out.print("Return");
        return;
    case Oops:
        out.print("Oops");
        return;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

} // namespace WTF

#if !ASSERT_ENABLED
IGNORE_RETURN_TYPE_WARNINGS_END
#endif

#endif // ENABLE(B3_JIT)
