#pragma once

#include <Arduino.h>
#include "restriction_policy.h"

inline String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length() + 16);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

inline String formatBytesPretty(uint64_t bytes) {
    if (bytes >= (1ULL << 30)) {
        float gb = (float)bytes / (1ULL << 30);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f GB", gb);
        return String(buf);
    }
    if (bytes >= (1ULL << 20)) {
        float mb = (float)bytes / (1ULL << 20);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f MB", mb);
        return String(buf);
    }
    if (bytes >= 1024) {
        float kb = (float)bytes / 1024;
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f KB", kb);
        return String(buf);
    }
    return String((uint32_t)bytes) + " B";
}

inline String buildQuotaNoticeHtml(const String& ip,
                                   const String& mac,
                                   const String& hostname,
                                   ClientRestrictionReason reason,
                                   uint64_t dailyUsed,
                                   uint64_t dailyLimit,
                                   uint32_t waiverRemainingSecs,
                                   uint8_t waiversUsedToday = 0,
                                   uint8_t maxWaiversPerDay = 2) {
    const char* badgeText = "Notice";
    const char* badgeColor = "#F59E0B";
    const char* badgeBg = "rgba(245, 158, 11, 0.15)";
    const char* badgeBorder = "rgba(245, 158, 11, 0.35)";
    const char* titleText = "Internet Access Paused";
    const char* descText = "Access is temporarily restricted on this network.";
    bool canRequestWaiver = true;

    if (reason == RESTRICTION_QUOTA_DAILY) {
        badgeText = "Daily Quota Reached";
        badgeColor = "#F59E0B";
        badgeBg = "rgba(245, 158, 11, 0.15)";
        badgeBorder = "rgba(245, 158, 11, 0.35)";
        titleText = "Daily Data Allowance Reached";
        descText = "You have reached your allocated high-speed internet data for today. Traffic is paused to prevent overconsumption.";
    } else if (reason == RESTRICTION_QUOTA_HOURLY) {
        badgeText = "Hourly Limit Reached";
        badgeColor = "#F59E0B";
        badgeBg = "rgba(245, 158, 11, 0.15)";
        badgeBorder = "rgba(245, 158, 11, 0.35)";
        titleText = "Hourly Bandwidth Throttled";
        descText = "Your hourly bandwidth quota has been reached. Access will automatically resume at the start of the next hour.";
    } else if (reason == RESTRICTION_CURFEW) {
        badgeText = "Curfew Active";
        badgeColor = "#818CF8";
        badgeBg = "rgba(129, 140, 248, 0.15)";
        badgeBorder = "rgba(129, 140, 248, 0.35)";
        titleText = "Scheduled Wi-Fi Curfew";
        descText = "Internet access is currently paused according to the scheduled home network curfew window.";
    } else if (reason == RESTRICTION_ADMIN_BLOCK) {
        badgeText = "Restricted by Admin";
        badgeColor = "#EF4444";
        badgeBg = "rgba(239, 68, 68, 0.15)";
        badgeBorder = "rgba(239, 68, 68, 0.35)";
        titleText = "Device Access Blocked";
        descText = "This device has been administratively blocked. Please contact your network administrator.";
        canRequestWaiver = false;
    } else if (waiverRemainingSecs > 0) {
        badgeText = "Waiver Active";
        badgeColor = "#10B981";
        badgeBg = "rgba(16, 185, 129, 0.15)";
        badgeBorder = "rgba(16, 185, 129, 0.35)";
        titleText = "Temporary Extension Granted";
        descText = "You have an active emergency extension. Full internet access is currently restored.";
    }

    String rawName = hostname.length() > 0 ? hostname : (mac.length() > 0 ? mac : "Your Device");
    String safeDisplayName = htmlEscape(rawName);
    String safeIp = htmlEscape(ip);
    String usedStr = formatBytesPretty(dailyUsed);
    String limitStr = (dailyLimit > 0) ? formatBytesPretty(dailyLimit) : "Unlimited";
    int pct = 0;
    if (dailyLimit > 0) {
        pct = (int)((dailyUsed * 100ULL) / dailyLimit);
        if (pct > 100) pct = 100;
    }

    bool dailyCapReached = (waiversUsedToday >= maxWaiversPerDay);
    uint8_t waiversLeft = (maxWaiversPerDay > waiversUsedToday) ? (maxWaiversPerDay - waiversUsedToday) : 0;

    String html;
    html.reserve(8192);

    html += R"raw(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>MicroRouter — )raw";
    html += badgeText;
    html += R"raw(</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
  background: radial-gradient(circle at top, #131b31 0%, #080c16 100%);
  color: #F3F4F6;
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px 16px;
}
.card {
  width: 100%;
  max-width: 440px;
  background: rgba(17, 24, 39, 0.88);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 24px;
  box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6), 0 0 40px rgba(99, 102, 241, 0.08);
  padding: 32px 24px;
  text-align: center;
}
.icon-ring {
  width: 72px;
  height: 72px;
  margin: 0 auto 20px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
)raw";
    html += "  background: "; html += badgeBg; html += ";\n";
    html += "  border: 1.5px solid "; html += badgeBorder; html += ";\n";
    html += "  box-shadow: 0 0 24px "; html += badgeBg; html += ";\n";
    html += R"raw(}
