#include "anydsl/analyses/domtree.h"

#include <limits>

#include "anydsl/lambda.h"

#include "anydsl/analyses/scope.h"

namespace anydsl {

//------------------------------------------------------------------------------

DomNode::DomNode(const Lambda* lambda) 
    : lambda_(lambda) 
    , idom_(0)
{
    lambda->scratch_set(this);
}

//------------------------------------------------------------------------------

class DomBuilder {
public:

    DomBuilder(const Lambda* entry, const LambdaSet& scope)
        : entry(entry)
        , scope(scope)
        , index2node(scope.size())
    {
        anydsl_assert(contains(entry), "entry not contained in scope");
    }


    size_t num() const { return index2node.size(); }
    bool contains(const Lambda* lambda) { return scope.find(lambda) != scope.end(); }

    DomNode* build();
    DomNode* intersect(DomNode* i, DomNode* j);
    size_t number(const Lambda* cur, size_t i);

    const Lambda* entry;
    const LambdaSet& scope;
    Array<DomNode*> index2node;
};


DomNode* DomBuilder::build() {
    // mark all nodes as unnumbered
    for_all (lambda, scope)
        lambda->scratch.ptr = 0;

    // mark all nodes in post-order
    size_t num2 = number(entry, 0);
    DomNode* entry_node = entry->scratch_get<DomNode>();
    anydsl_assert(num2 == num(), "bug in numbering -- maybe scope contains unreachable blocks?");
    anydsl_assert(num() - 1 == entry_node->index(), "bug in numbering");

    // map entry to entry, all other are set to 0 by the DomNode constructor
    entry_node->idom_ = entry_node;

    if (num() > 1) {
        for (bool changed = true; changed;) {
            changed = false;

            // for all lambdas in reverse post-order except start node
            for (size_t i = num() - 1; i --> 0; /* the C++ goes-to operator :) */) {
                DomNode* cur = index2node[i];

                // for all predecessors of cur
                DomNode* new_idom = 0;
                for_all (caller, cur->lambda()->callers()) {
                    if (contains(caller)) {
                        if (DomNode* other_idom = caller->scratch_get<DomNode>()->idom_) {
                            if (!new_idom)
                                new_idom = caller->scratch_get<DomNode>();// pick first processed predecessor of cur
                            else
                                new_idom = intersect(other_idom, new_idom);
                        }
                    }
                }

                assert(new_idom);

                if (cur->idom() != new_idom) {
                    cur->idom_ = new_idom;
                    changed = true;
                }
            }
        }
    }

    // add children
    for_all (node, index2node) {
        if (!node->entry())
            node->idom_->children_.insert(node);
    }

    return entry_node;
}

size_t DomBuilder::number(const Lambda* cur, size_t i) {
    DomNode* node = new DomNode(cur);

    // for each successor in scope
    for_all (succ, cur->succ()) {
        if (contains(succ) && !succ->scratch.ptr)
            i = number(succ, i);
    }

    node->index_ = i;
    index2node[i] = node;

    return i + 1;
}

DomNode* DomBuilder::intersect(DomNode* i, DomNode* j) {
    while (i->index() != j->index()) {
        while (i->index() < j->index()) 
            i = i->idom_;
        while (j->index() < i->index()) 
            j = j->idom_;
    }

    return i;
}

//------------------------------------------------------------------------------

const DomNode* calc_domtree(const Lambda* entry) {
    LambdaSet scope = find_scope(entry);
    return calc_domtree(entry, scope);
}

const DomNode* calc_domtree(const Lambda* entry, const LambdaSet& scope) {
    DomBuilder builder(entry, scope);
    return builder.build();
}

//------------------------------------------------------------------------------

} // namespace anydsl
