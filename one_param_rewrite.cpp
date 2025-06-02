#include "one_param_rewrite.h"

OneParamRewrite::OneParamRewrite(const char* from, const char* to)
    : AsyncWebRewrite(from, to) {
    // Find the parameter placeholder in the from URL
    _paramStart = _from.indexOf('{');
    _paramEnd = _from.indexOf('}', _paramStart);
    
    if (_paramStart >= 0 && _paramEnd > _paramStart) {
        // Extract the prefix (everything before the parameter)
        _urlPrefix = _from.substring(0, _paramStart);
        
        // Check if there's anything after the parameter
        if (_paramEnd < _from.length() - 1) {
            _urlSuffix = _from.substring(_paramEnd + 1);
            _hasSuffix = true;
        } else {
            _hasSuffix = false;
        }

        // Store the base params (everything before the parameter in the target URL)
        int targetParamStart = _params.indexOf('{');
        if (targetParamStart >= 0) {
            _paramsBackup = _params.substring(0, targetParamStart);
        } else {
            _paramsBackup = _params;
        }
    } else {
        // No parameter found, treat as exact match
        _urlPrefix = _from;
        _hasSuffix = false;
        _paramsBackup = _params;
    }
}

bool OneParamRewrite::match(AsyncWebServerRequest *request) {
    String url = request->url();
    
    // Check if URL starts with our prefix
    if (!url.startsWith(_urlPrefix)) {
        return false;
    }

    // If we have a suffix, check if the URL ends with it
    if (_hasSuffix) {
        // Extract the parameter value
        String paramValue = url.substring(_urlPrefix.length());
        int suffixPos = paramValue.indexOf(_urlSuffix);
        if (suffixPos < 0) {
            return false;
        }
        paramValue = paramValue.substring(0, suffixPos);
        
        // Reconstruct the target URL with the parameter
        _params = _paramsBackup + paramValue + _urlSuffix;
    } else {
        // Parameter is at the end, just take everything after the prefix
        String paramValue = url.substring(_urlPrefix.length());
        _params = _paramsBackup + paramValue;
    }
    
    return true;
}