.icon-ring svg { width: 34px; height: 34px; stroke: )raw";
    html += badgeColor;
    html += R"raw(; }
.badge {
  display: inline-block;
  padding: 4px 12px;
  border-radius: 9999px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.08em;
)raw";
    html += "  color: "; html += badgeColor; html += ";\n";
    html += "  background: "; html += badgeBg; html += ";\n";
    html += "  border: 1px solid "; html += badgeBorder; html += ";\n";
    html += R"raw(  margin-bottom: 12px;
}
h1 { font-size: 21px; font-weight: 700; color: #FFFFFF; line-height: 1.3; margin-bottom: 8px; }
.desc { font-size: 13.5px; color: #9CA3AF; line-height: 1.5; margin-bottom: 24px; }
.stats-box {
  background: rgba(31, 41, 55, 0.55);
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 16px;
  padding: 16px;
  margin-bottom: 20px;
  text-align: left;
}
.stat-row { display: flex; justify-content: space-between; font-size: 12.5px; margin-bottom: 8px; }
.stat-label { color: #9CA3AF; }
.stat-val { color: #E5E7EB; font-weight: 600; font-family: monospace; }
.prog-track { width: 100%; height: 8px; background: rgba(255, 255, 255, 0.1); border-radius: 9999px; overflow: hidden; margin: 10px 0 6px; }
.prog-bar { height: 100%; border-radius: 9999px; transition: width 0.4s ease; background: linear-gradient(90deg, #F59E0B, #EF4444); }
.reset-note { font-size: 11.5px; color: #6B7280; text-align: center; margin-top: 8px; display: block; }
.btn {
  display: block;
  width: 100%;
  padding: 14px 20px;
  border-radius: 14px;
  font-size: 14.5px;
  font-weight: 600;
  border: none;
  cursor: pointer;
  transition: all 0.2s ease;
  text-decoration: none;
  text-align: center;
}
.btn-primary {
  background: linear-gradient(135deg, #3B82F6 0%, #6366F1 100%);
  color: #FFFFFF;
  box-shadow: 0 4px 16px rgba(99, 102, 241, 0.35);
}
.btn-primary:active { transform: scale(0.98); opacity: 0.9; }
.btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }
.btn-sec {
  background: rgba(255, 255, 255, 0.08);
  color: #E5E7EB;
  margin-top: 10px;
  font-size: 13px;
  padding: 11px 16px;
}
.countdown-box {
  background: rgba(16, 185, 129, 0.12);
  border: 1px solid rgba(16, 185, 129, 0.3);
  border-radius: 14px;
  padding: 14px;
  margin-bottom: 16px;
  display: none;
}
.countdown-val { font-size: 28px; font-weight: 800; color: #10B981; font-family: monospace; letter-spacing: 2px; }
.countdown-lbl { font-size: 11px; color: #A7F3D0; text-transform: uppercase; letter-spacing: 0.05em; margin-top: 2px; }
.footer-note { font-size: 11px; color: #4B5563; margin-top: 24px; }
.sub-link { font-size: 12px; color: #818CF8; cursor: pointer; margin-top: 12px; display: inline-block; text-decoration: underline; }
</style>
</head>
<body>
<div class="card">
  <div class="icon-ring">
)raw";

    if (waiverRemainingSecs > 0) {
        html += R"raw(    <svg fill="none" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z"/></svg>)raw";
    } else {
        html += R"raw(    <svg fill="none" viewBox="0 0 24 24" stroke-width="2"><path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m9-.75a9 9 0 11-18 0 9 9 0 0118 0zm-9 3.75h.008v.008H12v-.008z"/></svg>)raw";
    }

    html += R"raw(
  </div>
  <div class="badge" id="statusBadge">)raw";
    html += badgeText;
    html += R"raw(</div>
  <h1 id="statusTitle">)raw";
    html += titleText;
    html += R"raw(</h1>
  <p class="desc" id="statusDesc">)raw";
    html += descText;
    html += R"raw(</p>

  <div class="stats-box">
    <div class="stat-row"><span class="stat-label">Device</span><span class="stat-val">)raw";
    html += safeDisplayName;
    html += R"raw(</span></div>
    <div class="stat-row"><span class="stat-label">IP Address</span><span class="stat-val">)raw";
    html += safeIp;
    html += R"raw(</span></div>
)raw";

    if (dailyLimit > 0) {
        html += "    <div class=\"stat-row\"><span class=\"stat-label\">Daily Usage</span><span class=\"stat-val\">" + usedStr + " / " + limitStr + "</span></div>\n";
        html += "    <div class=\"prog-track\"><div class=\"prog-bar\" style=\"width: " + String(pct) + "%;\"></div></div>\n";
    }

    html += R"raw(    <span class="reset-note">Limits reset automatically at 00:00 midnight</span>
  </div>

  <div class="countdown-box" id="countdownBox">
    <div class="countdown-val" id="countdownTimer">--:--</div>
    <div class="countdown-lbl">Extension Remaining • Browsing Active</div>
  </div>
)raw";

    // Actions
    if (canRequestWaiver) {
        if (!dailyCapReached) {
            html += "  <button class=\"btn btn-primary\" id=\"waiverBtn\" onclick=\"requestExtension()\">Request 30-Minute Extension (" + String(waiversLeft) + " left today)</button>\n";
            html += "  <a class=\"btn btn-sec\" id=\"resumeBtn\" style=\"display:none;\" href=\"/hotspot-detect.html\">Resume Browsing</a>\n";
        } else {
            html += "  <div style=\"font-size:12.5px; color:#EF4444; background:rgba(239,68,68,0.1); border:1px solid rgba(239,68,68,0.25); border-radius:12px; padding:12px; margin-bottom:8px;\">Daily extension limit reached (" + String(maxWaiversPerDay) + "/" + String(maxWaiversPerDay) + " used). Please contact your network administrator.</div>\n";
            html += "  <a class=\"btn btn-sec\" id=\"resumeBtn\" style=\"display:none;\" href=\"/hotspot-detect.html\">Resume Browsing</a>\n";
        }
    } else {
        html += "  <div style=\"font-size:12px; color:#EF4444; margin-top:8px;\">Self-service extension disabled for this device.</div>\n";
    }

    html += R"raw(
  <div class="footer-note">MicroRouter Intelligent Gateway • Parental & Bandwidth Shield</div>
</div>

<script>
var initialRemaining = )raw";
    html += String(waiverRemainingSecs);
    html += R"raw(;
