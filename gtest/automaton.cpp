#include <memory>

#include <gtest/gtest.h>

#include "thorin/driver.h"
#include "thorin/world.h"

#include "thorin/fe/parser.h"

#include "dialects/regex/pass/dfa.h"
#include "dialects/regex/pass/dfamin.h"
#include "dialects/regex/pass/nfa.h"
#include "dialects/regex/pass/nfa2dfa.h"
#include "dialects/regex/pass/regex2nfa.h"
#include "dialects/regex/regex.h"

using namespace thorin::automaton;

TEST(Automaton, NFA) {
    auto nfa   = std::make_unique<NFA>();
    auto start = nfa->add_state();
    nfa->set_start(start);
    auto second = nfa->add_state();
    start->add_transition(second, 'a');
    start->add_transition(second, 'b');
    second->add_transition(second, 'b');
    auto third = nfa->add_state();
    third->set_accepting(true);
    second->add_transition(third, 'a');
    third->add_transition(third, 'a');
    third->add_transition(second, 'b');

    EXPECT_EQ(nfa->get_start(), start);
    EXPECT_FALSE(start->is_accepting());
    EXPECT_TRUE(third->is_accepting());
    EXPECT_EQ(start->get_transitions('a'), std::vector<const NFANode*>{second});
    EXPECT_EQ(start->get_transitions('b'), std::vector<const NFANode*>{second});
    EXPECT_EQ(second->get_transitions('a'), std::vector<const NFANode*>{third});
    EXPECT_EQ(second->get_transitions('b'), std::vector<const NFANode*>{second});
    EXPECT_EQ(third->get_transitions('a'), std::vector<const NFANode*>{third});
    EXPECT_EQ(third->get_transitions('b'), std::vector<const NFANode*>{second});
}

