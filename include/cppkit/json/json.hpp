#pragma once

#include "cppkit/reflection/reflection.hpp"
#include <type_traits>
#include <cctype>
#include <iomanip>
#include <map>
#include <set>
#include <unordered_set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <list>
#include <variant>
#include <functional>
#include <iostream>

namespace cppkit::json
{
    namespace internal
    {
        template <typename T>
        struct is_set_container : std::false_type
        {
        };

        template <typename T, typename A>
        struct is_set_container<std::unordered_set<T, A>> : std::true_type
        {
        };

        template <typename T, typename A>
        struct is_set_container<std::set<T, A>> : std::true_type
        {
        };

        template <typename T>
        constexpr bool is_set_container_v = is_set_container<T>::value;

        template <typename T>
        struct is_map_container : std::false_type
        {
        };

        template <typename T, typename A>
        struct is_map_container<std::unordered_map<T, A>> : std::true_type
        {
        };

        template <typename T, typename A>
        struct is_map_container<std::map<T, A>> : std::true_type
        {
        };

        template <typename T>
        constexpr bool is_map_container_v = is_map_container<T>::value;

        template <typename T>
        struct is_sequence_container : std::false_type
        {
        };

        template <typename T, typename A>
        struct is_sequence_container<std::vector<T, A>> : std::true_type
        {
        };

        template <typename T, typename A>
        struct is_sequence_container<std::list<T, A>> : std::true_type
        {
        };

        template <typename T, typename A>
        struct is_sequence_container<std::deque<T, A>> : std::true_type
        {
        };

        template <typename T>
        constexpr bool is_sequence_container_v = is_sequence_container<T>::value;

        template <typename T, typename = void>
        struct is_reflectable : std::false_type
        {
        };

        template <typename T>
        struct is_reflectable<T, std::void_t<decltype(reflection::MetaData<T>::info())>> : std::true_type
        {
        };

        template <typename T>
        constexpr bool is_reflectable_v = is_reflectable<T>::value;
    }

    class Json
    {
    public:
        using array = std::vector<Json>;
        using object = std::map<std::string, Json>;
        using value_type = std::variant<std::nullptr_t, bool, double, std::string, array, object>;

        value_type v;

        Json() noexcept : v(nullptr)
        {
        }

        explicit Json(std::nullptr_t) noexcept : v(nullptr)
        {
        }

        explicit Json(bool b) noexcept : v(b)
        {
        }

        explicit Json(const int i) noexcept : v(static_cast<double>(i))
        {
        }

        explicit Json(const long l) noexcept : v(static_cast<double>(l))
        {
        }

        explicit Json(double d) noexcept : v(d)
        {
        }

        explicit Json(const char* s) : v(std::string(s))
        {
        }

        explicit Json(const std::string& s) : v(s)
        {
        }

        explicit Json(std::string&& s) : v(std::string(std::move(s)))
        {
        }

        explicit Json(const array& a) : v(a)
        {
        }

        explicit Json(array&& a) : v(std::move(a))
        {
        }

        explicit Json(const object& o) : v(o)
        {
        }

        explicit Json(object&& o) : v(std::move(o))
        {
        }

        static Json makeArray() { return Json(array{}); }

        static Json makeObject() { return Json(object{}); }

        [[nodiscard]]
        bool isNull() const { return std::holds_alternative<std::nullptr_t>(v); }

        [[nodiscard]]
        bool isBool() const { return std::holds_alternative<bool>(v); }

        [[nodiscard]]
        bool isNumber() const { return std::holds_alternative<double>(v); }

        [[nodiscard]]
        bool isString() const { return std::holds_alternative<std::string>(v); }

        [[nodiscard]]
        bool isArray() const { return std::holds_alternative<array>(v); }

        [[nodiscard]]
        bool isObject() const { return std::holds_alternative<object>(v); }

        bool& asBool() { return std::get<bool>(v); }

        double& asNumber() { return std::get<double>(v); }

        std::string& asString() { return std::get<std::string>(v); }

        array& asArray() { return std::get<array>(v); }

        object& asObject() { return std::get<object>(v); }

        [[nodiscard]]
        const bool& asBool() const { return std::get<bool>(v); }

        [[nodiscard]]
        const double& asNumber() const { return std::get<double>(v); }

        [[nodiscard]]
        const std::string& asString() const { return std::get<std::string>(v); }

        [[nodiscard]]
        const array& asArray() const { return std::get<array>(v); }

        [[nodiscard]]
        const object& asObject() const { return std::get<object>(v); }

        Json& operator[](const std::string& key)
        {
            if (!isObject())
                v = object{};
            return std::get<object>(v)[key];
        }

        const Json& operator[](const std::string& key) const { return std::get<object>(v).at(key); }

