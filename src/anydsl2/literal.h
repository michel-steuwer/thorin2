#ifndef ANYDSL2_LITERAL_H
#define ANYDSL2_LITERAL_H

#include <vector>

#include "anydsl2/primop.h"
#include "anydsl2/type.h"
#include "anydsl2/util/box.h"

namespace anydsl2 {

class Type;
class World;

//------------------------------------------------------------------------------

class Literal : public PrimOp {
protected:

    Literal(int kind, const Type* type, const std::string& name)
        : PrimOp(kind, 0, type, name)
    {}
};

//------------------------------------------------------------------------------

/// Base class for \p Any and \p Bottom.
class Undef : public Literal {
protected:

    Undef(int kind, const Type* type, const std::string& name)
        : Literal(kind, type, name)
    {}
};

//------------------------------------------------------------------------------

/** 
 * @brief The wish-you-a-value value.
 *
 * This literal represents an arbitrary value.
 * When ever an operation takes an \p Undef value as argument, 
 * you may literally wish your favorite value instead.
 */
class Any : public Undef {
private:

    Any(const Type* type)
        : Undef(Node_Any, type, "<any>")
    {}
    virtual void vdump(Printer& printer) const ;

    friend class World;
};

//------------------------------------------------------------------------------

/** 
 * @brief The novalue-value.
 *
 * This literal represents literally 'no value'.
 * Extremely useful for data flow analysis.
 */
class Bottom : public Undef {
private:

    Bottom(const Type* type)
        : Undef(Node_Bottom, type, "<bottom>")
    {}
    virtual void vdump(Printer& printer) const ;

    friend class World;
};

//------------------------------------------------------------------------------

typedef boost::tuple<int, const Type*, Box> PrimLitTuple;
size_t hash_def(const PrimLitTuple& tuple);
bool equal_def(const PrimLitTuple& tuple, const Def* other);

class PrimLit : public Literal {
private:

    PrimLit(const Type* type, Box box)
        : Literal(Node_PrimLit, type, "")
        , box_(box)
    {}

public:

    Box box() const { return box_; }
    const PrimType* primtype() const { return type()->as<PrimType>(); }
    PrimTypeKind primtype_kind() const { return primtype()->primtype_kind(); }
    PrimLitTuple ptuple() const { return PrimLitTuple(kind(), type(), box()); }

private:

    virtual void vdump(Printer& printer) const ;
    //ANYDSL2_HASH_EQUAL

    virtual bool equal(const Node* other) const { return equal_def(ptuple(), other->as<Def>()); } \
    virtual size_t hash() const { return hash_def(ptuple()); }
    Box box_;

    friend class World;
};

//------------------------------------------------------------------------------

/**
 * The sole purpose of this node is to hold types.
 * This node is not destroyed by the dead code elimination, and hence,
 * the held type won't be destroyed in the unused type elimination.
 */
class TypeKeeper : public Literal {
private:

    TypeKeeper(const Type* type, const std::string& name)
        : Literal(Node_TypeKeeper, type, name)
    {}
    virtual void vdump(Printer& printer) const ;

    friend class World;
};

//------------------------------------------------------------------------------

template<class T>
T Def::primlit_value() const {
    const PrimLit* lit = this->as<PrimLit>();
    switch (lit->primtype_kind()) {
#define ANYDSL2_UF_TYPE(T) case PrimType_##T: return lit->box().get_##T();
#include "anydsl2/tables/primtypetable.h"
        default: ANYDSL2_UNREACHABLE;
    }
}

//------------------------------------------------------------------------------

} // namespace anydsl2

#endif
