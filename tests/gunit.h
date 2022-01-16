#ifndef _GUNIT_H_
#define _GUNIT_H_

// A simple version of test framework that mimics gunit.
// Has no external dependencies.

#include <iostream>

namespace testing
{
    class Test;
    struct TestRegRecord {
        const char* name;
        TestRegRecord* next;
        Test* (*fn)();
        TestRegRecord(const char* name, Test* (*fn)());
    };

    class Test {
    public:
        virtual void SetUp() {}
        virtual void Run() {}
        virtual void TearDown() {}
    };

    template<typename COMPARER, typename T>
    bool check(T a, T b, const COMPARER& cond, const char* comp_name) {
        if (cond(a, b))
            return true;
        std::cerr << a << comp_name << b << " at ";
        return false;
    }

    struct crlf {
        const char* file;
        int line;
        crlf(const char* file, int line) : file(file), line(line) {}
        ~crlf() { std::cerr << std::endl; }
        friend std::ostream& operator<< (std::ostream& stream, const crlf& crlf) {
            return stream << crlf.file << ":" << crlf.line << " ";
        }
    };

    struct fail {
        const char* file;
        int line;
        fail(const char* file, int line) : file(file), line(line) {}
        ~fail() {
            std::cerr << std::endl << std::flush;
            std::terminate();
        }
        friend std::ostream& operator<< (std::ostream& stream, const fail& crlf) {
            return stream << crlf.file << ":" << crlf.line << " ";
        }
    };
}

int RUN_ALL_TESTS();

#define TEST_STR_(S) #S
#define TEST_STR(S) TEST_STR_(S)
#define TEST_CLASS_NAME(CASE, TST) CASE##_##TST##_Test

#define TEST_(CASE, TST, BASE)                                        \
  class TEST_CLASS_NAME(CASE, TST) : public BASE {                    \
   public:                                                            \
    static Test* Create_() { return new TEST_CLASS_NAME(CASE, TST); } \
    void Run() override;                                              \
  };                                                                  \
  ::testing::TestRegRecord CASE##_##TST##reg(                         \
      TEST_STR(TEST_CLASS_NAME(CASE, TST)),                           \
      TEST_CLASS_NAME(CASE, TST)::Create_);                           \
  void TEST_CLASS_NAME(CASE, TST)::Run()

#define TEST(CASE, TST) TEST_(CASE, TST, ::testing::Test)
#define TEST_F(CASE, TST) TEST_(CASE, TST, CASE)

#define ASSERT_COND(A, B, COND, COND_TEXT) testing::check(A, B, COND<const decltype(A)>(), COND_TEXT) ? std::cout : std::cout << testing::fail(__FILE__, __LINE__) 
#define EXPECT_COND(A, B, COND, COND_TEXT) testing::check(A, B, COND<const decltype(A)>(), COND_TEXT) ? std::cout : std::cout << testing::crlf(__FILE__, __LINE__) 

#define ASSERT_EQ(A, B) ASSERT_COND(A, B, std::equal_to, "!=")
#define ASSERT_NE(A, B) ASSERT_COND(A, B, std::not_equal_to, "==")
#define ASSERT_LT(A, B) ASSERT_COND(A, B, std::less, ">=")
#define ASSERT_LE(A, B) ASSERT_COND(A, B, std::less_equal, ">")
#define ASSERT_TRUE(A) ASSERT_COND(A, true, std::equal_to, "!=")
#define ASSERT_FALSE(A) ASSERT_COND(A, false, std::equal_to, "!=")

#define EXPECT_EQ(A, B) EXPECT_COND(A, B, std::equal_to, "!=")
#define EXPECT_NE(A, B) EXPECT_COND(A, B, std::not_equal_to, "==")
#define EXPECT_LT(A, B) EXPECT_COND(A, B, std::less, ">=")
#define EXPECT_LE(A, B) EXPECT_COND(A, B, std::less_equal, ">")
#define EXPECT_TRUE(A) EXPECT_COND(A, true, std::equal_to, "!=")
#define EXPECT_FALSE(A) EXPECT_COND(A, false, std::equal_to, "!=")

#endif  // _GUNIT_H_