// https://cyberzhg.github.io/toolbox/regex2nfa?regex=KGF8YikrYQ==
TEST(Automaton, NFAAorBplusA) {
    auto nfa = std::make_unique<NFA>();
    std::vector<NFANode*> states;
    for (int i = 0; i < 14; ++i) states.push_back(nfa->add_state());
    nfa->set_start(states[0]);
    states[0]->add_transition(states[1], NFA::SpecialTransitons::EPSILON);
    states[1]->add_transition(states[2], 'a');
    states[2]->add_transition(states[5], NFA::SpecialTransitons::EPSILON);

    states[0]->add_transition(states[3], NFA::SpecialTransitons::EPSILON);
    states[3]->add_transition(states[4], 'a');
    states[4]->add_transition(states[5], NFA::SpecialTransitons::EPSILON);

    states[5]->add_transition(states[12], NFA::SpecialTransitons::EPSILON);

    states[5]->add_transition(states[6], NFA::SpecialTransitons::EPSILON);
    states[6]->add_transition(states[7], NFA::SpecialTransitons::EPSILON);
    states[7]->add_transition(states[8], 'a');
    states[8]->add_transition(states[11], NFA::SpecialTransitons::EPSILON);

    states[6]->add_transition(states[9], NFA::SpecialTransitons::EPSILON);
    states[9]->add_transition(states[10], 'b');
    states[10]->add_transition(states[11], NFA::SpecialTransitons::EPSILON);

    states[11]->add_transition(states[6], NFA::SpecialTransitons::EPSILON);
    states[11]->add_transition(states[12], NFA::SpecialTransitons::EPSILON);
    states[12]->add_transition(states[13], 'a');

    states[13]->set_accepting(true);

    // Test start state
    EXPECT_EQ(nfa->get_start(), states[0]);

    // Test non-accepting states
    for (int i = 0; i < 13; ++i) EXPECT_FALSE(states[i]->is_accepting());

    // Test accepting state
    EXPECT_TRUE(states[13]->is_accepting());

    std::vector<const NFANode*> empty;
    // Test transitions
    auto c = std::vector<const NFANode*>{
        {states[1], states[3]}
    };
    EXPECT_EQ(states[0]->get_transitions(NFA::SpecialTransitons::EPSILON), c);
    EXPECT_EQ(states[0]->get_transitions('a'), empty);
    EXPECT_EQ(states[0]->get_transitions('b'), empty);

    EXPECT_EQ(states[1]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[1]->get_transitions('a'), std::vector<const NFANode*>{states[2]});
    EXPECT_EQ(states[1]->get_transitions('b'), empty);

    EXPECT_EQ(states[2]->get_transitions(NFA::SpecialTransitons::EPSILON), std::vector<const NFANode*>{states[5]});
    EXPECT_EQ(states[2]->get_transitions('a'), empty);
    EXPECT_EQ(states[2]->get_transitions('b'), empty);

    EXPECT_EQ(states[3]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[3]->get_transitions('a'), std::vector<const NFANode*>{states[4]});
    EXPECT_EQ(states[3]->get_transitions('b'), empty);

    EXPECT_EQ(states[4]->get_transitions(NFA::SpecialTransitons::EPSILON), std::vector<const NFANode*>{states[5]});
    EXPECT_EQ(states[4]->get_transitions('a'), empty);
    EXPECT_EQ(states[4]->get_transitions('b'), empty);

    c = std::vector<const NFANode*>{
        {states[12], states[6]}
    };
    EXPECT_EQ(states[5]->get_transitions(NFA::SpecialTransitons::EPSILON), c);
    EXPECT_EQ(states[5]->get_transitions('a'), empty);
    EXPECT_EQ(states[5]->get_transitions('b'), empty);

    c = std::vector<const NFANode*>{
        {states[7], states[9]}
    };
    EXPECT_EQ(states[6]->get_transitions(NFA::SpecialTransitons::EPSILON), c);
    EXPECT_EQ(states[6]->get_transitions('a'), empty);
    EXPECT_EQ(states[6]->get_transitions('b'), empty);

    EXPECT_EQ(states[7]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[7]->get_transitions('a'), std::vector<const NFANode*>{states[8]});
    EXPECT_EQ(states[7]->get_transitions('b'), empty);

    EXPECT_EQ(states[8]->get_transitions(NFA::SpecialTransitons::EPSILON), std::vector<const NFANode*>{states[11]});
    EXPECT_EQ(states[8]->get_transitions('a'), empty);
    EXPECT_EQ(states[8]->get_transitions('b'), empty);

    EXPECT_EQ(states[9]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[9]->get_transitions('a'), empty);
    EXPECT_EQ(states[9]->get_transitions('b'), std::vector<const NFANode*>{states[10]});

    EXPECT_EQ(states[10]->get_transitions(NFA::SpecialTransitons::EPSILON), std::vector<const NFANode*>{states[11]});
    EXPECT_EQ(states[10]->get_transitions('a'), empty);
    EXPECT_EQ(states[10]->get_transitions('b'), empty);

    c = std::vector<const NFANode*>{
        {states[6], states[12]}
    };
    EXPECT_EQ(states[11]->get_transitions(NFA::SpecialTransitons::EPSILON), c);
    EXPECT_EQ(states[11]->get_transitions('a'), empty);
    EXPECT_EQ(states[11]->get_transitions('b'), empty);

    EXPECT_EQ(states[12]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[12]->get_transitions('a'), std::vector<const NFANode*>{states[13]});
    EXPECT_EQ(states[12]->get_transitions('b'), empty);

    EXPECT_EQ(states[13]->get_transitions(NFA::SpecialTransitons::EPSILON), empty);
    EXPECT_EQ(states[13]->get_transitions('a'), empty);
    EXPECT_EQ(states[13]->get_transitions('b'), empty);
}

using namespace thorin;
TEST(Automaton, Regex2NFA) {
    Driver driver;
    World& w    = driver.world();
    auto parser = Parser(w);
    for (auto plugin : {"compile", "mem", "core", "math", "regex"}) parser.plugin(plugin);

    auto pattern = w.call<regex::conj>(
        2, w.tuple({w.call<regex::lit>(w.lit_idx(256, 'a')), w.call<regex::lit>(w.lit_idx(256, 'b'))})); // (a & b)
    pattern->dump(10);
    auto nfa = regex::regex2nfa(pattern);
    std::cout << *nfa;
}

TEST(Automaton, Regex2NFAAorBplusA) {
    Driver driver;
    World& w    = driver.world();
    auto parser = Parser(w);
    for (auto plugin : {"compile", "mem", "core", "math", "regex"}) parser.plugin(plugin);

    auto pattern = w.call<regex::conj>(
        2,
        w.tuple({w.call(regex::quant::plus, w.call<regex::disj>(2, w.tuple({w.call<regex::lit>(w.lit_idx(256, 'a')),
                                                                            w.call<regex::lit>(w.lit_idx(256, 'b'))}))),
                 w.call<regex::lit>(w.lit_idx(256, 'a'))})); // (a & b)
    pattern->dump(10);
    auto nfa = regex::regex2nfa(pattern);
    std::cout << *nfa;

    auto dfa = nfa2dfa(*nfa);
    std::cout << *dfa;
    std::cout << *minimize_dfa(*dfa);
}

