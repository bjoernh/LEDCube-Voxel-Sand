#ifndef APP_TEMPLATE_H
#define APP_TEMPLATE_H

#include <CubeApplication.h>
#include <string>

class AppTemplate : public CubeApplication {
public:
    AppTemplate(std::string serverUri);
    bool loop() override;
};

#endif //APP_TEMPLATE_H
