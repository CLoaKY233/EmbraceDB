// CONFIG CHECK - DUMMY FILE

#include <fmt/core.h>
#include <string>
#include <vector>

struct User {
    int id;
    std::string username;
};

auto main() -> int {
    std::vector<User> users;
    users.push_back({1, "admin"});
    users.push_back({2, "guest"});

    fmt::print("Database loaded with {} users.\n", users.size());

    for (const auto &user : users) {
        fmt::print("ID: {}, Name: {}\n", user.id, user.username);
    }

    return 0;
}
