/*
 * Copyright (C) 2015-2021 Apple Inc. All rights reserved.
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
#include "testb3.h"
#include <wtf/WasmSIMD128.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

#if ENABLE(B3_JIT)

void testPinRegisters()
{
    auto go = [&] (bool pin) {
        Procedure proc;
        RegisterSetBuilder csrs;
        csrs.merge(RegisterSetBuilder::calleeSaveRegisters());
        csrs.exclude(RegisterSetBuilder::stackRegisters());
#if CPU(ARM)
        // FIXME We should allow this to be used. See the note
        // in https://commits.webkit.org/257808@main for more
        // info about why masm is using scratch registers on
        // ARM-only.
        csrs.remove(MacroAssembler::addressTempRegister);
#endif // CPU(ARM)
        if (pin) {
            csrs.buildAndValidate().forEach(
                [&] (Reg reg) {
                    proc.pinRegister(reg);
                });
        }
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<intptr_t, intptr_t, intptr_t>(proc, root);
        Value* a = arguments[0];
        Value* b = arguments[1];
        Value* c = arguments[2];
        Value* d = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::regCS0);
        root->appendNew<CCallValue>(
            proc, Void, Origin(),
            root->appendNew<ConstPtrValue>(proc, Origin(), static_cast<intptr_t>(0x1234)));
        root->appendNew<CCallValue>(
            proc, Void, Origin(),
            root->appendNew<ConstPtrValue>(proc, Origin(), static_cast<intptr_t>(0x1235)),
            a, b, c);
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Void, Origin());
        patchpoint->appendSomeRegister(d);
        unsigned optLevel = proc.optLevel();
        patchpoint->setGenerator(
            [&] (CCallHelpers&, const StackmapGenerationParams& params) {
                if (optLevel > 1)
                    CHECK_EQ(params[0].gpr(), GPRInfo::regCS0);
            });
        root->appendNew<Value>(proc, Return, Origin());
        auto code = compileProc(proc);
        bool usesCSRs = false;
        for (Air::BasicBlock* block : proc.code()) {
            for (Air::Inst& inst : *block) {
                if (inst.kind.opcode == Air::Patch && inst.origin == patchpoint)
                    continue;
                inst.forEachTmpFast(
                    [&] (Air::Tmp tmp) {
                        if (tmp.isReg())
                            usesCSRs |= csrs.buildAndValidate().contains(tmp.reg(), IgnoreVectors);
                    });
            }
        }
        if (proc.optLevel() < 2) {
            // Our less good register allocators may use the
            // pinned CSRs in a move.
            usesCSRs = false;
        }
        for (const RegisterAtOffset& regAtOffset : proc.calleeSaveRegisterAtOffsetList())
            usesCSRs |= csrs.buildAndValidate().contains(regAtOffset.reg(), IgnoreVectors);
        CHECK_EQ(usesCSRs, !pin);
    };

    go(true);
    go(false);
}

void testX86LeaAddAddShlLeft()
{
    // Add(Add(Shl(@x, $c), @y), $d)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        root->appendNew<Value>(
            proc, Add, Origin(),
            root->appendNew<Value>(
                proc, Shl, Origin(),
                arguments[1],
                root->appendNew<Const32Value>(proc, Origin(), 2)),
            arguments[0]),
        root->appendNew<ConstPtrValue>(proc, Origin(), 100));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea 0x64(%rdi,%rsi,4), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), (1 + (2 << 2)) + 100);
}

void testX86LeaAddAddShlRight()
{
    // Add(Add(@x, Shl(@y, $c)), $d)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        root->appendNew<Value>(
            proc, Add, Origin(),
            arguments[0],
            root->appendNew<Value>(
                proc, Shl, Origin(),
                arguments[1],
                root->appendNew<Const32Value>(proc, Origin(), 2))),
        root->appendNew<ConstPtrValue>(proc, Origin(), 100));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea 0x64(%rdi,%rsi,4), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), (1 + (2 << 2)) + 100);
}

void testX86LeaAddAdd()
{
    // Add(Add(@x, @y), $c)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        root->appendNew<Value>(
            proc, Add, Origin(),
            arguments[1],
            arguments[0]),
        root->appendNew<ConstPtrValue>(proc, Origin(), 100));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), (1 + 2) + 100);
    if (proc.optLevel() > 1) {
        checkDisassembly(
            *code,
            [&] (const char* disassembly) -> bool {
                return strstr(disassembly, "lea 0x64(%rdi,%rsi,1), %rax")
                    || strstr(disassembly, "lea 0x64(%rsi,%rdi,1), %rax");
            },
            "Expected to find something like lea 0x64(%rdi,%rsi,1), %rax but didn't!"_s);
    }
}

void testX86LeaAddShlRight()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 2)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea (%rdi,%rsi,4), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + (2 << 2));
}

void testX86LeaAddShlLeftScale1()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 0)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + 2);
    if (proc.optLevel() > 1) {
        checkDisassembly(
            *code,
            [&] (const char* disassembly) -> bool {
                return strstr(disassembly, "lea (%rdi,%rsi,1), %rax")
                    || strstr(disassembly, "lea (%rsi,%rdi,1), %rax");
            },
            "Expected to find something like lea (%rdi,%rsi,1), %rax but didn't!");
    }
}

void testX86LeaAddShlLeftScale2()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 1)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea (%rdi,%rsi,2), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + (2 << 1));
}

void testX86LeaAddShlLeftScale4()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 2)),
        arguments[0]);
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea (%rdi,%rsi,4), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + (2 << 2));
}

void testX86LeaAddShlLeftScale8()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 3)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    if (proc.optLevel() > 1)
        checkUsesInstruction(*code, "lea (%rdi,%rsi,8), %rax");
    else
        checkUsesInstruction(*code, "lea");
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + (2 << 3));
}

void testAddShl32()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int64_t, int64_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 32)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    CHECK_EQ(invoke<int64_t>(*code, static_cast<int64_t>(1), static_cast<int64_t>(2)), static_cast<int64_t>(1) + (static_cast<int64_t>(2) << static_cast<int32_t>(32)));
}

void testAddShl64()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 64)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + 2);
}

void testAddShl65()
{
    // Add(Shl(@x, $c), @y)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    Value* result = root->appendNew<Value>(
        proc, Add, Origin(),
        arguments[0],
        root->appendNew<Value>(
            proc, Shl, Origin(),
            arguments[1],
            root->appendNew<Const32Value>(proc, Origin(), 65)));
    root->appendNew<Value>(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    CHECK_EQ(invoke<intptr_t>(*code, 1, 2), 1 + (2 << 1));
}

void testReduceStrengthReassociation(bool flip)
{
    // Add(Add(@x, $c), @y) -> Add(Add(@x, @y), $c)
    // and
    // Add(@y, Add(@x, $c)) -> Add(Add(@x, @y), $c)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int, int>(proc, root);
    Value* arg1 = arguments[0];
    Value* arg2 = arguments[1];

    Value* innerAdd = root->appendNew<Value>(
        proc, Add, Origin(), arg1,
        root->appendNew<ConstPtrValue>(proc, Origin(), 42));

    Value* outerAdd;
    if (flip)
        outerAdd = root->appendNew<Value>(proc, Add, Origin(), arg2, innerAdd);
    else
        outerAdd = root->appendNew<Value>(proc, Add, Origin(), innerAdd, arg2);

    root->appendNew<Value>(proc, Return, Origin(), outerAdd);

    proc.resetReachability();

    if (shouldBeVerbose(proc)) {
        dataLog("IR before reduceStrength:\n");
        dataLog(proc);
    }

    reduceStrength(proc);

    if (shouldBeVerbose(proc)) {
        dataLog("IR after reduceStrength:\n");
        dataLog(proc);
    }

    CHECK_EQ(root->last()->opcode(), Return);
    CHECK_EQ(root->last()->child(0)->opcode(), Add);
    CHECK(root->last()->child(0)->child(1)->isIntPtr(42));
    CHECK_EQ(root->last()->child(0)->child(0)->opcode(), Add);
    CHECK(
        (root->last()->child(0)->child(0)->child(0) == arg1 && root->last()->child(0)->child(0)->child(1) == arg2) ||
        (root->last()->child(0)->child(0)->child(0) == arg2 && root->last()->child(0)->child(0)->child(1) == arg1));
}

template<typename B3ContType, typename Type64, typename Type32>
void testReduceStrengthTruncConstant(Type64 filler, Type32 value)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    int64_t bits = std::bit_cast<int64_t>(filler);
    int32_t loBits = std::bit_cast<int32_t>(value);
    bits = ((bits >> 32) << 32) | loBits;
    Type64 testValue = std::bit_cast<Type64>(bits);

    Value* b2  = root->appendNew<B3ContType>(proc, Origin(), testValue);
    Value* b3  = root->appendNew<Value>(proc, JSC::B3::Trunc, Origin(), b2);
    root->appendNew<Value>(proc, Return, Origin(), b3);

    proc.resetReachability();

    reduceStrength(proc);

    CHECK_EQ(root->last()->opcode(), Return);
    if constexpr (std::is_same_v<B3ContType, ConstDoubleValue>) {
        CHECK_EQ(root->last()->child(0)->opcode(), ConstFloat);
        CHECK_EQ(std::bit_cast<int32_t>(root->last()->child(0)->asFloat()), std::bit_cast<int32_t>(value));
    } else
        CHECK(root->last()->child(0)->isInt32(value));
}

void testReduceStrengthTruncInt64Constant(int64_t filler, int32_t value)
{
    testReduceStrengthTruncConstant<Const64Value>(filler, value);
}

void testReduceStrengthTruncDoubleConstant(double filler, float value)
{
    testReduceStrengthTruncConstant<ConstDoubleValue>(filler, value);
}

void testLoadBaseIndexShift2()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    root->appendNew<Value>(
        proc, Return, Origin(),
        root->appendNew<MemoryValue>(
            proc, Load, Int32, Origin(),
            root->appendNew<Value>(
                proc, Add, Origin(),
                arguments[0],
                root->appendNew<Value>(
                    proc, Shl, Origin(),
                    arguments[1],
                    root->appendNew<Const32Value>(proc, Origin(), 2)))));
    auto code = compileProc(proc);
    if (isX86() && proc.optLevel() > 1)
        checkUsesInstruction(*code, "(%rdi,%rsi,4)");
    int32_t value = 12341234;
    char* ptr = std::bit_cast<char*>(&value);
    for (unsigned i = 0; i < 10; ++i)
        CHECK_EQ(invoke<int32_t>(*code, ptr - (static_cast<intptr_t>(1) << static_cast<intptr_t>(2)) * i, i), 12341234);
}

void testLoadBaseIndexShift32()
{
#if CPU(ADDRESS64)
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);
    root->appendNew<Value>(
        proc, Return, Origin(),
        root->appendNew<MemoryValue>(
            proc, Load, Int32, Origin(),
            root->appendNew<Value>(
                proc, Add, Origin(),
                arguments[0],
                root->appendNew<Value>(
                    proc, Shl, Origin(),
                    arguments[1],
                    root->appendNew<Const32Value>(proc, Origin(), 32)))));
    auto code = compileProc(proc);
    int32_t value = 12341234;
    char* ptr = std::bit_cast<char*>(&value);
    for (unsigned i = 0; i < 10; ++i)
        CHECK_EQ(invoke<int32_t>(*code, ptr - (static_cast<intptr_t>(1) << static_cast<intptr_t>(32)) * i, i), 12341234);
#endif
}

void testOptimizeMaterialization()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    if constexpr (is32Bit())
        return;

    BasicBlock* root = proc.addBlock();
    root->appendNew<CCallValue>(
        proc, Void, Origin(),
        root->appendNew<ConstPtrValue>(proc, Origin(), 0x123423453456llu),
        root->appendNew<ConstPtrValue>(proc, Origin(), 0x123423453456llu + 35));
    root->appendNew<Value>(proc, Return, Origin());

    auto code = compileProc(proc);
    bool found = false;
    for (Air::BasicBlock* block : proc.code()) {
        for (Air::Inst& inst : *block) {
            if (inst.kind.opcode != Air::Add64)
                continue;
            if (inst.args[0] != Air::Arg::imm(35))
                continue;
            found = true;
        }
    }
    CHECK(found);
}

template<typename Func>
void generateLoop(Procedure& proc, const Func& func)
{
    BasicBlock* root = proc.addBlock();
    BasicBlock* loop = proc.addBlock();
    BasicBlock* end = proc.addBlock();

    UpsilonValue* initialIndex = root->appendNew<UpsilonValue>(
        proc, Origin(), root->appendNew<Const32Value>(proc, Origin(), 0));
    root->appendNew<Value>(proc, Jump, Origin());
    root->setSuccessors(loop);

    Value* index = loop->appendNew<Value>(proc, Phi, Int32, Origin());
    initialIndex->setPhi(index);

    Value* one = func(loop, index);

    Value* nextIndex = loop->appendNew<Value>(proc, Add, Origin(), index, one);
    UpsilonValue* loopIndex = loop->appendNew<UpsilonValue>(proc, Origin(), nextIndex);
    loopIndex->setPhi(index);
    loop->appendNew<Value>(
        proc, Branch, Origin(),
        loop->appendNew<Value>(
            proc, LessThan, Origin(), nextIndex,
            loop->appendNew<Const32Value>(proc, Origin(), 100)));
    loop->setSuccessors(loop, end);

    end->appendNew<Value>(proc, Return, Origin());
}

static std::array<int, 100> makeArrayForLoops()
{
    std::array<int, 100> result;
    for (unsigned i = 0; i < result.size(); ++i)
        result[i] = i & 1;
    return result;
}

template<typename Func>
void generateLoopNotBackwardsDominant(Procedure& proc, std::array<int, 100>& array, const Func& func)
{
    BasicBlock* root = proc.addBlock();
    BasicBlock* loopHeader = proc.addBlock();
    BasicBlock* loopCall = proc.addBlock();
    BasicBlock* loopFooter = proc.addBlock();
    BasicBlock* end = proc.addBlock();

    UpsilonValue* initialIndex = root->appendNew<UpsilonValue>(
        proc, Origin(), root->appendNew<Const32Value>(proc, Origin(), 0));
    // If you look carefully, you'll notice that this is an extremely sneaky use of Upsilon that demonstrates
    // the extent to which our SSA is different from normal-person SSA.
    UpsilonValue* defaultOne = root->appendNew<UpsilonValue>(
        proc, Origin(), root->appendNew<Const32Value>(proc, Origin(), 1));
    root->appendNew<Value>(proc, Jump, Origin());
    root->setSuccessors(loopHeader);

    Value* index = loopHeader->appendNew<Value>(proc, Phi, Int32, Origin());
    initialIndex->setPhi(index);

    // if (array[index])
    loopHeader->appendNew<Value>(
        proc, Branch, Origin(),
        loopHeader->appendNew<MemoryValue>(
            proc, Load, Int32, Origin(),
            loopHeader->appendNew<Value>(
                proc, Add, Origin(),
                loopHeader->appendNew<ConstPtrValue>(proc, Origin(), &array),
                loopHeader->appendNew<Value>(
                    proc, Mul, Origin(),
                    is32Bit() ? index : loopHeader->appendNew<Value>(proc, ZExt32, Origin(), index),
                    loopHeader->appendNew<ConstPtrValue>(proc, Origin(), sizeof(int))))));
    loopHeader->setSuccessors(loopCall, loopFooter);

    Value* functionCall = func(loopCall, index);
    UpsilonValue* oneFromFunction = loopCall->appendNew<UpsilonValue>(proc, Origin(), functionCall);
    loopCall->appendNew<Value>(proc, Jump, Origin());
    loopCall->setSuccessors(loopFooter);

    Value* one = loopFooter->appendNew<Value>(proc, Phi, Int32, Origin());
    defaultOne->setPhi(one);
    oneFromFunction->setPhi(one);
    Value* nextIndex = loopFooter->appendNew<Value>(proc, Add, Origin(), index, one);
    UpsilonValue* loopIndex = loopFooter->appendNew<UpsilonValue>(proc, Origin(), nextIndex);
    loopIndex->setPhi(index);
    loopFooter->appendNew<Value>(
        proc, Branch, Origin(),
        loopFooter->appendNew<Value>(
            proc, LessThan, Origin(), nextIndex,
            loopFooter->appendNew<Const32Value>(proc, Origin(), 100)));
    loopFooter->setSuccessors(loopHeader, end);

    end->appendNew<Value>(proc, Return, Origin());
}

extern "C" {
static JSC_DECLARE_NOEXCEPT_JIT_OPERATION_WITHOUT_WTF_INTERNAL(oneFunction, int, (int* callCount));
}
JSC_DEFINE_NOEXCEPT_JIT_OPERATION(oneFunction, int, (int* callCount))
{
    (*callCount)++;
    return 1;
}

extern "C" {
static JSC_DECLARE_NOEXCEPT_JIT_OPERATION_WITHOUT_WTF_INTERNAL(noOpFunction, void, ());
}
JSC_DEFINE_NOEXCEPT_JIT_OPERATION(noOpFunction, void, ())
{
}

void testLICMPure()
{
    Procedure proc;

    if (proc.optLevel() < 2)
        return;

    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<intptr_t>(proc, loop);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMPureSideExits()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<intptr_t>(proc, loop);
            Effects effects = Effects::none();
            effects.exitsSideways = true;
            effects.reads = HeapRange::top();
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));

            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMPureWritesPinned()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writesPinned = true;
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));

            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMPureWrites()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writes = HeapRange(63479);
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));

            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMReadsLocalState()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.readsLocalState = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u); // We'll fail to hoist because the loop has Upsilons.
}

void testLICMReadsPinned()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.readsPinned = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMReads()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.reads = HeapRange::top();
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMPureNotBackwardsDominant()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    auto array = makeArrayForLoops();
    generateLoopNotBackwardsDominant(
        proc, array,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMPureFoiledByChild()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value* index) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0],
                index);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMPureNotBackwardsDominantFoiledByChild()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    auto array = makeArrayForLoops();
    generateLoopNotBackwardsDominant(
        proc, array,
        [&] (BasicBlock* loop, Value* index) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), Effects::none(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0],
                index);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 50u);
}

void testLICMExitsSideways()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.exitsSideways = true;
            effects.reads = HeapRange::top();
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMWritesLocalState()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writesLocalState = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMWrites()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writes = HeapRange(666);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMFence()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.fence = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMWritesPinned()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writesPinned = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMControlDependent()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.controlDependent = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMControlDependentNotBackwardsDominant()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    auto array = makeArrayForLoops();
    generateLoopNotBackwardsDominant(
        proc, array,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.controlDependent = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 50u);
}

void testLICMControlDependentSideExits()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.exitsSideways = true;
            effects.reads = HeapRange::top();
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));
        
            effects = Effects::none();
            effects.controlDependent = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMReadsPinnedWritesPinned()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writesPinned = true;
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));
        
            effects = Effects::none();
            effects.readsPinned = true;
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMReadsWritesDifferentHeaps()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writes = HeapRange(6436);
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));
        
            effects = Effects::none();
            effects.reads = HeapRange(4886);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 1u);
}

void testLICMReadsWritesOverlappingHeaps()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            Effects effects = Effects::none();
            effects.writes = HeapRange(6436, 74458);
            loop->appendNew<CCallValue>(
                proc, Void, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(noOpFunction)));
        
            effects = Effects::none();
            effects.reads = HeapRange(48864, 78239);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(), effects,
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testLICMDefaultCall()
{
    Procedure proc;
    generateLoop(
        proc,
        [&] (BasicBlock* loop, Value*) -> Value* {
            auto arguments = cCallArgumentValues<unsigned*>(proc, loop);
            return loop->appendNew<CCallValue>(
                proc, Int32, Origin(),
                loop->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(oneFunction)),
                arguments[0]);
        });

    unsigned callCount = 0;
    compileAndRun<void>(proc, &callCount);
    CHECK_EQ(callCount, 100u);
}

void testDepend32()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int32_t*>(proc, root);
    Value* ptr = arguments[0];
    Value* first = root->appendNew<MemoryValue>(proc, Load, Int32, Origin(), ptr, 0);
    Value* depend = root->appendNew<Value>(proc, Depend, Origin(), first);
    if constexpr (!is32Bit())
        depend = root->appendNew<Value>(proc, ZExt32, Origin(), depend);
    Value* second = root->appendNew<MemoryValue>(
        proc, Load, Int32, Origin(),
        root->appendNew<Value>(
            proc, Add, Origin(), ptr,
            depend),
        4);
    root->appendNew<Value>(
        proc, Return, Origin(),
        root->appendNew<Value>(proc, Add, Origin(), first, second));

    int32_t values[2];
    values[0] = 42;
    values[1] = 0xbeef;

    auto code = compileProc(proc);
    if (isARM64() || isARM_THUMB2())
        checkUsesInstruction(*code, "eor");
    else if (isX86()) {
        checkDoesNotUseInstruction(*code, "mfence");
        checkDoesNotUseInstruction(*code, "lock");
    }
    CHECK_EQ(invoke<int32_t>(*code, values), 42 + 0xbeef);
}

void testDepend64()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int64_t*>(proc, root);
    Value* ptr = arguments[0];
    Value* first = root->appendNew<MemoryValue>(proc, Load, Int64, Origin(), ptr, 0);
    Value* second = root->appendNew<MemoryValue>(
        proc, Load, Int64, Origin(),
        root->appendNew<Value>(
            proc, Add, Origin(), ptr,
            root->appendNew<Value>(proc, Depend, Origin(), first)),
        8);
    root->appendNew<Value>(
        proc, Return, Origin(),
        root->appendNew<Value>(proc, Add, Origin(), first, second));

    int64_t values[2];
    values[0] = 42;
    values[1] = 0xbeef;

    auto code = compileProc(proc);
    if (isARM64())
        checkUsesInstruction(*code, "eor");
    else if (isX86()) {
        checkDoesNotUseInstruction(*code, "mfence");
        checkDoesNotUseInstruction(*code, "lock");
    }
    CHECK_EQ(invoke<int64_t>(*code, values), 42 + 0xbeef);
}

void testWasmBoundsCheck(unsigned offset)
{
    Procedure proc;
    if (proc.optLevel() < 1)
        return;
    GPRReg pinned = GPRInfo::argumentGPR1;
    proc.pinRegister(pinned);

    proc.setWasmBoundsCheckGenerator([=](CCallHelpers& jit, WasmBoundsCheckValue*, GPRReg pinnedGPR) {
        CHECK_EQ(pinnedGPR, pinned);

        // This should always work because a function this simple should never have callee
        // saves.
        jit.move(CCallHelpers::TrustedImm32(42), GPRInfo::returnValueGPR);
        jit.emitFunctionEpilogue();
        jit.ret();
    });

    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int32_t>(proc, root);
    root->appendNew<WasmBoundsCheckValue>(proc, Origin(), pinned, arguments[0], offset);
    Value* result = root->appendNew<Const32Value>(proc, Origin(), 0x42);
    root->appendNewControlValue(proc, Return, Origin(), result);

    auto code = compileProc(proc);
    uint32_t bound = 2 + offset;
    auto computeResult = [&] (uint32_t input) {
        return input + offset < bound ? 0x42 : 42;
    };

    CHECK_EQ(invoke<int32_t>(*code, 1, bound), computeResult(1));
    CHECK_EQ(invoke<int32_t>(*code, 3, bound), computeResult(3));
    CHECK_EQ(invoke<int32_t>(*code, 2, bound), computeResult(2));
}

void testWasmAddress()
{
    Procedure proc;
    GPRReg pinnedGPR = GPRInfo::argumentGPR2;
    proc.pinRegister(pinnedGPR);

    unsigned loopCount = 100;
    Vector<unsigned> values(loopCount);
    unsigned numToStore = 42;

    BasicBlock* root = proc.addBlock();
    BasicBlock* header = proc.addBlock();
    BasicBlock* body = proc.addBlock();
    BasicBlock* continuation = proc.addBlock();
    auto arguments = cCallArgumentValues<unsigned, unsigned, unsigned*>(proc, root);

    // Root
    Value* loopCountValue = arguments[0];
    Value* valueToStore = arguments[1];
    UpsilonValue* beginUpsilon = root->appendNew<UpsilonValue>(proc, Origin(), root->appendNew<Const32Value>(proc, Origin(), 0));
    root->appendNewControlValue(proc, Jump, Origin(), header);

    // Header
    Value* indexPhi = header->appendNew<Value>(proc, Phi, Int32, Origin());
    header->appendNewControlValue(proc, Branch, Origin(),
        header->appendNew<Value>(proc, Below, Origin(), indexPhi, loopCountValue),
        body, continuation);

    // Body
    Value* pointer = body->appendNew<Value>(proc, Mul, Origin(), indexPhi,
        body->appendNew<Const32Value>(proc, Origin(), sizeof(unsigned)));
    if (!is32Bit())
        pointer = body->appendNew<Value>(proc, ZExt32, Origin(), pointer);
    body->appendNew<MemoryValue>(proc, Store, Origin(), valueToStore,
        body->appendNew<WasmAddressValue>(proc, Origin(), pointer, pinnedGPR), 0);
    UpsilonValue* incUpsilon = body->appendNew<UpsilonValue>(proc, Origin(),
        body->appendNew<Value>(proc, Add, Origin(), indexPhi,
            body->appendNew<Const32Value>(proc, Origin(), 1)));
    body->appendNewControlValue(proc, Jump, Origin(), header);

    // Continuation
    continuation->appendNewControlValue(proc, Return, Origin());

    beginUpsilon->setPhi(indexPhi);
    incUpsilon->setPhi(indexPhi);


    auto code = compileProc(proc);
    invoke<void>(*code, loopCount, numToStore, values.span().data());
    for (unsigned value : values)
        CHECK_EQ(numToStore, value);
}

void testWasmAddressWithOffset()
{
    Procedure proc;
    GPRReg pinnedGPR = GPRInfo::argumentGPR2;
    proc.pinRegister(pinnedGPR);

    Vector<uint8_t> values(3);
    values[0] = 20;
    values[1] = 21;
    values[2] = 22;
    uint8_t numToStore = 42;

    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int32_t, uint32_t, uint8_t*>(proc, root);
    // Root
    Value* offset = arguments[0];
    Value* valueToStore = arguments[1];
    Value* pointer = offset;
    if (!is32Bit())
        pointer = root->appendNew<Value>(proc, ZExt32, Origin(), offset);
    root->appendNew<MemoryValue>(proc, Store8, Origin(), valueToStore, root->appendNew<WasmAddressValue>(proc, Origin(), pointer, pinnedGPR), 1);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);
    invoke<void>(*code, 1, numToStore, values.span().data());
    CHECK_EQ(20U, values[0]);
    CHECK_EQ(21U, values[1]);
    CHECK_EQ(42U, values[2]);
}

void testFastTLSLoad()
{
#if ENABLE(FAST_TLS_JIT)
    _pthread_setspecific_direct(WTF_TESTING_KEY, std::bit_cast<void*>(static_cast<uintptr_t>(0xbeef)));

    Procedure proc;
    BasicBlock* root = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, pointerType(), Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->setGenerator(
        [&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            jit.loadFromTLSPtr(fastTLSOffsetForKey(WTF_TESTING_KEY), params[0].gpr());
        });

    root->appendNew<Value>(proc, Return, Origin(), patchpoint);

    CHECK_EQ(compileAndRun<uintptr_t>(proc), static_cast<uintptr_t>(0xbeef));
#endif
}

void testFastTLSStore()
{
#if ENABLE(FAST_TLS_JIT)
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Void, Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->numGPScratchRegisters = 1;
    patchpoint->setGenerator(
        [&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            GPRReg scratch = params.gpScratch(0);
            jit.move(CCallHelpers::TrustedImm32(0xdead), scratch);
            jit.storeToTLSPtr(scratch, fastTLSOffsetForKey(WTF_TESTING_KEY));
        });

    root->appendNewControlValue(proc, Return, Origin());

    compileAndRun<void>(proc);
    CHECK_EQ(std::bit_cast<uintptr_t>(_pthread_getspecific_direct(WTF_TESTING_KEY)), static_cast<uintptr_t>(0xdead));
#endif
}

static NEVER_INLINE bool doubleEq(double a, double b) { return a == b; }
static NEVER_INLINE bool doubleNeq(double a, double b) { return a != b; }
static NEVER_INLINE bool doubleGt(double a, double b) { return a > b; }
static NEVER_INLINE bool doubleGte(double a, double b) { return a >= b; }
static NEVER_INLINE bool doubleLt(double a, double b) { return a < b; }
static NEVER_INLINE bool doubleLte(double a, double b) { return a <= b; }

void testDoubleLiteralComparison(double a, double b)
{
    using Test = std::tuple<B3::Opcode, bool (*)(double, double)>;
    StdList<Test> tests = {
        Test { NotEqual, doubleNeq },
        Test { Equal, doubleEq },
        Test { EqualOrUnordered, doubleEq },
        Test { GreaterThan, doubleGt },
        Test { GreaterEqual, doubleGte },
        Test { LessThan, doubleLt },
        Test { LessEqual, doubleLte },
    };

    for (const Test& test : tests) {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        Value* valueA = root->appendNew<ConstDoubleValue>(proc, Origin(), a);
        Value* valueB = root->appendNew<ConstDoubleValue>(proc, Origin(), b);

        // This is here just to make reduceDoubleToFloat do things.
        Value* valueC = root->appendNew<ConstDoubleValue>(proc, Origin(), 0.0);
        Value* valueAsFloat = root->appendNew<Value>(proc, DoubleToFloat, Origin(), valueC);

        root->appendNewControlValue(
            proc, Return, Origin(),
                root->appendNew<Value>(proc, BitAnd, Origin(),
                    root->appendNew<Value>(proc, std::get<0>(test), Origin(), valueA, valueB),
                    root->appendNew<Value>(proc, Equal, Origin(), valueAsFloat, valueAsFloat)));

        CHECK_EQ(!!compileAndRun<int32_t>(proc), std::get<1>(test)(a, b));
    }
}

void testFloatEqualOrUnorderedFolding()
{
    for (auto& first : floatingPointOperands<float>()) {
        for (auto& second : floatingPointOperands<float>()) {
            float a = first.value;
            float b = second.value;
            bool expectedResult = (a == b) || std::isunordered(a, b);
            Procedure proc;
            BasicBlock* root = proc.addBlock();
            Value* constA = root->appendNew<ConstFloatValue>(proc, Origin(), a);
            Value* constB = root->appendNew<ConstFloatValue>(proc, Origin(), b);

            root->appendNewControlValue(proc, Return, Origin(),
                root->appendNew<Value>(
                    proc, EqualOrUnordered, Origin(),
                    constA,
                    constB));
            CHECK_EQ(!!compileAndRun<int32_t>(proc), expectedResult);
        }
    }
}

void testFloatEqualOrUnorderedFoldingNaN()
{
    StdList<float> nans = {
        std::bit_cast<float>(0xfffffffd),
        std::bit_cast<float>(0xfffffffe),
        std::bit_cast<float>(0xfffffff0),
        static_cast<float>(PNaN),
    };

    unsigned i = 0;
    for (float nan : nans) {
        RELEASE_ASSERT(std::isnan(nan));
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<double>(proc, root);
        Value* a = root->appendNew<ConstFloatValue>(proc, Origin(), nan);
        Value* b = root->appendNew<Value>(proc, DoubleToFloat, Origin(), arguments[0]);

        if (i % 2)
            std::swap(a, b);
        ++i;
        root->appendNewControlValue(proc, Return, Origin(),
            root->appendNew<Value>(proc, EqualOrUnordered, Origin(), a, b));
        CHECK(!!compileAndRun<int32_t>(proc, static_cast<double>(1.0)));
    }
}

void testFloatEqualOrUnorderedDontFold()
{
    for (auto& first : floatingPointOperands<float>()) {
        float a = first.value;
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<double>(proc, root);
        Value* constA = root->appendNew<ConstFloatValue>(proc, Origin(), a);
        Value* b = root->appendNew<Value>(proc, DoubleToFloat, Origin(), arguments[0]);
        root->appendNewControlValue(proc, Return, Origin(),
            root->appendNew<Value>(
                proc, EqualOrUnordered, Origin(), constA, b));

        auto code = compileProc(proc);

        for (auto& second : floatingPointOperands<float>()) {
            float b = second.value;
            bool expectedResult = (a == b) || std::isunordered(a, b);
            CHECK_EQ(!!invoke<int32_t>(*code, static_cast<double>(b)), expectedResult);
        }
    }
}

extern "C" {
static JSC_DECLARE_NOEXCEPT_JIT_OPERATION_WITHOUT_WTF_INTERNAL(functionNineArgs, void, (int32_t, void*, void*, void*, void*, void*, void*, void*, void*));
}
JSC_DEFINE_NOEXCEPT_JIT_OPERATION(functionNineArgs, void, (int32_t, void*, void*, void*, void*, void*, void*, void*, void*))
{
}

void testShuffleDoesntTrashCalleeSaves()
{
    Procedure proc;

    BasicBlock* root = proc.addBlock();
    BasicBlock* likely = proc.addBlock();
    BasicBlock* unlikely = proc.addBlock();

    auto regs = RegisterSetBuilder::registersToSaveForCCall(RegisterSetBuilder::allScalarRegisters());

    unsigned i = 0;
    Vector<Value*> patches;
    for (Reg reg : regs.buildAndValidate()) {
        if (RegisterSetBuilder::argumentGPRs().contains(reg, IgnoreVectors) || !reg.isGPR())
            continue;
        ++i;
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Int32, Origin());
        patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
        patchpoint->resultConstraints = { ValueRep::reg(reg.gpr()) };
        patchpoint->setGenerator(
            [=] (CCallHelpers& jit, const StackmapGenerationParams& params) {
                AllowMacroScratchRegisterUsage allowScratch(jit);
                jit.move(CCallHelpers::TrustedImm32(i), params[0].gpr());
            });
        patches.append(patchpoint);
    }

    Value* arg1 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(0 % GPRInfo::numberOfArgumentRegisters));
    Value* arg2 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(1 % GPRInfo::numberOfArgumentRegisters));
    Value* arg3 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(2 % GPRInfo::numberOfArgumentRegisters));
    Value* arg4 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(3 % GPRInfo::numberOfArgumentRegisters));
    Value* arg5 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(4 % GPRInfo::numberOfArgumentRegisters));
    Value* arg6 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(5 % GPRInfo::numberOfArgumentRegisters));
    Value* arg7 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(6 % GPRInfo::numberOfArgumentRegisters));
    Value* arg8 = root->appendNew<ArgumentRegValue>(proc, Origin(), GPRInfo::toArgumentRegister(7 % GPRInfo::numberOfArgumentRegisters));

    PatchpointValue* ptr = root->appendNew<PatchpointValue>(proc, pointerType(), Origin());
    ptr->clobber(RegisterSetBuilder::macroClobberedGPRs());
    ptr->resultConstraints = { ValueRep::reg(GPRInfo::regCS0) };
    ptr->appendSomeRegister(arg1);
    ptr->setGenerator(
        [=] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            jit.move(params[1].gpr(), params[0].gpr());
        });

    Value* condition = root->appendNew<Value>(
        proc, Equal, Origin(), 
        ptr,
        root->appendNew<ConstPtrValue>(proc, Origin(), 0));

    root->appendNewControlValue(
        proc, Branch, Origin(),
        condition,
        FrequentedBlock(likely, FrequencyClass::Normal), FrequentedBlock(unlikely, FrequencyClass::Rare));

    // Never executes.
    Value* const42 = likely->appendNew<Const32Value>(proc, Origin(), 42);
    likely->appendNewControlValue(proc, Return, Origin(), const42);

    // Always executes.
    Value* constNumber = unlikely->appendNew<Const32Value>(proc, Origin(), 0x1);

    unlikely->appendNew<CCallValue>(
        proc, Void, Origin(),
        unlikely->appendNew<ConstPtrValue>(proc, Origin(), tagCFunction<OperationPtrTag>(functionNineArgs)),
        constNumber, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);

    PatchpointValue* voidPatch = unlikely->appendNew<PatchpointValue>(proc, Void, Origin());
    voidPatch->clobber(RegisterSetBuilder::macroClobberedGPRs());
    for (Value* v : patches)
        voidPatch->appendSomeRegister(v);
    voidPatch->appendSomeRegister(arg1);
    voidPatch->appendSomeRegister(arg2);
    voidPatch->appendSomeRegister(arg3);
    voidPatch->appendSomeRegister(arg4);
    voidPatch->appendSomeRegister(arg5);
    voidPatch->appendSomeRegister(arg6);
    voidPatch->setGenerator([=] (CCallHelpers&, const StackmapGenerationParams&) { });

    unlikely->appendNewControlValue(proc, Return, Origin(),
        unlikely->appendNew<MemoryValue>(proc, Load, Int32, Origin(), ptr));

    int32_t* inputPtr = static_cast<int32_t*>(fastMalloc(sizeof(int32_t)));
    *inputPtr = 48;
    CHECK_EQ(compileAndRun<int32_t>(proc, inputPtr), 48);
    fastFree(inputPtr);
}

void testDemotePatchpointTerminal()
{
    Procedure proc;

    BasicBlock* root = proc.addBlock();
    BasicBlock* done = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Int32, Origin());
    patchpoint->effects.terminal = true;
    root->setSuccessors(done);

    done->appendNew<Value>(proc, Return, Origin(), patchpoint);

    proc.resetReachability();
    breakCriticalEdges(proc);
    IndexSet<Value*> valuesToDemote;
    valuesToDemote.add(patchpoint);
    demoteValues(proc, valuesToDemote);
    validate(proc);
}

void testReportUsedRegistersLateUseFollowedByEarlyDefDoesNotMarkUseAsDead()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    BasicBlock* root = proc.addBlock();

    RegisterSetBuilder allRegs = RegisterSetBuilder::allGPRs();
    allRegs.exclude(RegisterSetBuilder::stackRegisters());
    allRegs.exclude(RegisterSetBuilder::reservedHardwareRegisters());

    {
        // Make every reg 42 (just needs to be a value other than 10).
        Value* const42 = root->appendNew<Const32Value>(proc, Origin(), 42);
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Void, Origin());
        for (Reg reg : allRegs.buildAndValidate())
            patchpoint->append(const42, ValueRep::reg(reg));
        patchpoint->setGenerator([&] (CCallHelpers&, const StackmapGenerationParams&) { });
    }

    {
        Value* const10 = root->appendNew<Const32Value>(proc, Origin(), 10);
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Void, Origin());
        for (Reg reg : allRegs.buildAndValidate())
            patchpoint->append(const10, ValueRep::lateReg(reg));
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams&) {
            for (Reg reg : allRegs.buildAndValidate()) {
                auto done = jit.branch32(CCallHelpers::Equal, reg.gpr(), CCallHelpers::TrustedImm32(10));
                jit.breakpoint();
                done.link(&jit);
            }
        });
    }

    {
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, Int32, Origin());
        patchpoint->resultConstraints = { ValueRep::SomeEarlyRegister };
        patchpoint->setGenerator([&] (CCallHelpers&, const StackmapGenerationParams& params) {
            RELEASE_ASSERT(allRegs.buildAndValidate().contains(params[0].gpr(), IgnoreVectors));
        });
    }

    root->appendNewControlValue(proc, Return, Origin());

    compileAndRun<void>(proc);
}

void testInfiniteLoopDoesntCauseBadHoisting()
{
    Procedure proc;
    if (proc.optLevel() < 2)
        return;
    BasicBlock* root = proc.addBlock();
    BasicBlock* header = proc.addBlock();
    BasicBlock* loadBlock = proc.addBlock();
    BasicBlock* postLoadBlock = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t>(proc, root);

    Value* arg = arguments[0];
    root->appendNewControlValue(proc, Jump, Origin(), header);

    header->appendNewControlValue(
        proc, Branch, Origin(),
        header->appendNew<Value>(proc, Equal, Origin(),
            arg,
            header->appendNew<ConstPtrValue>(proc, Origin(), 10)), header, loadBlock);

    PatchpointValue* patchpoint = loadBlock->appendNew<PatchpointValue>(proc, Void, Origin());
    patchpoint->effects = Effects::none();
    patchpoint->effects.writesLocalState = true; // Don't DCE this.
    patchpoint->setGenerator(
        [&] (CCallHelpers& jit, const StackmapGenerationParams&) {
            // This works because we don't have callee saves.
            jit.emitFunctionEpilogue();
            jit.ret();
        });

    Value* badLoad = loadBlock->appendNew<MemoryValue>(proc, Load, Int64, Origin(), arg, 0);

    loadBlock->appendNewControlValue(
        proc, Branch, Origin(),
        loadBlock->appendNew<Value>(proc, Equal, Origin(),
            badLoad,
            loadBlock->appendNew<Const64Value>(proc, Origin(), 45)), header, postLoadBlock);

    postLoadBlock->appendNewControlValue(proc, Return, Origin(), badLoad);

    // The patchpoint early ret() works because we don't have callee saves.
    auto code = compileProc(proc);
    RELEASE_ASSERT(!proc.calleeSaveRegisterAtOffsetList().registerCount());
    invoke<void>(*code, static_cast<intptr_t>(55)); // Shouldn't crash dereferncing 55.
}

static void testSimpleTuplePair(unsigned first, int64_t second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, proc.addTuple({ Int32, Int64 }), Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::SomeRegister };
    patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
        AllowMacroScratchRegisterUsage allowScratch(jit);
        jit.move(CCallHelpers::TrustedImm32(first), params[0].gpr());
        jit.move(CCallHelpers::TrustedImmPtr(second), params[1].gpr());
    });
    Value* i32 = root->appendNew<Value>(proc, ZExt32, Origin(),
        root->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 0));
    Value* i64 = root->appendNew<ExtractValue>(proc, Origin(), Int64, patchpoint, 1);
    root->appendNew<Value>(proc, Return, Origin(), root->appendNew<Value>(proc, Add, Origin(), i32, i64));

    CHECK_EQ(compileAndRun<int64_t>(proc), first + second);
}

static void testSimpleTuplePairUnused(unsigned first, int64_t second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, proc.addTuple({ Int32, Int64, Double }), Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::SomeRegister, ValueRep::SomeRegister };
    patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
        AllowMacroScratchRegisterUsage allowScratch(jit);
        jit.move(CCallHelpers::TrustedImm32(first), params[0].gpr());
#if !CPU(ARM_THUMB2) // FIXME
        jit.move(CCallHelpers::TrustedImm64(second), params[1].gpr());
        jit.move64ToDouble(CCallHelpers::Imm64(std::bit_cast<uint64_t>(0.0)), params[2].fpr());
#endif
    });
    Value* i32 = root->appendNew<Value>(proc, ZExt32, Origin(),
        root->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 0));
    Value* i64 = root->appendNew<ExtractValue>(proc, Origin(), Int64, patchpoint, 1);
    root->appendNew<Value>(proc, Return, Origin(), root->appendNew<Value>(proc, Add, Origin(), i32, i64));

    CHECK_EQ(compileAndRun<int64_t>(proc), first + second);
}

static void testSimpleTuplePairStack(unsigned first, int64_t second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, proc.addTuple({ Int32, Int64 }), Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::stackArgument(0) };
    patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
        AllowMacroScratchRegisterUsage allowScratch(jit);
        jit.move(CCallHelpers::TrustedImm32(first), params[0].gpr());
#if !CPU(ARM_THUMB2) // FIXME
        jit.store64(CCallHelpers::TrustedImm64(second), CCallHelpers::Address(CCallHelpers::framePointerRegister, params[1].offsetFromFP()));
#endif
    });
    Value* i32 = root->appendNew<Value>(proc, ZExt32, Origin(),
        root->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 0));
    Value* i64 = root->appendNew<ExtractValue>(proc, Origin(), Int64, patchpoint, 1);
    root->appendNew<Value>(proc, Return, Origin(), root->appendNew<Value>(proc, Add, Origin(), i32, i64));

    CHECK_EQ(compileAndRun<int64_t>(proc), first + second);
}

template<B3::TypeKind kind, typename ResultType>
static void testBottomTupleValue()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    auto* tuple = root->appendNew<BottomTupleValue>(proc, Origin(), proc.addTuple({
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
    }));
    Value* result = root->appendNew<ExtractValue>(proc, Origin(), kind, tuple, 0);
    root->appendNew<Value>(proc, Return, Origin(), result);
    CHECK_EQ(compileAndRun<ResultType>(proc), 0);
}

template<B3::TypeKind kind, typename ResultType>
static void testTouchAllBottomTupleValue()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();

    Type tupleType = proc.addTuple({
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
        kind, kind, kind, kind, kind,
    });
    auto* tuple = root->appendNew<BottomTupleValue>(proc, Origin(), tupleType);
    Value* result = root->appendNew<ExtractValue>(proc, Origin(), kind, tuple, 0);
    for (unsigned index = 1; index < proc.resultCount(tupleType); ++index)
        result = root->appendNew<Value>(proc, Add, Origin(), result, root->appendNew<ExtractValue>(proc, Origin(), kind, tuple, index));
    root->appendNew<Value>(proc, Return, Origin(), result);
    CHECK_EQ(compileAndRun<ResultType>(proc), 0);
}

template<bool shouldFixSSA>
static void tailDupedTuplePair(unsigned first, double second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    BasicBlock* truthy = proc.addBlock();
    BasicBlock* falsey = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t>(proc, root);

    Type tupleType = proc.addTuple({ Int32, Double });
    Variable* var = proc.addVariable(tupleType);

    Value* test = arguments[0];
    PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, tupleType, Origin());
    patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
    patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::stackArgument(0) };
    patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
        AllowMacroScratchRegisterUsage allowScratch(jit);
        jit.move(CCallHelpers::TrustedImm32(first), params[0].gpr());
#if !CPU(ARM_THUMB2) // FIXME
        jit.store64(CCallHelpers::TrustedImm64(std::bit_cast<uint64_t>(second)), CCallHelpers::Address(CCallHelpers::framePointerRegister, params[1].offsetFromFP()));
#endif
    });
    root->appendNew<VariableValue>(proc, Set, Origin(), var, patchpoint);
    root->appendNewControlValue(proc, Branch, Origin(), test, FrequentedBlock(truthy), FrequentedBlock(falsey));

    auto addDup = [&] (BasicBlock* block) {
        Value* tuple = block->appendNew<VariableValue>(proc, B3::Get, Origin(), var);
        Value* i32 = block->appendNew<Value>(proc, ZExt32, Origin(),
            block->appendNew<ExtractValue>(proc, Origin(), Int32, tuple, 0));
        i32 = block->appendNew<Value>(proc, IToD, Origin(), i32);
        Value* f64 = block->appendNew<ExtractValue>(proc, Origin(), Double, tuple, 1);
        block->appendNew<Value>(proc, Return, Origin(), block->appendNew<Value>(proc, Add, Origin(), i32, f64));
    };

    addDup(truthy);
    addDup(falsey);

    proc.resetReachability();
    if (shouldFixSSA)
        fixSSA(proc);
    CHECK_EQ(compileAndRun<double>(proc, first), first + second);
}

template<bool shouldFixSSA>
static void tuplePairVariableLoop(unsigned first, uint64_t second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    BasicBlock* body = proc.addBlock();
    BasicBlock* exit = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, intptr_t>(proc, root);

    Type tupleType = proc.addTuple({ Int32, Int64 });
    Variable* var = proc.addVariable(tupleType);

    {
        Value* first = arguments[0];
        Value* second = arguments[1];
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, tupleType, Origin());
        patchpoint->append({ first, ValueRep::SomeRegister });
        patchpoint->append({ second, ValueRep::SomeRegister });
        patchpoint->resultConstraints = { ValueRep::SomeEarlyRegister, ValueRep::SomeEarlyRegister };
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            jit.move(params[2].gpr(), params[0].gpr());
            jit.move(params[3].gpr(), params[1].gpr());
        });
        root->appendNew<VariableValue>(proc, Set, Origin(), var, patchpoint);
        root->appendNewControlValue(proc, Jump, Origin(), body);
    }

    {
        Value* tuple = body->appendNew<VariableValue>(proc, B3::Get, Origin(), var);
        Value* first = body->appendNew<ExtractValue>(proc, Origin(), Int32, tuple, 0);
        Value* second = body->appendNew<ExtractValue>(proc, Origin(), Int64, tuple, 1);
        PatchpointValue* patchpoint = body->appendNew<PatchpointValue>(proc, tupleType, Origin());
        patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
        patchpoint->append({ first, ValueRep::SomeRegister });
        patchpoint->append({ second, ValueRep::SomeRegister });
        patchpoint->resultConstraints = { ValueRep::SomeEarlyRegister, ValueRep::stackArgument(0) };
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            CHECK(params[3].gpr() != params[0].gpr());
            CHECK(params[2].gpr() != params[0].gpr());
#if !CPU(ARM_THUMB2) // FIXME
            jit.add64(CCallHelpers::TrustedImm32(1), params[3].gpr(), params[0].gpr());
            jit.store64(params[0].gpr(), CCallHelpers::Address(CCallHelpers::framePointerRegister, params[1].offsetFromFP()));
#endif

            jit.move(params[2].gpr(), params[0].gpr());
            jit.urshift32(CCallHelpers::TrustedImm32(1), params[0].gpr());
        });
        body->appendNew<VariableValue>(proc, Set, Origin(), var, patchpoint);
        Value* condition = body->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 0);
        body->appendNewControlValue(proc, Branch, Origin(), condition, FrequentedBlock(body), FrequentedBlock(exit));
    }

    {
        Value* tuple = exit->appendNew<VariableValue>(proc, B3::Get, Origin(), var);
        Value* second = exit->appendNew<ExtractValue>(proc, Origin(), Int64, tuple, 1);
        exit->appendNew<Value>(proc, Return, Origin(), second);
    }

    proc.resetReachability();
    validate(proc);
    if (shouldFixSSA)
        fixSSA(proc);
    CHECK_EQ(compileAndRun<uint64_t>(proc, first, second), second + (first ? getMSBSet(first) : first) + 1);
}

template<bool shouldFixSSA>
static void tupleNestedLoop(intptr_t first, double second)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    BasicBlock* outerLoop = proc.addBlock();
    BasicBlock* innerLoop = proc.addBlock();
    BasicBlock* outerContinuation = proc.addBlock();
    auto arguments = cCallArgumentValues<intptr_t, double>(proc, root);

    Type tupleType = proc.addTuple({ Int32, Double, Int32 });
    Variable* varOuter = proc.addVariable(tupleType);
    Variable* varInner = proc.addVariable(tupleType);
    Variable* tookInner = proc.addVariable(Int32);

    {
        Value* first = arguments[0];
        Value* second = arguments[1];
        PatchpointValue* patchpoint = root->appendNew<PatchpointValue>(proc, tupleType, Origin());
        patchpoint->append({ first, ValueRep::SomeRegisterWithClobber });
        patchpoint->append({ second, ValueRep::SomeRegisterWithClobber });
        patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::SomeRegister, ValueRep::SomeEarlyRegister };
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            jit.move(params[3].gpr(), params[0].gpr());
            jit.move(params[0].gpr(), params[2].gpr());
            jit.moveDouble(params[4].fpr(), params[1].fpr());
        });
        root->appendNew<VariableValue>(proc, Set, Origin(), varOuter, patchpoint);
        root->appendNew<VariableValue>(proc, Set, Origin(), tookInner, root->appendIntConstant(proc, Origin(), Int32, 0));
        root->appendNewControlValue(proc, Jump, Origin(), outerLoop);
    }

    {
        Value* tuple = outerLoop->appendNew<VariableValue>(proc, B3::Get, Origin(), varOuter);
        Value* first = outerLoop->appendNew<ExtractValue>(proc, Origin(), Int32, tuple, 0);
        Value* second = outerLoop->appendNew<ExtractValue>(proc, Origin(), Double, tuple, 1);
        Value* third = outerLoop->appendNew<VariableValue>(proc, B3::Get, Origin(), tookInner);
        PatchpointValue* patchpoint = outerLoop->appendNew<PatchpointValue>(proc, tupleType, Origin());
        patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
        patchpoint->append({ first, ValueRep::SomeRegisterWithClobber });
        patchpoint->append({ second, ValueRep::SomeRegisterWithClobber });
        patchpoint->append({ third, ValueRep::SomeRegisterWithClobber });
        patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::SomeRegister, ValueRep::SomeRegister };
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            jit.move(params[3].gpr(), params[0].gpr());
            jit.moveConditionally32(CCallHelpers::Equal, params[5].gpr(), CCallHelpers::TrustedImm32(0), params[0].gpr(), params[5].gpr(), params[2].gpr());
            jit.moveDouble(params[4].fpr(), params[1].fpr());
        });
        outerLoop->appendNew<VariableValue>(proc, Set, Origin(), varOuter, patchpoint);
        outerLoop->appendNew<VariableValue>(proc, Set, Origin(), varInner, patchpoint);
        Value* condition = outerLoop->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 2);
        outerLoop->appendNewControlValue(proc, Branch, Origin(), condition, FrequentedBlock(outerContinuation), FrequentedBlock(innerLoop));
    }

    {
        Value* tuple = innerLoop->appendNew<VariableValue>(proc, B3::Get, Origin(), varInner);
        Value* first = innerLoop->appendNew<ExtractValue>(proc, Origin(), Int32, tuple, 0);
        Value* second = innerLoop->appendNew<ExtractValue>(proc, Origin(), Double, tuple, 1);
        PatchpointValue* patchpoint = innerLoop->appendNew<PatchpointValue>(proc, tupleType, Origin());
        patchpoint->clobber(RegisterSetBuilder::macroClobberedGPRs());
        patchpoint->append({ first, ValueRep::SomeRegisterWithClobber });
        patchpoint->append({ second, ValueRep::SomeRegisterWithClobber });
        patchpoint->resultConstraints = { ValueRep::SomeRegister, ValueRep::SomeRegister, ValueRep::SomeEarlyRegister };
        patchpoint->setGenerator([&] (CCallHelpers& jit, const StackmapGenerationParams& params) {
            AllowMacroScratchRegisterUsage allowScratch(jit);
            jit.move(params[3].gpr(), params[0].gpr());
            jit.move(CCallHelpers::TrustedImm32(0), params[2].gpr());
            jit.moveDouble(params[4].fpr(), params[1].fpr());
        });
        innerLoop->appendNew<VariableValue>(proc, Set, Origin(), varOuter, patchpoint);
        innerLoop->appendNew<VariableValue>(proc, Set, Origin(), varInner, patchpoint);
        Value* condition = innerLoop->appendNew<ExtractValue>(proc, Origin(), Int32, patchpoint, 2);
        innerLoop->appendNew<VariableValue>(proc, Set, Origin(), tookInner, innerLoop->appendIntConstant(proc, Origin(), Int32, 1));
        innerLoop->appendNewControlValue(proc, Branch, Origin(), condition, FrequentedBlock(innerLoop), FrequentedBlock(outerLoop));
    }

    {
        Value* tuple = outerContinuation->appendNew<VariableValue>(proc, B3::Get, Origin(), varInner);
        Value* first = outerContinuation->appendNew<ExtractValue>(proc, Origin(), Int32, tuple, 0);
        Value* second = outerContinuation->appendNew<ExtractValue>(proc, Origin(), Double, tuple, 1);
        Value* result = outerContinuation->appendNew<Value>(proc, Add, Origin(), second, outerContinuation->appendNew<Value>(proc, IToD, Origin(), first));
        outerContinuation->appendNewControlValue(proc, Return, Origin(), result);
    }

    proc.resetReachability();
    validate(proc);
    if (shouldFixSSA)
        fixSSA(proc);
    CHECK_EQ(compileAndRun<double>(proc, first, second), first + second);
}

void addTupleTests(const TestConfig* config, Deque<RefPtr<SharedTask<void()>>>& tasks)
{
    RUN_BINARY(testSimpleTuplePair, int32Operands(), int64Operands());
    RUN_BINARY(testSimpleTuplePairUnused, int32Operands(), int64Operands());
    RUN_BINARY(testSimpleTuplePairStack, int32Operands(), int64Operands());
    RUN((testBottomTupleValue<Int32, int32_t>)());
    RUN((testBottomTupleValue<Int64, int64_t>)());
    RUN((testBottomTupleValue<Float, float>)());
    RUN((testBottomTupleValue<Double, double>)());
    RUN((testTouchAllBottomTupleValue<Int32, int32_t>)());
    RUN((testTouchAllBottomTupleValue<Int64, int64_t>)());
    RUN((testTouchAllBottomTupleValue<Float, float>)());
    RUN((testTouchAllBottomTupleValue<Double, double>)());
    // use int64 as second argument because checking for NaN is annoying and doesn't really matter for this test.
    RUN_BINARY(tailDupedTuplePair<true>, int32Operands(), int64Operands());
    RUN_BINARY(tailDupedTuplePair<false>, int32Operands(), int64Operands());
    RUN_BINARY(tuplePairVariableLoop<true>, int32Operands(), int64Operands());
    RUN_BINARY(tuplePairVariableLoop<false>, int32Operands(), int64Operands());
    RUN_BINARY(tupleNestedLoop<true>, int32Operands(), int64Operands());
    RUN_BINARY(tupleNestedLoop<false>, int32Operands(), int64Operands());
}

template <typename FloatType>
static void testFMaxMin()
{
    auto checkResult = [&] (FloatType result, FloatType expected) {
        CHECK_EQ(std::isnan(result), std::isnan(expected));
        if (!std::isnan(expected)) {
            CHECK_EQ(result, expected);
            CHECK_EQ(std::signbit(result), std::signbit(expected));
        }
    };

    auto runArgTest = [&] (bool max, FloatType arg1, FloatType arg2) {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<FloatType, FloatType>(proc, root);
        Value* a = arguments[0];
        Value* b = arguments[1];
        Value* result = root->appendNew<Value>(proc, max ? FMax : FMin, Origin(), a, b);
        root->appendNewControlValue(proc, Return, Origin(), result);
        auto code = compileProc(proc);
        return invoke<FloatType>(*code, arg1, arg2);
    };

    auto runConstTest = [&] (bool max, FloatType arg1, FloatType arg2) {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        Value* a;
        Value* b;
        if (std::is_same_v<FloatType, float>) {
            a = root->appendNew<ConstFloatValue>(proc, Origin(), arg1);
            b = root->appendNew<ConstFloatValue>(proc, Origin(), arg2);
        } else {
            a = root->appendNew<ConstDoubleValue>(proc, Origin(), arg1);
            b = root->appendNew<ConstDoubleValue>(proc, Origin(), arg2);
        }
        Value* result = root->appendNew<Value>(proc, max ? FMax : FMin, Origin(), a, b);
        root->appendNewControlValue(proc, Return, Origin(), result);
        auto code = compileProc(proc);
        return invoke<FloatType>(*code, arg1, arg2);
    };

    auto runMinTest = [&] (FloatType a, FloatType b, FloatType expected) {
        checkResult(runArgTest(false, a, b), expected);
        checkResult(runArgTest(false, b, a), expected);
        checkResult(runConstTest(false, a, b), expected);
        checkResult(runConstTest(false, b, a), expected);
    };

    auto runMaxTest = [&] (FloatType a, FloatType b, FloatType expected) {
        checkResult(runArgTest(true, a, b), expected);
        checkResult(runConstTest(true, a, b), expected);
        checkResult(runArgTest(true, b, a), expected);
        checkResult(runConstTest(true, b, a), expected);
    };

    auto inf = std::numeric_limits<FloatType>::infinity();

    runMinTest(10.0, 0.0, 0.0);
    runMinTest(-10.0, 4.0, -10.0);
    runMinTest(4.1, 4.2, 4.1);
    runMinTest(-4.1, -4.2, -4.2);
    runMinTest(0.0, -0.0, -0.0);
    runMinTest(-0.0, -0.0, -0.0);
    runMinTest(0.0, 0.0, 0.0);
    runMinTest(-inf, 0, -inf);
    runMinTest(-inf, inf, -inf);
    runMinTest(inf, 42.0, 42.0);
    if constexpr (std::is_same_v<FloatType, float>) {
        runMinTest(0.0, std::nanf(""), std::nanf(""));
        runMinTest(std::nanf(""), 42.0, std::nanf(""));
    } else if constexpr (std::is_same_v<FloatType, double>) {
        runMinTest(0.0, std::nan(""), std::nan(""));
        runMinTest(std::nan(""), 42.0, std::nan(""));
    }


    runMaxTest(0.0, 10.0, 10.0);
    runMaxTest(-10.0, 4.0, 4.0);
    runMaxTest(4.1, 4.2, 4.2);
    runMaxTest(-4.1, -4.2, -4.1);
    runMaxTest(0.0, -0.0, 0.0);
    runMaxTest(-0.0, -0.0, -0.0);
    runMaxTest(0.0, 0.0, 0.0);
    runMaxTest(-inf, 0, 0);
    runMaxTest(-inf, inf, inf);
    runMaxTest(inf, 42.0, inf);
    if constexpr (std::is_same_v<FloatType, float>) {
        runMaxTest(0.0, std::nanf(""), std::nanf(""));
        runMaxTest(std::nanf(""), 42.0, std::nanf(""));
    } else if constexpr (std::is_same_v<FloatType, double>) {
        runMaxTest(0.0, std::nan(""), std::nan(""));
        runMaxTest(std::nan(""), 42.0, std::nan(""));
    }
}

void testFloatMaxMin()
{
    testFMaxMin<float>();
}

void testDoubleMaxMin()
{
    testFMaxMin<double>();
}

void testVectorOrConstants(v128_t lhs, v128_t rhs)
{
    alignas(16) v128_t vector;
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, lhsConstant, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());

        compileAndRun<void>(proc, &vector);
        CHECK(bitEquals(vector, vectorOr(lhs, rhs)));
    }
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* first = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, lhsConstant);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, first, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());
        auto code = compileProc(proc);

        for (auto& operand : v128Operands()) {
            vector = operand.value;
            invoke<void>(*code, &vector);
            CHECK(bitEquals(vector, vectorOr(vectorOr(lhs, operand.value), rhs)));
        }
    }
}

void testVectorOrSelf()
{
    alignas(16) v128_t vector;
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, input);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand : v128Operands()) {
        vector = operand.value;
        invoke<void>(*code, &vector);
        CHECK(bitEquals(vector, vectorOr(operand.value, operand.value)));
    }
}

void testVectorXorOrAllOnesToVectorAndXor()
{
    alignas(16) v128_t vectors[2];
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* constant = root->appendNew<Const128Value>(proc, Origin(), vectorAllOnes());
    Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address, sizeof(v128_t));
    Value* result0 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input0, constant);
    Value* result1 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input1, constant);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, result0, result1);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand0 : v128Operands()) {
        for (auto& operand1 : v128Operands()) {
            vectors[0] = operand0.value;
            vectors[1] = operand1.value;
            invoke<void>(*code, vectors);
            CHECK(bitEquals(vectors[0], vectorOr(vectorXor(operand0.value, vectorAllOnes()), vectorXor(operand1.value, vectorAllOnes()))));
        }
    }
}

void testVectorXorAndAllOnesToVectorOrXor()
{
    alignas(16) v128_t vectors[2];
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* constant = root->appendNew<Const128Value>(proc, Origin(), vectorAllOnes());
    Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address, sizeof(v128_t));
    Value* result0 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input0, constant);
    Value* result1 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input1, constant);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, result0, result1);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand0 : v128Operands()) {
        for (auto& operand1 : v128Operands()) {
            vectors[0] = operand0.value;
            vectors[1] = operand1.value;
            invoke<void>(*code, vectors);
            CHECK(bitEquals(vectors[0], vectorAnd(vectorXor(operand0.value, vectorAllOnes()), vectorXor(operand1.value, vectorAllOnes()))));
        }
    }
}

void testVectorXorOrAllOnesConstantToVectorAndXor(v128_t constant)
{
    alignas(16) v128_t vector;
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* allOnes = root->appendNew<Const128Value>(proc, Origin(), vectorAllOnes());
    Value* constant0 = root->appendNew<Const128Value>(proc, Origin(), constant);
    Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* result0 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, allOnes);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, result0, constant0);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand : v128Operands()) {
        vector = operand.value;
        invoke<void>(*code, &vector);
        CHECK(bitEquals(vector, vectorOr(vectorXor(operand.value, vectorAllOnes()), constant)));
    }
}

void testVectorXorAndAllOnesConstantToVectorOrXor(v128_t constant)
{
    alignas(16) v128_t vector;
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* allOnes = root->appendNew<Const128Value>(proc, Origin(), vectorAllOnes());
    Value* constant0 = root->appendNew<Const128Value>(proc, Origin(), constant);
    Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* result0 = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, allOnes);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, result0, constant0);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand : v128Operands()) {
        vector = operand.value;
        invoke<void>(*code, &vector);
        CHECK(bitEquals(vector, vectorAnd(vectorXor(operand.value, vectorAllOnes()), constant)));
    }
}

void testVectorAndConstants(v128_t lhs, v128_t rhs)
{
    alignas(16) v128_t vector;
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, lhsConstant, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());

        compileAndRun<void>(proc, &vector);
        CHECK(bitEquals(vector, vectorAnd(lhs, rhs)));
    }
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* first = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, lhsConstant);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, first, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());
        auto code = compileProc(proc);

        for (auto& operand : v128Operands()) {
            vector = operand.value;
            invoke<void>(*code, &vector);
            CHECK(bitEquals(vector, vectorAnd(vectorAnd(lhs, operand.value), rhs)));
        }
    }
}

void testVectorAndSelf()
{
    alignas(16) v128_t vector;
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, input);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand : v128Operands()) {
        vector = operand.value;
        invoke<void>(*code, &vector);
        CHECK(bitEquals(vector, vectorAnd(operand.value, operand.value)));
    }
}

void testVectorXorConstants(v128_t lhs, v128_t rhs)
{
    alignas(16) v128_t vector;
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, lhsConstant, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());

        compileAndRun<void>(proc, &vector);
        CHECK(bitEquals(vector, vectorXor(lhs, rhs)));
    }
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* lhsConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* rhsConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* first = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, lhsConstant);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, first, rhsConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());
        auto code = compileProc(proc);

        for (auto& operand : v128Operands()) {
            vector = operand.value;
            invoke<void>(*code, &vector);
            CHECK(bitEquals(vector, vectorXor(vectorXor(lhs, operand.value), rhs)));
        }
    }
}

void testVectorXorSelf()
{
    alignas(16) v128_t vector;
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address = arguments[0];
    Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
    Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, input);
    root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
    root->appendNewControlValue(proc, Return, Origin());

    auto code = compileProc(proc);

    for (auto& operand : v128Operands()) {
        vector = operand.value;
        invoke<void>(*code, &vector);
        CHECK(bitEquals(vector, vectorXor(operand.value, operand.value)));
    }
}

void testVectorAndConstantConstant(v128_t lhs, v128_t rhs)
{
    alignas(16) v128_t vector;
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* firstConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* secondConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* first = root->appendNew<SIMDValue>(proc, Origin(), VectorXor, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, firstConstant);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, first, secondConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());
        auto code = compileProc(proc);

        for (auto& operand : v128Operands()) {
            vector = operand.value;
            invoke<void>(*code, &vector);
            CHECK(bitEquals(vector, vectorAnd(vectorXor(operand.value, lhs), rhs)));
        }
    }
    {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* firstConstant = root->appendNew<Const128Value>(proc, Origin(), lhs);
        Value* secondConstant = root->appendNew<Const128Value>(proc, Origin(), rhs);
        Value* input = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* first = root->appendNew<SIMDValue>(proc, Origin(), VectorOr, B3::V128, SIMDLane::v128, SIMDSignMode::None, input, firstConstant);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorAnd, B3::V128, SIMDLane::v128, SIMDSignMode::None, first, secondConstant);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());
        auto code = compileProc(proc);

        for (auto& operand : v128Operands()) {
            vector = operand.value;
            invoke<void>(*code, &vector);
            CHECK(bitEquals(vector, vectorAnd(vectorOr(operand.value, lhs), rhs)));
        }
    }
}

void testVectorFmulByElementFloat()
{
    for (unsigned i = 0; i < 4; ++i) {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*, void*, void*>(proc, root);

        Value* address0 = arguments[0];
        Value* address1 = arguments[1];
        Value* address2 = arguments[2];

        Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address0);
        Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address1);
        Value* dup = root->appendNew<SIMDValue>(proc, Origin(), VectorDupElement, B3::V128, SIMDLane::f32x4, SIMDSignMode::None, static_cast<uint8_t>(i), input0);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorMul, B3::V128, SIMDLane::f32x4, SIMDSignMode::None, input1, dup);

        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address2);
        root->appendNewControlValue(proc, Return, Origin());

        auto code = compileProc(proc);

        auto checkFloat = [&](float a, float b) {
            if (std::isnan(a))
                CHECK(std::isnan(b));
            else
                CHECK_EQ(a, b);
        };

        for (auto& operand0 : floatingPointOperands<float>()) {
            for (auto& operand1 : floatingPointOperands<float>()) {
                for (auto& operand2 : floatingPointOperands<float>()) {
                    for (auto& operand3 : floatingPointOperands<float>()) {
                        alignas(16) v128_t vector0;
                        alignas(16) v128_t vector1;
                        alignas(16) v128_t vector2;
                        alignas(16) v128_t result;

                        vector0.f32x4[0] = operand0.value;
                        vector0.f32x4[1] = operand1.value;
                        vector0.f32x4[2] = operand2.value;
                        vector0.f32x4[3] = operand3.value;

                        vector1.f32x4[0] = operand3.value;
                        vector1.f32x4[1] = operand2.value;
                        vector1.f32x4[2] = operand1.value;
                        vector1.f32x4[3] = operand0.value;

                        result.f32x4[0] = vector0.f32x4[i] * vector1.f32x4[0];
                        result.f32x4[1] = vector0.f32x4[i] * vector1.f32x4[1];
                        result.f32x4[2] = vector0.f32x4[i] * vector1.f32x4[2];
                        result.f32x4[3] = vector0.f32x4[i] * vector1.f32x4[3];

                        invoke<void>(*code, &vector0, &vector1, &vector2);
                        checkFloat(result.f32x4[0], vector2.f32x4[0]);
                        checkFloat(result.f32x4[1], vector2.f32x4[1]);
                        checkFloat(result.f32x4[2], vector2.f32x4[2]);
                        checkFloat(result.f32x4[3], vector2.f32x4[3]);
                    }
                }
            }
        }
    }
}

void testVectorFmulByElementDouble()
{
    for (unsigned i = 0; i < 2; ++i) {
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*, void*, void*>(proc, root);

        Value* address0 = arguments[0];
        Value* address1 = arguments[1];
        Value* address2 = arguments[2];

        Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address0);
        Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address1);
        Value* dup = root->appendNew<SIMDValue>(proc, Origin(), VectorDupElement, B3::V128, SIMDLane::f64x2, SIMDSignMode::None, static_cast<uint8_t>(i), input0);
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorMul, B3::V128, SIMDLane::f64x2, SIMDSignMode::None, input1, dup);

        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address2);
        root->appendNewControlValue(proc, Return, Origin());

        auto code = compileProc(proc);

        auto checkDouble = [&](double a, double b) {
            if (std::isnan(a))
                CHECK(std::isnan(b));
            else
                CHECK_EQ(a, b);
        };

        for (auto& operand0 : floatingPointOperands<double>()) {
            for (auto& operand1 : floatingPointOperands<double>()) {
                for (auto& operand2 : floatingPointOperands<double>()) {
                    for (auto& operand3 : floatingPointOperands<double>()) {
                        alignas(16) v128_t vector0;
                        alignas(16) v128_t vector1;
                        alignas(16) v128_t vector2;
                        alignas(16) v128_t result;

                        vector0.f64x2[0] = operand0.value;
                        vector0.f64x2[1] = operand1.value;

                        vector1.f64x2[0] = operand2.value;
                        vector1.f64x2[1] = operand3.value;

                        result.f64x2[0] = vector0.f64x2[i] * vector1.f64x2[0];
                        result.f64x2[1] = vector0.f64x2[i] * vector1.f64x2[1];

                        invoke<void>(*code, &vector0, &vector1, &vector2);
                        checkDouble(result.f64x2[0], vector2.f64x2[0]);
                        checkDouble(result.f64x2[1], vector2.f64x2[1]);
                    }
                }
            }
        }
    }
}

void testVectorExtractLane0Float()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address0 = arguments[0];
    Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address0);
    root->appendNewControlValue(proc, Return, Origin(), root->appendNew<SIMDValue>(proc, Origin(), VectorExtractLane, B3::Float, SIMDLane::f32x4, SIMDSignMode::None, static_cast<uint8_t>(0), input0));

    auto code = compileProc(proc);

    auto checkFloat = [&](float a, float b) {
        if (std::isnan(a))
            CHECK(std::isnan(b));
        else
            CHECK_EQ(a, b);
    };

    for (auto& operand0 : floatingPointOperands<float>()) {
        alignas(16) v128_t vector0;

        vector0.f32x4[0] = operand0.value;
        vector0.f32x4[1] = 1;
        vector0.f32x4[2] = 2;
        vector0.f32x4[3] = 3;

        float result = invoke<float>(*code, &vector0);
        checkFloat(result, operand0.value);
    }
}

void testVectorExtractLane0Double()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<void*>(proc, root);

    Value* address0 = arguments[0];
    Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address0);
    root->appendNewControlValue(proc, Return, Origin(), root->appendNew<SIMDValue>(proc, Origin(), VectorExtractLane, B3::Double, SIMDLane::f64x2, SIMDSignMode::None, static_cast<uint8_t>(0), input0));

    auto code = compileProc(proc);

    auto checkDouble = [&](double a, double b) {
        if (std::isnan(a))
            CHECK(std::isnan(b));
        else
            CHECK_EQ(a, b);
    };

    for (auto& operand0 : floatingPointOperands<double>()) {
        alignas(16) v128_t vector0;

        vector0.f64x2[0] = operand0.value;
        vector0.f64x2[1] = 32;

        double result = invoke<double>(*code, &vector0);
        checkDouble(result, operand0.value);
    }
}

void testVectorMulHigh()
{
    auto vectorMulHigh = [&](SIMDLane lane, SIMDSignMode signMode, v128_t lhs, v128_t rhs) {
        auto simdeLHS = std::bit_cast<simde_v128_t>(lhs);
        auto simdeRHS = std::bit_cast<simde_v128_t>(rhs);
        switch (lane) {
        case SIMDLane::i16x8:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u16x8_extmul_high_u8x16(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i16x8_extmul_high_i8x16(simdeLHS, simdeRHS));
        case SIMDLane::i32x4:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u32x4_extmul_high_u16x8(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i32x4_extmul_high_i16x8(simdeLHS, simdeRHS));
        case SIMDLane::i64x2:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u64x2_extmul_high_u32x4(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i64x2_extmul_high_i32x4(simdeLHS, simdeRHS));
        default:
            return v128_t { };
        }
    };

    auto test = [&](SIMDLane lane, SIMDSignMode signMode) {
        alignas(16) v128_t vectors[2];
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address, sizeof(v128_t));
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorMulHigh, B3::V128, lane, signMode, input0, input1);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());

        auto code = compileProc(proc);
        for (auto& operand0 : v128Operands()) {
            for (auto& operand1 : v128Operands()) {
                vectors[0] = operand0.value;
                vectors[1] = operand1.value;
                invoke<void>(*code, vectors);
                CHECK(bitEquals(vectors[0], vectorMulHigh(lane, signMode, operand0.value, operand1.value)));
            }
        }
    };

    test(SIMDLane::i16x8, SIMDSignMode::Signed);
    test(SIMDLane::i16x8, SIMDSignMode::Unsigned);
    test(SIMDLane::i32x4, SIMDSignMode::Signed);
    test(SIMDLane::i32x4, SIMDSignMode::Unsigned);
    test(SIMDLane::i64x2, SIMDSignMode::Signed);
    test(SIMDLane::i64x2, SIMDSignMode::Unsigned);
}

void testVectorMulLow()
{
    auto vectorMulLow = [&](SIMDLane lane, SIMDSignMode signMode, v128_t lhs, v128_t rhs) {
        auto simdeLHS = std::bit_cast<simde_v128_t>(lhs);
        auto simdeRHS = std::bit_cast<simde_v128_t>(rhs);
        switch (lane) {
        case SIMDLane::i16x8:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u16x8_extmul_low_u8x16(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i16x8_extmul_low_i8x16(simdeLHS, simdeRHS));
        case SIMDLane::i32x4:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u32x4_extmul_low_u16x8(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i32x4_extmul_low_i16x8(simdeLHS, simdeRHS));
        case SIMDLane::i64x2:
            if (signMode == SIMDSignMode::Unsigned)
                return std::bit_cast<v128_t>(simde_wasm_u64x2_extmul_low_u32x4(simdeLHS, simdeRHS));
            return std::bit_cast<v128_t>(simde_wasm_i64x2_extmul_low_i32x4(simdeLHS, simdeRHS));
        default:
            return v128_t { };
        }
    };

    auto test = [&](SIMDLane lane, SIMDSignMode signMode) {
        alignas(16) v128_t vectors[2];
        Procedure proc;
        BasicBlock* root = proc.addBlock();
        auto arguments = cCallArgumentValues<void*>(proc, root);

        Value* address = arguments[0];
        Value* input0 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address);
        Value* input1 = root->appendNew<MemoryValue>(proc, Load, V128, Origin(), address, sizeof(v128_t));
        Value* result = root->appendNew<SIMDValue>(proc, Origin(), VectorMulLow, B3::V128, lane, signMode, input0, input1);
        root->appendNew<MemoryValue>(proc, Store, Origin(), result, address);
        root->appendNewControlValue(proc, Return, Origin());

        auto code = compileProc(proc);
        for (auto& operand0 : v128Operands()) {
            for (auto& operand1 : v128Operands()) {
                vectors[0] = operand0.value;
                vectors[1] = operand1.value;
                invoke<void>(*code, vectors);
                CHECK(bitEquals(vectors[0], vectorMulLow(lane, signMode, operand0.value, operand1.value)));
            }
        }
    };

    test(SIMDLane::i16x8, SIMDSignMode::Signed);
    test(SIMDLane::i16x8, SIMDSignMode::Unsigned);
    test(SIMDLane::i32x4, SIMDSignMode::Signed);
    test(SIMDLane::i32x4, SIMDSignMode::Unsigned);
    test(SIMDLane::i64x2, SIMDSignMode::Signed);
    test(SIMDLane::i64x2, SIMDSignMode::Unsigned);
}

void testInt52RoundTripUnary(int32_t constant)
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int32_t>(proc, root);
    Value* argA = arguments[0];
    Value* int52A = root->appendNew<Value>(proc, Shl, Origin(), root->appendNew<Value>(proc, SExt32, Origin(), argA), root->appendNew<Const32Value>(proc, Origin(), 12));
    Value* int52B = root->appendNew<Value>(proc, Shl, Origin(), root->appendNew<Value>(proc, SExt32, Origin(), root->appendNew<Const32Value>(proc, Origin(), constant)), root->appendNew<Const32Value>(proc, Origin(), 12));
    Value* node = root->appendNew<Value>(proc, Add, Origin(), int52A, int52B);
    Value* result = root->appendNew<Value>(proc, Trunc, Origin(), root->appendNew<Value>(proc, SShr, Origin(), node, root->appendNew<Const32Value>(proc, Origin(), 12)));
    root->appendNew<Value>(proc, Return, Origin(), result);
    auto code = compileProc(proc);

    for (auto value : int32Operands())
        CHECK_EQ(invoke<int32_t>(*code, value.value), static_cast<int32_t>(((static_cast<int64_t>(value.value) << 12) + (static_cast<int64_t>(constant) << 12)) >> 12));
}

void testInt52RoundTripBinary()
{
    Procedure proc;
    BasicBlock* root = proc.addBlock();
    auto arguments = cCallArgumentValues<int32_t, int32_t>(proc, root);
    Value* argA = arguments[0];
    Value* argB = arguments[1];
    Value* int52A = root->appendNew<Value>(proc, Shl, Origin(), root->appendNew<Value>(proc, SExt32, Origin(), argA), root->appendNew<Const32Value>(proc, Origin(), 12));
    Value* int52B = root->appendNew<Value>(proc, Shl, Origin(), root->appendNew<Value>(proc, SExt32, Origin(), argB), root->appendNew<Const32Value>(proc, Origin(), 12));
    Value* node = root->appendNew<Value>(proc, Add, Origin(), int52A, int52B);
    Value* result = root->appendNew<Value>(proc, Trunc, Origin(), root->appendNew<Value>(proc, SShr, Origin(), node, root->appendNew<Const32Value>(proc, Origin(), 12)));
    root->appendNew<Value>(proc, Return, Origin(), result);
    auto code = compileProc(proc);

    for (auto lhs : int32Operands()) {
        for (auto rhs : int32Operands())
            CHECK_EQ(invoke<int32_t>(*code, lhs.value, rhs.value), static_cast<int32_t>(((static_cast<int64_t>(lhs.value) << 12) + (static_cast<int64_t>(rhs.value) << 12)) >> 12));
    }
}

#endif // ENABLE(B3_JIT)

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
