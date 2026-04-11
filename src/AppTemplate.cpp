#include "AppTemplate.h"
#include <iostream>

AppTemplate::AppTemplate(std::string serverUri) : CubeApplication(30, serverUri, "AppTemplate") {
    // Initialization code
    // The second parameter to CubeApplication is typical for application ID/type.
    std::cout << "AppTemplate Initialized. Connecting to: " << serverUri << std::endl;
}

bool AppTemplate::loop() {
    // Hue ranges from 0 to 360
    static float hue = 0.0f;
    hue += 2.0f; 
    if (hue >= 360.0f) {
        hue -= 360.0f;
    }
    
    Color textColor;
    textColor.fromHSV(hue, 1.0f, 1.0f);
    
    // Clear previous frame
    clear();
    
    // Draw screen names/numbers to indicate layout
    // Coordinates are roughly centered for a 64x64 screen
    drawText(front,  Vector2i(25, 30), textColor, "0-Front");
    drawText(right,  Vector2i(25, 30), textColor, "1-Right");
    drawText(back,   Vector2i(25, 30), textColor, "2-Back");
    drawText(left,   Vector2i(25, 30), textColor, "3-Left");
    drawText(top,    Vector2i(25, 30), textColor, "4-Top");
    drawText(bottom, Vector2i(25, 30), textColor, "5-Bottom");
    
    // Render the updated contents
    render();
    
    // Returning true tells the underlying threaded engine to continue looping
    return true;
}
