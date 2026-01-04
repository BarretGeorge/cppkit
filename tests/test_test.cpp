#include "cppkit/testing/test.hpp"

#include "cppkit/array.hpp"

using namespace cppkit::testing;

int Add(const int a, const int b)
{
    return a + b;
}

TEST(MathSuite, CanAddTwoNumbers)
{
    EXPECT_EQ(5, Add(2, 3));
    EXPECT_EQ(0, Add(-1, 1));
}

TEST(MathSuite, AssertFailureStopsTest)
{
    ASSERT_EQ(10, Add(5, 5));
    ASSERT_EQ(20, Add(10, 9));
    EXPECT_TRUE(false);
}

class StringFixture : public Test
{
protected:
    void SetUp() override
    {
        // 在每个测试前运行
        str_ = "Hello, World";
        std::cout << "StringFixture SetUp" << std::endl;
    }

    void TearDown() override
    {
        // 在每个测试后运行
        str_.clear();
        std::cout << "StringFixture TearDown" << std::endl;
    }

    std::string str_;
};

TEST_F(StringFixture, SubstringTest)
{
    ASSERT_EQ("Hello", str_.substr(0, 5));
}

int main()
{
    const std::vector v = {1, 2, 3, 4, 5};

    for (const auto strVec = cppkit::arrayMap(v, [](int x) { return std::to_string(x); }); const auto& s : strVec)
    {
        std::cout << s << " ";
    }
    return RunAllTests();
}
