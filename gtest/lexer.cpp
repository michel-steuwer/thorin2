#include "thorin/fe/lexer.h"

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "thorin/driver.h"

using namespace std::literals;
using namespace thorin;

TEST(Lexer, Toks) {
    Driver driver;
    std::istringstream is("{ } ( ) [ ] ‹ › « » : , . .lam .Pi λ Π 23₀₁₂₃₄₅₆₇₈₉");
    Lexer lexer(driver.world(), is);

    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_brace_l));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_brace_r));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_paren_l));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_paren_r));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_brckt_l));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_brckt_r));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_angle_l));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_angle_r));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_quote_l));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::D_quote_r));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::T_colon));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::T_comma));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::T_dot));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::K_lam));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::K_Pi));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::T_lm));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::T_Pi));
    auto tok = lexer.lex();
    EXPECT_TRUE(tok.isa(Tok::Tag::L_i));
    EXPECT_TRUE(lexer.lex().isa(Tok::Tag::EoF));
    EXPECT_EQ(tok.lit_i(), driver.world().lit_idx(123456789, 23));
}

TEST(Lexer, Errors) {
    Driver driver;
    std::istringstream is1("asdf \xc0\xc0");
    Lexer l1(driver.world(), is1);
    l1.lex();
    EXPECT_ANY_THROW(l1.lex());

    std::istringstream is2("foo \xaa");
    Lexer l2(driver.world(), is2);
    l2.lex();
    EXPECT_ANY_THROW(l2.lex());

    std::istringstream is3("+");
    Lexer l3(driver.world(), is3);
    EXPECT_ANY_THROW(l3.lex());

    std::istringstream is4("-");
    Lexer l4(driver.world(), is4);
    EXPECT_ANY_THROW(l4.lex());
}

TEST(Lexer, Eof) {
    Driver driver;
    std::istringstream is("");

    Lexer lexer(driver.world(), is);
    for (int i = 0; i < 10; i++) EXPECT_TRUE(lexer.lex().isa(Tok::Tag::EoF));
}

class Real : public testing::TestWithParam<int> {};

TEST_P(Real, sign) {
    Driver driver;
    World& w = driver.world();

    // clang-format off
    auto check = [&w](std::string s, f64 r) {
        const auto sign = GetParam();
        switch (sign) {
            case 0: break;
            case 1: s.insert(0, "+"sv); break;
            case 2: s.insert(0, "-"sv); break;
            default: fe::unreachable();
        }

        std::istringstream is(s);
        Lexer lexer(w, is);

        auto tag = lexer.lex();
        EXPECT_TRUE(tag.isa(Tok::Tag::L_f));
        EXPECT_EQ(std::bit_cast<f64>(tag.lit_u()), sign == 2 ? -r : r);
    };

    check(  "2e+3",   2e+3); check(  "2E3",   2E3);
    check( "2.e-3",  2.e-3); check( "2.E3",  2.E3); check( "2.3",  2.3);
    check( ".2e+3",  .2e+3); check( ".2E3",  .2E3); check( ".23",  .23);
    check("2.3e-4", 2.3e-4); check("2.3E4", 2.3E4); check("2.34", 2.34);

    check(  "0x2p+3",   0x2p+3); check(  "0x2P3",   0x2P3);
    check( "0x2.p-3",  0x2.p-3); check( "0x2.P3",  0x2.P3);
    check( "0x.2p+3",  0x.2p+3); check( "0x.2P3",  0x.2P3);
    check("0x2.3p-4", 0x2.3p-4); check("0x2.3P4", 0x2.3P4);
    // clang-format on

    std::istringstream is1("0x2.34");
    Lexer l1(w, is1);
    EXPECT_ANY_THROW(l1.lex());

    std::istringstream is2("2.34e");
    Lexer l2(w, is2);
    EXPECT_ANY_THROW(l2.lex());
}

INSTANTIATE_TEST_SUITE_P(Lexer, Real, testing::Range(0, 3));
