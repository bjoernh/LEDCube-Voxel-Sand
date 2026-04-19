#include "WaterCube.h"

#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string serverUri = DEFAULTSERVERURI;
    if (argc > 1) {
        serverUri = argv[1];
    }

    WaterCube app(serverUri);
    app.start();

    while (true) {
        sleep(2);
    }

    return 0;
}
