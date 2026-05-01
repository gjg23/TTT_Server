// main.cpp
// Main server logic
#include "server.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Start server
    Server server(atoi(argv[1]), "accounts.dat");
    server.run();
    exit(EXIT_SUCCESS);
}