TEST(Automaton, Regex2NFA1or5or9) {
    Driver driver;
    World& w    = driver.world();
    auto parser = Parser(w);
    for (auto plugin : {"compile", "mem", "core", "math", "regex"}) parser.plugin(plugin);

    // %regex.disj 2 (%regex.disj 2 (%regex.range ‹2; 49:(.Idx 256)›, %regex.range ‹2; 53:(.Idx 256)›), %regex.range ‹2;
    // 57:(.Idx 256)›)
    auto pattern
        = w.call<regex::disj>(2, w.tuple({w.call<regex::disj>(2, w.tuple({w.call<regex::lit>(w.lit_idx(256, '1')),
                                                                          w.call<regex::lit>(w.lit_idx(256, '5'))})),
                                          w.call<regex::lit>(w.lit_idx(256, '9'))})); // (a & b)
    pattern->dump(10);
    auto nfa = regex::regex2nfa(pattern);
    std::cout << *nfa;

    auto dfa = nfa2dfa(*nfa);
    std::cout << *dfa;
    std::cout << *minimize_dfa(*dfa);
}

TEST(Automaton, DFA) {
    auto dfa   = std::make_unique<DFA>();
    auto start = dfa->add_state();
    dfa->set_start(start);
    auto second = dfa->add_state();
    start->add_transition(second, 'a');
    start->add_transition(second, 'b');
    second->add_transition(second, 'b');
    auto third = dfa->add_state();
    third->set_accepting(true);
    second->add_transition(third, 'a');
    third->add_transition(third, 'a');
    third->add_transition(second, 'b');

    EXPECT_EQ(dfa->get_start(), start);
    EXPECT_FALSE(start->is_accepting());
    EXPECT_TRUE(third->is_accepting());
    EXPECT_EQ(start->get_transition('a'), second);
    EXPECT_EQ(start->get_transition('b'), second);
    EXPECT_EQ(second->get_transition('a'), third);
    EXPECT_EQ(second->get_transition('b'), second);
    EXPECT_EQ(third->get_transition('a'), third);
    EXPECT_EQ(third->get_transition('b'), second);

    std::cout << *dfa;
    std::cout << *minimize_dfa(*dfa);
}

TEST(Automaton, DFAMin) {
    // https://cyberzhg.github.io/toolbox/nfa2dfa?regex=KGF8YikrYQ==
    auto dfa   = std::make_unique<DFA>();
    auto start = dfa->add_state();
    dfa->set_start(start);
    auto stateB = dfa->add_state();
    start->add_transition(stateB, 'a');
    auto stateC = dfa->add_state();
    start->add_transition(stateC, 'b');
    auto stateD = dfa->add_state();
    stateD->set_accepting(true);
    stateB->add_transition(stateD, 'a');
    stateC->add_transition(stateD, 'a');
    stateD->add_transition(stateD, 'a');
    auto stateE = dfa->add_state();
    stateB->add_transition(stateE, 'b');
    stateC->add_transition(stateE, 'b');
    stateD->add_transition(stateE, 'b');
    stateE->add_transition(stateE, 'b');
    stateE->add_transition(stateD, 'a');

    EXPECT_EQ(dfa->get_start(), start);
    EXPECT_FALSE(start->is_accepting());
    EXPECT_TRUE(stateD->is_accepting());
    EXPECT_EQ(start->get_transition('a'), stateB);
    EXPECT_EQ(start->get_transition('b'), stateC);
    EXPECT_EQ(stateB->get_transition('a'), stateD);
    EXPECT_EQ(stateB->get_transition('b'), stateE);
    EXPECT_EQ(stateC->get_transition('a'), stateD);
    EXPECT_EQ(stateC->get_transition('b'), stateE);
    EXPECT_EQ(stateD->get_transition('a'), stateD);
    EXPECT_EQ(stateD->get_transition('b'), stateE);
    EXPECT_EQ(stateE->get_transition('a'), stateD);
    EXPECT_EQ(stateE->get_transition('b'), stateE);

    std::cout << *dfa;
    auto min_dfa = minimize_dfa(*dfa);
    std::cout << *min_dfa;

    auto min_start = min_dfa->get_start();
    EXPECT_FALSE(min_start->is_accepting());

    auto min_stateB = min_start->get_transition('a');
    EXPECT_EQ(min_start->get_transition('b'), min_stateB);

    auto min_stateC = min_stateB->get_transition('a');
    EXPECT_EQ(min_stateB->get_transition('b'), min_stateB);

    EXPECT_TRUE(min_stateC->is_accepting());
    EXPECT_EQ(min_stateC->get_transition('a'), min_stateC);
    EXPECT_EQ(min_stateC->get_transition('b'), min_stateB);
}
