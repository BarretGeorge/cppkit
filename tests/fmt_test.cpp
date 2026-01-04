#include "cppkit/fmt.hpp"
#include "cppkit/strings.hpp"

int main()
{
    // 无占位符
    cppkit::print("hello bob");

    // 占位符
    cppkit::print("hello {}", "bob");

    // 多个占位符
    cppkit::print("hello {} {}", 2025, "bob");

    // 转义
    cppkit::print("转义效果{{}");

    const auto str = cppkit::sprintf("hello {}", "bob");
    std::cout << str << std::endl;

    std::cout << cppkit::replaceAll(str, "l", "L") << std::endl;

    std::cout << cppkit::replace(str, "l", "L", 1) << std::endl;

    const auto escaped = cppkit::escapeHtml("<html></html>");
    std::cout << escaped << std::endl;
    std::cout << cppkit::unescapeHtml(escaped) << std::endl;
    return 0;
}
