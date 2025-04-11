#include <print>

#include "./tftp/tftp.hpp"


int main() {
    tftp::TFTPServer server{};

    std::println("Starting server");
    server.instance();
}