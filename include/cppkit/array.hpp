#pragma once

#include <vector>

namespace cppkit
{
    template <typename T, typename Func>
    auto arrayMap(const std::vector<T>& vec, Func func)
    {
        using ResultType = std::invoke_result_t<Func, T>;

        std::vector<ResultType> result;
        result.reserve(vec.size());

        for (const auto& item : vec)
        {
            result.push_back(func(item));
        }
        return result;
    }
} // namespace cppkit
