#include "cppkit/json/json.hpp"
#include <iostream>

#include "cppkit/json/json_parser.hpp"

using namespace cppkit::json;

struct Address
{
    std::string city;
    int zip{};
};

// 注册 Address
REFLECT(Address, FIELD(city), FIELD(zip))

struct User
{
    int id{};
    std::string name;
    std::vector<double> scores;
    Address address;
    std::vector<Address> oldAddresses;
    std::set<std::string> tags;
    std::map<std::string, int> loginLog;
};

// 注册 User
REFLECT(User,
        FIELD(id),
        FIELD(name),
        FIELD(scores),
        FIELD(address),
        FIELD(oldAddresses),
        FIELD(tags),
        FIELD(loginLog)
)

void unmanagedJsonExample()
{
    const auto jsonStr = R"({})";

    const auto user = cppkit::json::fromJson<User>(jsonStr);
    std::cout << "Name: " << user.name << std::endl;
}

int main(int argc, char* argv[])
{
    unmanagedJsonExample();

    const auto jsonStr = R"({"name":"lsm1998","age":50,"subscribes":["YouTube","qq music",100]})";

    if (auto json = Json::parse(jsonStr); json["name"].isString())
    {
        std::cout << json["name"].asString() << std::endl;
    }

    auto obj = Json{};
    obj["greeting"] = "Hello, JSON!";
    obj["year"] = 2025;

    obj["name"] = Json("bob");
    obj["is_active"] = Json(true);

    std::vector<User> users{
        {
            1,
            "lsm1998",
            {99.5, 88.0},
            {"mast", 10001},
            {{"Beijing", 100086}, {"Tokyo", 100000}},
            {"Java", "Golang"},
            {
                {
                    "Beijing", 1765609508
                }
            }
        }
    };

    obj["list"] = users;
    obj["map"] = std::map<std::string, int>{{"one", 1}, {"two", 2}};

    std::cout << obj.dump() << std::endl;

    User u;
    u.id = 1;
    u.name = "lsm1998";
    u.scores = {99.5, 88.0};
    u.address = {"mast", 10001};
    u.oldAddresses = {{"Beijing", 100086}, {"Tokyo", 100000}};
    u.tags = {"Java", "Golang"};
    u.loginLog = {
        {
            "Beijing", 1765609508
        }
    };

    // 万能序列化函数
    std::cout << stringify(u) << std::endl;

    // 构建复杂字符串，带有换行等特殊字符
    u.name = "lsm\n\"1998\"\t\\/";

    // 先序列化然后打印
    std::string json_str = stringify(u);
    std::cout << "Serialized JSON: " << json_str << std::endl;
    std::cout << "JSON length: " << json_str.length() << std::endl;

    // 万能反序列化函数
    auto t = cppkit::json::fromJson<User>(json_str);

    std::cout << t.name << std::endl;

    auto j = Json();
    j["name"] = "hello";
    j["age"] = 30;
    std::cout << stringify(j) << std::endl;
    return 0;
}
