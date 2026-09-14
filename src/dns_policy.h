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

inline bool isEncryptedDnsEndpoint(const char* domain) {
    if (!domain) return false;
    return domainMatches(domain, "dns.google") ||
           domainMatches(domain, "cloudflare-dns.com") ||
           domainMatches(domain, "one.one.one.one") ||
           domainMatches(domain, "dns.quad9.net") ||
           domainMatches(domain, "dns.adguard-dns.com") ||
           domainMatches(domain, "dns.nextdns.io");
}

inline bool isCaptiveProbeDomain(const char* domain) {
    if (!domain) return false;
    return domainMatches(domain, "captive.apple.com") ||
           domainMatches(domain, "connectivitycheck.gstatic.com") ||
           domainMatches(domain, "connectivitycheck.android.com") ||
           domainMatches(domain, "clients3.google.com") ||
           domainMatches(domain, "www.msftconnecttest.com") ||
           domainMatches(domain, "msftconnecttest.com") ||
           domainMatches(domain, "www.msftncsi.com") ||
           domainMatches(domain, "detectportal.firefox.com");
}