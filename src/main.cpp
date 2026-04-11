#include "SandCube.h"

#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string serverUri = DEFAULTSERVERURI;
    if (argc > 1) {
        serverUri = argv[1];
    }

    SandCube app(serverUri);
    app.start();

    // libmatrixapplication runs the loop() in its own thread — keep main alive.
    while (true) {
        sleep(2);
    }

    return 0;
}
