#include "WaterCube.h"

#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string serverUri = DEFAULTSERVERURI;
    bool imuDebug = false;
    bool profile  = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a(argv[i]);
        if (a == "--imu-debug")      imuDebug = true;
        else if (a == "--profile")   profile  = true;
        else                         serverUri = a;
    }

    WaterCube app(serverUri, imuDebug, profile);
    app.start();

    while (true) {
        sleep(2);
    }

    return 0;
}
