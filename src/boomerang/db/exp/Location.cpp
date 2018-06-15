#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "Location.h"


#include "boomerang/core/Boomerang.h"
#include "boomerang/db/exp/RefExp.h"
#include "boomerang/visitor/expvisitor/ExpVisitor.h"
#include "boomerang/visitor/expmodifier/ExpModifier.h"
#include "boomerang/util/LocationSet.h"
#include "boomerang/util/Log.h"


Location::Location(const Location& other)
    : Unary(other.m_oper, other.subExp1->clone())
    , m_proc(other.m_proc)
{
}


Location::Location(OPER oper, SharedExp exp, UserProc *proc)
    : Unary(oper, exp)
    , m_proc(proc)
{
    assert(m_oper == opRegOf || m_oper == opMemOf || m_oper == opLocal || m_oper == opGlobal || m_oper == opParam || m_oper == opTemp);

    if (proc == nullptr) {
        // eep.. this almost always causes problems
        SharedExp e = exp;

        if (e) {
            bool giveUp = false;

            while (this->m_proc == nullptr && !giveUp) {
                switch (e->getOper())
                {
                case opRegOf:
                case opMemOf:
                case opTemp:
                case opLocal:
                case opGlobal:
                case opParam:
                    this->m_proc = std::static_pointer_cast<Location>(e)->getProc();
                    giveUp     = true;
                    break;

                case opSubscript:
                    e = e->getSubExp1();
                    break;

                default:
                    giveUp = true;
                    break;
                }
            }
        }
    }
}


SharedExp Location::clone() const
{
    return std::make_shared<Location>(m_oper, subExp1->clone(), m_proc);
}


SharedExp Location::polySimplify(bool& changed)
{
    SharedExp res = Unary::polySimplify(changed);

    if ((res->getOper() == opMemOf) && (res->getSubExp1()->getOper() == opAddrOf)) {
        LOG_VERBOSE("polySimplify %1", res);

        res  = res->getSubExp1()->getSubExp1();
        changed = true;
        return res;
    }

    // check for m[a[loc.x]] becomes loc.x
    if ((res->getOper() == opMemOf) && (res->getSubExp1()->getOper() == opAddrOf) &&
        (res->getSubExp1()->getSubExp1()->getOper() == opMemberAccess)) {
        res  = subExp1->getSubExp1();
        changed = true;
        return res;
    }

    return res;
}


void Location::getDefinitions(LocationSet& defs)
{
    // This is a hack to fix aliasing (replace with something general)
    // FIXME! This is x86 specific too. Use -O for overlapped registers!
    if ((m_oper == opRegOf) && (std::static_pointer_cast<const Const>(subExp1)->getInt() == PENT_REG_EAX)) {
        defs.insert(Location::regOf(PENT_REG_AX));
    }
}


bool Location::accept(ExpVisitor *v)
{
    bool visitChildren = true;
    if (!v->preVisit(shared_from_base<Location>(), visitChildren)) {
        return false;
    }

    if (visitChildren) {
        if (!subExp1->accept(v)) {
            return false;
        }
    }

    return v->postVisit(shared_from_base<Location>());
}


std::shared_ptr<Location> Location::local(const QString& name, UserProc *p)
{
    return std::make_shared<Location>(opLocal, Const::get(name), p);
}


SharedExp Location::accept(ExpModifier *mod)
{
    // This looks to be the same source code as Unary::accept, but the type of "this" is different, which is all
    // important here!  (it makes a call to a different visitor member function).
    bool      visitChildren = true;
    SharedExp ret = preAccept(mod, visitChildren);

    if (visitChildren) {
        this->childAccept(mod);
    }

    return ret->postAccept(mod);
}


SharedExp Location::preAccept(ExpModifier* mod, bool& visitChildren)
{
    return mod->preModify(access<Location>(), visitChildren);
}


SharedExp Location::postAccept(ExpModifier* mod)
{
    return mod->postModify(access<Location>());
}