        Json& operator[](const size_t idx)
        {
            if (!isArray())
                throw std::runtime_error("not an array");
            return std::get<array>(v).at(idx);
        }

        template <typename T, typename = std::enable_if_t<
                                  !std::is_same_v<std::decay_t<T>, Json> &&
                                  !std::is_same_v<std::decay_t<T>, array> &&
                                  !std::is_same_v<std::decay_t<T>, object>>>
        explicit Json(const T& obj)
        {
            using DecayT = std::decay_t<T>;
            if constexpr (std::is_arithmetic_v<DecayT>)
            {
                if constexpr (std::is_same_v<DecayT, bool>)
                {
                    v = static_cast<bool>(obj);
                }
                else
                {
                    v = static_cast<double>(obj);
                }
            }
            else if constexpr (std::is_convertible_v<T, std::string_view>)
            {
                v = std::string(obj);
            }
            else if constexpr (internal::is_sequence_container_v<T>)
            {
                array arr;
                arr.reserve(obj.size());
                for (const auto& item : obj)
                {
                    arr.emplace_back(item);
                }
                v = std::move(arr);
            }
            else if constexpr (internal::is_map_container_v<T>)
            {
                object objJson;
                for (const auto& [key, value] : obj)
                {
                    objJson.emplace(key, Json(value));
                }
                v = std::move(objJson);
            }
            else if constexpr (internal::is_reflectable_v<DecayT>)
            {
                object objJson;
                reflection::forEachField(obj, [&](const std::string_view name, const auto& value)
                {
                    objJson.emplace(name, Json(value));
                });
                v = std::move(objJson);
            }
            else
            {
                static_assert(internal::is_reflectable_v<T>,
                              "Type is not reflectable. Please register it using REFLECT macro.");
            }
        }

        template <typename T, typename = std::enable_if_t<
                                  !std::is_same_v<std::decay_t<T>, Json> &&
                                  !std::is_same_v<std::decay_t<T>, array> &&
                                  !std::is_same_v<std::decay_t<T>, object>>>
        explicit Json(T&& obj) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            using DecayT = std::decay_t<T>;
            if constexpr (std::is_arithmetic_v<DecayT>)
            {
                if constexpr (std::is_same_v<DecayT, bool>)
                {
                    v = static_cast<bool>(obj);
                }
                else
                {
                    v = static_cast<double>(obj);
                }
            }
            else if constexpr (std::is_convertible_v<T, std::string_view>)
            {
                v = std::string(std::forward<T>(obj));
            }
            else if constexpr (internal::is_sequence_container_v<T>)
            {
                array arr;
                arr.reserve(obj.size());
                for (auto& item : obj)
                {
                    arr.emplace_back(std::move(item));
                }
                v = std::move(arr);
            }
            else if constexpr (internal::is_map_container_v<T>)
            {
                object objJson;
                for (auto&& [key, value] : obj)
                {
                    objJson.emplace(std::move(key), Json(std::move(value)));
                }
                v = std::move(objJson);
            }
            else if constexpr (internal::is_reflectable_v<DecayT>)
            {
                object objJson;
                reflection::forEachField(std::forward<T>(obj), [&]<typename T0>(const std::string_view name, T0&& value)
                {
                    objJson.emplace(name, Json(std::forward<T0>(value)));
                });
                v = std::move(objJson);
            }
            else
            {
                static_assert(internal::is_reflectable_v<T>,
                              "Type is not reflectable. Please register it using REFLECT macro.");
            }
        }

        // Assignment operators
        Json& operator=(std::nullptr_t) noexcept
        {
            v = nullptr;
            return *this;
        }

        Json& operator=(bool b) noexcept
        {
            v = b;
            return *this;
        }

        Json& operator=(const int i) noexcept
        {
            v = static_cast<double>(i);
            return *this;
        }

        Json& operator=(const long l) noexcept
        {
            v = static_cast<double>(l);
            return *this;
        }

        Json& operator=(double d) noexcept
        {
            v = d;
            return *this;
        }

        Json& operator=(const char* s)
        {
            v = std::string(s);
            return *this;
        }

        Json& operator=(const std::string& s)
        {
            v = s;
            return *this;
        }

        Json& operator=(std::string&& s)
        {
            v = std::move(s);
            return *this;
        }

        Json& operator=(const array& a)
        {
            v = a;
            return *this;
        }

        Json& operator=(array&& a)
        {
            v = std::move(a);
            return *this;
        }

        Json& operator=(const object& o)
        {
            v = o;
            return *this;
        }

        Json& operator=(object&& o)
        {
            v = std::move(o);
            return *this;
        }

