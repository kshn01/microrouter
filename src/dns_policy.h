#pragma once

#include <stddef.h>
#include <string.h>

inline bool domainMatches(const char* domain, const char* pattern) {
    if (!domain || !pattern) return false;
    if (strcasecmp(domain, pattern) == 0) return true;

    size_t domainLength = strlen(domain);
    size_t patternLength = strlen(pattern);
    return domainLength > patternLength + 1 &&
           domain[domainLength - patternLength - 1] == '.' &&
           strcasecmp(domain + domainLength - patternLength, pattern) == 0;
}

inline bool isLocalDnsDomain(const char* domain) {
    return domain && strcasecmp(domain, "microrouter.local") == 0;
}

inline bool isDoHCanaryDomain(const char* domain) {
    return domain && strcasecmp(domain, "use-application-dns.net") == 0;
}