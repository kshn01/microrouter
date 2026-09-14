#pragma once

#include <Arduino.h>
#include "restriction_policy.h"

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
                                   uint32_t waiverRemainingSecs) {
    String badgeText = "Notice";
    String badgeColor = "#F59E0B";
    String badgeBg = "rgba(245, 158, 11, 0.15)";
    String badgeBorder = "rgba(245, 158, 11, 0.3)";
    String titleText = "Internet Access Paused";
    String descText = "Access is temporarily restricted on this network.";
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

    String displayName = hostname.length() > 0 ? hostname : (mac.length() > 0 ? mac : "Your Device");
    String usedStr = formatBytesPretty(dailyUsed);
    String limitStr = (dailyLimit > 0) ? formatBytesPretty(dailyLimit) : "Unlimited";
    int pct = 0;
    if (dailyLimit > 0) {
        pct = (int)((dailyUsed * 100ULL) / dailyLimit);
        if (pct > 100) pct = 100;
    }

    String html = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html += "<meta charset=\"utf-8\">\n";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no\">\n";
    html += "<title>MicroRouter — " + badgeText + "</title>\n";
    html += "<style>\n";
    html += "* { box-sizing: border-box; margin: 0; padding: 0; }\n";
    html += "body {\n";
    html += "  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n";
    html += "  background: radial-gradient(circle at top, #131b31 0%, #080c16 100%);\n";
    html += "  color: #F3F4F6;\n";
    html += "  min-height: 100vh;\n";
    html += "  display: flex;\n";
    html += "  align-items: center;\n";
    html += "  justify-content: center;\n";
    html += "  padding: 20px 16px;\n";
    html += "}\n";
    html += ".card {\n";
    html += "  width: 100%;\n";
    html += "  max-width: 440px;\n";
    html += "  background: rgba(17, 24, 39, 0.88);\n";
    html += "  border: 1px solid rgba(255, 255, 255, 0.08);\n";
    html += "  border-radius: 24px;\n";
    html += "  box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6), 0 0 40px rgba(99, 102, 241, 0.08);\n";
    html += "  padding: 32px 24px;\n";
    html += "  text-align: center;\n";
    html += "}\n";
    html += ".icon-ring {\n";
    html += "  width: 72px;\n";
    html += "  height: 72px;\n";
    html += "  margin: 0 auto 20px;\n";
    html += "  border-radius: 50%;\n";
    html += "  display: flex;\n";
    html += "  align-items: center;\n";
    html += "  justify-content: center;\n";
    html += "  background: " + badgeBg + ";\n";
    html += "  border: 1.5px solid " + badgeBorder + ";\n";
    html += "  box-shadow: 0 0 24px " + badgeBg + ";\n";
    html += "}\n";
    html += ".icon-ring svg { width: 34px; height: 34px; stroke: " + badgeColor + "; }\n";
    html += ".badge {\n";
    html += "  display: inline-block;\n";
    html += "  padding: 4px 12px;\n";
    html += "  border-radius: 9999px;\n";
    html += "  font-size: 11px;\n";
    html += "  font-weight: 700;\n";
    html += "  text-transform: uppercase;\n";
    html += "  letter-spacing: 0.08em;\n";
    html += "  color: " + badgeColor + ";\n";
    html += "  background: " + badgeBg + ";\n";
    html += "  border: 1px solid " + badgeBorder + ";\n";
    html += "  margin-bottom: 12px;\n";
    html += "}\n";
    html += "h1 { font-size: 21px; font-weight: 700; color: #FFFFFF; line-height: 1.3; margin-bottom: 8px; }\n";
    html += ".desc { font-size: 13.5px; color: #9CA3AF; line-height: 1.5; margin-bottom: 24px; }\n";
    html += ".stats-box {\n";
    html += "  background: rgba(31, 41, 55, 0.55);\n";
    html += "  border: 1px solid rgba(255, 255, 255, 0.06);\n";
    html += "  border-radius: 16px;\n";
    html += "  padding: 16px;\n";
    html += "  margin-bottom: 24px;\n";
    html += "  text-align: left;\n";
    html += "}\n";
    html += ".stat-row { display: flex; justify-content: space-between; font-size: 12.5px; margin-bottom: 8px; }\n";
    html += ".stat-label { color: #9CA3AF; }\n";
    html += ".stat-val { color: #E5E7EB; font-weight: 600; font-family: monospace; }\n";
    html += ".prog-track { width: 100%; height: 8px; background: rgba(255, 255, 255, 0.1); border-radius: 9999px; overflow: hidden; margin: 10px 0 6px; }\n";
    html += ".prog-bar { height: 100%; border-radius: 9999px; transition: width 0.4s ease; background: linear-gradient(90deg, #F59E0B, #EF4444); }\n";
    html += ".reset-note { font-size: 11.5px; color: #6B7280; text-align: center; margin-top: 8px; display: block; }\n";
    html += ".btn {\n";
    html += "  display: block;\n";
    html += "  width: 100%;\n";
    html += "  padding: 14px 20px;\n";
    html += "  border-radius: 14px;\n";
    html += "  font-size: 14.5px;\n";
    html += "  font-weight: 600;\n";
    html += "  border: none;\n";
    html += "  cursor: pointer;\n";
    html += "  transition: all 0.2s ease;\n";
    html += "  text-decoration: none;\n";
    html += "}\n";
    html += ".btn-primary {\n";
    html += "  background: linear-gradient(135deg, #3B82F6 0%, #6366F1 100%);\n";
    html += "  color: #FFFFFF;\n";
    html += "  box-shadow: 0 4px 16px rgba(99, 102, 241, 0.35);\n";
    html += "}\n";
    html += ".btn-primary:active { transform: scale(0.98); opacity: 0.9; }\n";
    html += ".btn-primary:disabled { opacity: 0.5; cursor: not-allowed; }\n";
    html += ".btn-sec {\n";
    html += "  background: rgba(255, 255, 255, 0.08);\n";
    html += "  color: #E5E7EB;\n";
    html += "  margin-top: 10px;\n";
    html += "  font-size: 13px;\n";
    html += "  padding: 11px 16px;\n";
    html += "}\n";
    html += ".countdown-box {\n";
    html += "  background: rgba(16, 185, 129, 0.12);\n";
    html += "  border: 1px solid rgba(16, 185, 129, 0.3);\n";
    html += "  border-radius: 14px;\n";
    html += "  padding: 14px;\n";
    html += "  margin-bottom: 16px;\n";
    html += "  display: none;\n";
    html += "}\n";
    html += ".countdown-val { font-size: 28px; font-weight: 800; color: #10B981; font-family: monospace; letter-spacing: 2px; }\n";
    html += ".countdown-lbl { font-size: 11px; color: #A7F3D0; text-transform: uppercase; letter-spacing: 0.05em; margin-top: 2px; }\n";
    html += ".footer-note { font-size: 11px; color: #4B5563; margin-top: 24px; }\n";
    html += "</style>\n</head>\n<body>\n";
    html += "<div class=\"card\">\n";

    // Icon
    html += "  <div class=\"icon-ring\">\n";
    if (waiverRemainingSecs > 0) {
        html += "    <svg fill=\"none\" viewBox=\"0 0 24 24\" stroke-width=\"2\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M9 12.75L11.25 15 15 9.75M21 12a9 9 0 11-18 0 9 9 0 0118 0z\"/></svg>\n";
    } else {
        html += "    <svg fill=\"none\" viewBox=\"0 0 24 24\" stroke-width=\"2\"><path stroke-linecap=\"round\" stroke-linejoin=\"round\" d=\"M12 9v3.75m9-.75a9 9 0 11-18 0 9 9 0 0118 0zm-9 3.75h.008v.008H12v-.008z\"/></svg>\n";
    }
    html += "  </div>\n";

    html += "  <div class=\"badge\" id=\"statusBadge\">" + badgeText + "</div>\n";
    html += "  <h1 id=\"statusTitle\">" + titleText + "</h1>\n";
    html += "  <p class=\"desc\" id=\"statusDesc\">" + descText + "</p>\n";

    // Stats
    html += "  <div class=\"stats-box\">\n";
    html += "    <div class=\"stat-row\"><span class=\"stat-label\">Device</span><span class=\"stat-val\">" + displayName + "</span></div>\n";
    html += "    <div class=\"stat-row\"><span class=\"stat-label\">IP Address</span><span class=\"stat-val\">" + ip + "</span></div>\n";
    if (dailyLimit > 0) {
        html += "    <div class=\"stat-row\"><span class=\"stat-label\">Daily Usage</span><span class=\"stat-val\">" + usedStr + " / " + limitStr + "</span></div>\n";
        html += "    <div class=\"prog-track\"><div class=\"prog-bar\" style=\"width: " + String(pct) + "%;\"></div></div>\n";
    }
    html += "    <span class=\"reset-note\">Limits reset automatically at 00:00 midnight</span>\n";
    html += "  </div>\n";

    // Countdown Box (when waiver granted or active)
    html += "  <div class=\"countdown-box\" id=\"countdownBox\">\n";
    html += "    <div class=\"countdown-val\" id=\"countdownTimer\">--:--</div>\n";
    html += "    <div class=\"countdown-lbl\">Extension Remaining • Browsing Active</div>\n";
    html += "  </div>\n";

    // Actions
    if (canRequestWaiver) {
        html += "  <button class=\"btn btn-primary\" id=\"waiverBtn\" onclick=\"requestExtension()\">Request 30-Minute Extension</button>\n";
        html += "  <a class=\"btn btn-sec\" id=\"resumeBtn\" style=\"display:none;\" href=\"http://captive.apple.com\" target=\"_blank\">Resume Browsing</a>\n";
    } else {
        html += "  <div style=\"font-size:12px; color:#EF4444; margin-top:8px;\">Self-service extension disabled for this device.</div>\n";
    }

    html += "  <div class=\"footer-note\">MicroRouter Intelligent Gateway • Parental & Bandwidth Shield</div>\n";
    html += "</div>\n";

    // Client-side script for instantaneous waiver request and live countdown
    html += "<script>\n";
    html += "var initialRemaining = " + String(waiverRemainingSecs) + ";\n";
    html += "var timerInterval = null;\n";
    html += "function startTimer(seconds) {\n";
    html += "  var rem = seconds;\n";
    html += "  var box = document.getElementById('countdownBox');\n";
    html += "  var disp = document.getElementById('countdownTimer');\n";
    html += "  var btn = document.getElementById('waiverBtn');\n";
    html += "  var resume = document.getElementById('resumeBtn');\n";
    html += "  if (box) box.style.display = 'block';\n";
    html += "  if (btn) btn.style.display = 'none';\n";
    html += "  if (resume) resume.style.display = 'block';\n";
    html += "  document.getElementById('statusBadge').innerText = 'Waiver Active';\n";
    html += "  document.getElementById('statusBadge').style.color = '#10B981';\n";
    html += "  document.getElementById('statusTitle').innerText = 'Emergency Extension Granted';\n";
    html += "  document.getElementById('statusDesc').innerText = 'Full internet access is now temporarily unlocked for your device.';\n";
    html += "  if (timerInterval) clearInterval(timerInterval);\n";
    html += "  timerInterval = setInterval(function() {\n";
    html += "    if (rem <= 0) {\n";
    html += "      clearInterval(timerInterval);\n";
    html += "      disp.innerText = '00:00';\n";
    html += "      setTimeout(function() { window.location.reload(); }, 2000);\n";
    html += "      return;\n";
    html += "    }\n";
    html += "    var m = Math.floor(rem / 60);\n";
    html += "    var s = rem % 60;\n";
    html += "    disp.innerText = (m < 10 ? '0' : '') + m + ':' + (s < 10 ? '0' : '') + s;\n";
    html += "    rem--;\n";
    html += "  }, 1000);\n";
    html += "}\n";
    html += "if (initialRemaining > 0) { startTimer(initialRemaining); }\n";
    html += "function requestExtension() {\n";
    html += "  var btn = document.getElementById('waiverBtn');\n";
    html += "  if (!btn) return;\n";
    html += "  btn.disabled = true;\n";
    html += "  btn.innerText = 'Unlocking Access...';\n";
    html += "  fetch('/api/device/request-waiver', { method: 'POST' })\n";
    html += "    .then(function(r) { return r.json(); })\n";
    html += "    .then(function(data) {\n";
    html += "      if (data.status === 'ok') {\n";
    html += "        startTimer(data.grantedSecs || 1800);\n";
    html += "      } else {\n";
    html += "        btn.disabled = false;\n";
    html += "        btn.innerText = 'Request 30-Minute Extension';\n";
    html += "        alert(data.message || 'Unable to grant extension');\n";
    html += "      }\n";
    html += "    })\n";
    html += "    .catch(function(err) {\n";
    html += "      btn.disabled = false;\n";
    html += "      btn.innerText = 'Request 30-Minute Extension';\n";
    html += "      alert('Network error requesting extension: ' + err);\n";
    html += "    });\n";
    html += "}\n";
    html += "</script>\n</body>\n</html>\n";

    return html;
}
