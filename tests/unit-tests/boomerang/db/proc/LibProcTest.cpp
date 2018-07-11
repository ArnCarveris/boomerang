#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "LibProcTest.h"


#include "boomerang/db/exp/Binary.h"
#include "boomerang/db/Module.h"
#include "boomerang/db/exp/Location.h"
#include "boomerang/db/proc/LibProc.h"
#include "boomerang/db/signature/PentiumSignature.h"
#include "boomerang/db/statements/CallStatement.h"


void LibProcTest::testName()
{
    LibProc proc(Address::INVALID, "test", nullptr);
    QCOMPARE(proc.getName(), QString("test"));
    proc.setName("foo");
    QCOMPARE(proc.getName(), QString("foo"));
}


void LibProcTest::testEntryAddr()
{
    LibProc proc(Address(0x1000), "test", nullptr);
    QCOMPARE(proc.getEntryAddress(), Address(0x1000));
    proc.setEntryAddress(Address::INVALID);
    QCOMPARE(proc.getEntryAddress(), Address::INVALID);
}


void LibProcTest::testRemoveFromModule()
{
    Module mod;
    LibProc proc(Address(0x1000), "test", &mod);
    proc.removeFromModule();
    QVERIFY(mod.getFunctionList().empty());
    QVERIFY(mod.getFunction(Address(0x1000)) == nullptr);
}


void LibProcTest::testRemoveParameter()
{
    LibProc proc(Address(0x1000), "test", nullptr);
    auto sig = std::make_shared<CallingConvention::StdC::PentiumSignature>("test");
    auto exp = Location::memOf(Binary::get(opPlus, Location::regOf(REG_PENT_ESP), Const::get(8)));
    sig->addParameter("foo", exp);

    proc.setSignature(sig);
    QCOMPARE(proc.getSignature()->getNumParams(), 1);

    proc.removeParameter(exp->clone()); // make sure to not compare by address
    QCOMPARE(proc.getSignature()->getNumParams(), 0);

    sig->addParameter("bar", exp);
    CallStatement call;
    call.setDestProc(&proc);
    proc.addCaller(&call);
    call.setNumArguments(1);

    proc.removeParameter(exp->clone());
    QCOMPARE(proc.getSignature()->getNumParams(), 0);
    QCOMPARE(call.getNumArguments(), 0);
}


void LibProcTest::testRenameParameter()
{
    LibProc proc(Address::INVALID, "test", nullptr);
    auto sig = std::make_shared<CallingConvention::StdC::PentiumSignature>("test");
    auto exp = Location::memOf(Binary::get(opPlus, Location::regOf(REG_PENT_ESP), Const::get(8)));
    sig->addParameter("foo", exp);
    sig->addParameter("foo", exp);
    proc.setSignature(sig);

    proc.renameParameter("foo", "bar");

    QCOMPARE(proc.getSignature()->getParamName(0), QString("bar"));
    QCOMPARE(proc.getSignature()->getParamName(1), QString("foo"));
}


void LibProcTest::testIsLib()
{
    LibProc proc(Address::INVALID, "print", nullptr);
    QVERIFY(proc.isLib());
}


void LibProcTest::testIsNoReturn()
{
    LibProc proc(Address::INVALID, "abort", nullptr);
    QVERIFY(proc.isNoReturn());
    proc.setName("test");
    QVERIFY(!proc.isNoReturn());
    std::shared_ptr<Signature> sig(new Signature("test"));
    proc.setSignature(sig);
    QVERIFY(!proc.isNoReturn());
}


void LibProcTest::testGetProven()
{
    LibProc proc(Address::INVALID, "test", nullptr);
    QVERIFY(proc.getProven(nullptr) == nullptr);
    QVERIFY(proc.getProven(Location::regOf(REG_PENT_EBX)) == nullptr);

    proc.setSignature(std::make_shared<CallingConvention::StdC::PentiumSignature>("test"));
    QVERIFY(proc.getProven(Location::regOf(REG_PENT_EBX)) != nullptr);
    QCOMPARE(proc.getProven(Location::regOf(REG_PENT_EBX))->toString(),
             Location::regOf(REG_PENT_EBX)->toString());
}


void LibProcTest::testGetPremised()
{
    LibProc proc(Address::INVALID, "test", nullptr);
    QVERIFY(proc.getPremised(nullptr) == nullptr);
    QVERIFY(proc.getPremised(Location::regOf(REG_PENT_EBX)) == nullptr);
}


void LibProcTest::testIsPreserved()
{
    LibProc proc(Address::INVALID, "test", nullptr);
    QVERIFY(!proc.isPreserved(nullptr));
    QVERIFY(!proc.isPreserved(Location::regOf(REG_PENT_EBX)));

    proc.setSignature(std::make_shared<CallingConvention::StdC::PentiumSignature>("test"));
    QVERIFY(!proc.isPreserved(Location::regOf(REG_PENT_EAX)));
    QVERIFY(proc.isPreserved(Location::regOf(REG_PENT_EBX)));
}

QTEST_GUILESS_MAIN(LibProcTest)
