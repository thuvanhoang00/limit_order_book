#include "../hdr/server.h"
int main()
{
    // nlohmann::json j =
    // {
    //     {"pi", 3.141},
    //     {"happy", true},
    //     {"name", "Niels"}
    // };
    // std::cout << nlohmann::to_string(j) << std::endl;
    thu::Server::getInstance().start();
    return 0;
}