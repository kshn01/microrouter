#pragma once

inline bool isUsableWifiScanResult(int channel, bool hasSsid) {
    return hasSsid && channel >= 1 && channel <= 14;
}