var timerInterval = null;

function startTimer(seconds) {
  var rem = seconds;
  var box = document.getElementById('countdownBox');
  var disp = document.getElementById('countdownTimer');
  var btn = document.getElementById('waiverBtn');
  var resume = document.getElementById('resumeBtn');
  if (box) box.style.display = 'block';
  if (btn) btn.style.display = 'none';
  if (resume) resume.style.display = 'block';
  document.getElementById('statusBadge').innerText = 'Waiver Active';
  document.getElementById('statusBadge').style.color = '#10B981';
  document.getElementById('statusTitle').innerText = 'Emergency Extension Granted';
  document.getElementById('statusDesc').innerText = 'Full internet access is now temporarily unlocked for your device.';
  if (timerInterval) clearInterval(timerInterval);
  timerInterval = setInterval(function() {
    if (rem <= 0) {
      clearInterval(timerInterval);
      disp.innerText = '00:00';
      setTimeout(function() { window.location.reload(); }, 2000);
      return;
    }
    var m = Math.floor(rem / 60);
    var s = rem % 60;
    disp.innerText = (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;
    rem--;
  }, 1000);
}

if (initialRemaining > 0) { startTimer(initialRemaining); }

function requestExtension() {
  var btn = document.getElementById('waiverBtn');
  if (btn) {
    btn.disabled = true;
    btn.innerText = 'Unlocking Access...';
  }

  fetch('/api/device/request-waiver', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({})
  })
  .then(function(r) { return r.json(); })
  .then(function(data) {
    if (data.status === 'ok') {
      startTimer(data.grantedSecs || 1800);
    } else {
      if (btn) {
        btn.disabled = false;
        btn.innerText = 'Request 30-Minute Extension';
      }
      alert(data.message || 'Unable to grant extension');
    }
  })
  .catch(function(err) {
    if (btn) {
      btn.disabled = false;
      btn.innerText = 'Request 30-Minute Extension';
    }
    alert('Network error requesting extension: ' + err);
  });
}
</script>
</body>
</html>
)raw";

    return html;
}
