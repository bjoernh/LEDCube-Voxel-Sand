#include "AppTemplate.h"
#include <string>
#include <iostream>
#include <unistd.h>

int main(int argc, char *argv[]) {
    // DEFAULTSERVERURI is typically defined in the matrixapplication headers
    std::string serverUri = DEFAULTSERVERURI; 

    // Basic argument parsing
    if (argc > 1) {
        serverUri = argv[1];
    }

    // Instantiate and start our custom application logic
    AppTemplate app(serverUri);
    app.start();
    
    // The application logic runs in its own thread from the libmatrixapplication framework.
    // The main thread simply waits.
    while(1) sleep(2);
    
    return 0;
}
