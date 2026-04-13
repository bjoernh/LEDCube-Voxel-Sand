#include "SandCube.h"

#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string serverUri = DEFAULTSERVERURI;
    bool imuDebug = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--imu-debug") {
            imuDebug = true;
        } else {
            serverUri = argv[i];
        }
    }

    SandCube app(serverUri, imuDebug);
    app.start();

    // libmatrixapplication runs the loop() in its own thread — keep main alive.
    while (true) {
        sleep(2);
    }

    return 0;
}