        template <typename T>
        Json& operator=(const T obj)
        {
            using DecayT = std::decay_t<T>;
            if constexpr (std::is_arithmetic_v<DecayT>)
            {
                if constexpr (std::is_same_v<T, bool>)
                {
                    v = static_cast<bool>(obj);
                }
                else
                {
                    v = static_cast<double>(obj);
                }
            }
            else if constexpr (std::is_convertible_v<T, std::string_view>)
            {
                v = std::string(obj);
            }
            else if constexpr (internal::is_sequence_container_v<T>)
            {
                array arr;
                arr.reserve(obj.size());
                for (const auto& item : obj)
                {
                    arr.emplace_back(item);
                }
                v = std::move(arr);
            }
            else if constexpr (internal::is_map_container_v<T>)
            {
                object objJson;
                for (const auto& [key, value] : obj)
                {
                    objJson.emplace(key, Json(value));
                }
                v = std::move(objJson);
            }
            else if constexpr (internal::is_reflectable_v<DecayT>)
            {
                object objJson;
                reflection::forEachField(obj, [&](const std::string_view name, const auto& value)
                {
                    objJson.emplace(name, Json(value));
                });
                v = std::move(objJson);
            }
            else
            {
                static_assert(internal::is_reflectable_v<T>,
                              "Type is not reflectable. Please register it using REFLECT macro.");
            }
            return *this;
        }

        // Serialization
        [[nodiscard]]
        std::string dump(bool pretty = false, int indent_size = 2) const;

    private:
        static void escapeString(std::ostream& os, const std::string& s);

        void dumpCompact(std::ostream& os) const;

        void dumpPretty(std::ostream& os, int depth, int indentSize) const;

    public:
        static Json parse(const std::string& s);
    };

    template <typename T>
    std::string stringify(const T& obj)
    {
        using Type = std::decay_t<T>;

        if constexpr (std::is_same_v<Type, Json>) // Json类型
        {
            return obj.dump();
        }
        else if constexpr (std::is_arithmetic_v<Type>) // 算术类型
        {
            if constexpr (std::is_same_v<Type, bool>)
            {
                return obj ? "true" : "false";
            }
            else
            {
                return std::to_string(obj);
            }
        }
        else if constexpr (std::is_convertible_v<Type, std::string_view>) // 字符串类型
        {
            std::ostringstream oss;
            oss << '"';
            for (unsigned char c : std::string(obj))
            {
                switch (c)
                {
                case '"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\b':
                    oss << "\\b";
                    break;
                case '\f':
                    oss << "\\f";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << (int)c <<
                            std::dec
                            << std::nouppercase;
                    }
                    else
                        oss << c;
                }
            }
            oss << '"';
            return oss.str();
        }
        else if constexpr (internal::is_sequence_container_v<Type> || internal::is_set_container_v<Type>) // 顺序或者set容器
        {
            std::string s = "[";
            bool first = true;
            for (auto& item : obj)
            {
                if (!first) s += ",";
                first = false;
                // 递归调用
                s += stringify(item);
            }
            s += "]";
            return s;
        }
        else if constexpr (internal::is_map_container_v<Type>) // map类型
        {
            std::string s = "{";
            bool first = true;
            for (const auto& [key, value] : obj)
            {
                if (!first) s += ",";
                first = false;

                // Key 必须转为 string
                std::ostringstream key_oss;
                key_oss << '"';
                if constexpr (std::is_arithmetic_v<std::decay_t<decltype(key)>>)
                {
                    key_oss << std::to_string(key);
                }
                else
                {
                    for (unsigned char c : std::string(key))
                    {
                        switch (c)
                        {
                        case '"':
                            key_oss << "\\\"";
                            break;
                        case '\\':
                            key_oss << "\\\\";
                            break;
                        case '\b':
                            key_oss << "\\b";
                            break;
                        case '\f':
                            key_oss << "\\f";
                            break;
                        case '\n':
                            key_oss << "\\n";
                            break;
                        case '\r':
                            key_oss << "\\r";
                            break;
                        case '\t':
                            key_oss << "\\t";
                            break;
                        default:
                            if (c < 0x20)
                            {
                                key_oss << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << (
                                        int)c <<
                                    std::dec
                                    << std::nouppercase;
                            }
                            else
                                key_oss << c;
                        }
                    }
                }
                key_oss << '"' << ':';
                s += key_oss.str();
                s += stringify(value);
            }
            s += "}";
            return s;
        }
        else if constexpr (internal::is_reflectable_v<Type>) // 自定义类型
        {
            std::string s = "{";
            bool first = true;
            reflection::forEachField(obj, [&first,&s](const std::string_view name, const auto& field_value)
            {
                if (!first) s += ",";
                first = false;
                s += "\"";
                s += name;
                s += "\":";
                // 递归调用
                s += stringify(field_value);
            });
            s += "}";
            return s;
        }
        else
        {
            static_assert(std::is_void_v<Type>, "Type is not registered with REFLECT macro!");
        }
        return "";
    }
} // namespace cppkit::json
