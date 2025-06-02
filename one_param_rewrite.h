#ifndef PROXCARD_ONEPARAM_REWRITE_H
#define PROXCARD_ONEPARAM_REWRITE_H

#include <ESPAsyncWebServer.h>

class OneParamRewrite : public AsyncWebRewrite {
private:
    String _urlPrefix;
    String _urlSuffix;
    int _paramStart;
    int _paramEnd;
    String _paramsBackup;
    bool _hasSuffix;

public:
    OneParamRewrite(const char* from, const char* to);
    bool match(AsyncWebServerRequest *request) override;
};

#endif // PROXCARD_ONEPARAM_REWRITE_H
