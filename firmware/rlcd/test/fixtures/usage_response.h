#pragma once
#include <string>

inline std::string response() {
  return R"({"schema":"rcink.usage-limits.v2","servedAt":1794297900,"providers":[
    {"id":"claude","status":"ok","usedPercent":33.75,"remainingPercent":66.25,"fetchedAt":1794297600,"expiresAt":1794298200,"resetsAt":1794632400},
    {"id":"codex","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null},
    {"id":"kimi","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null},
    {"id":"gemini","status":"unavailable","usedPercent":null,"remainingPercent":null,"fetchedAt":null,"expiresAt":null,"resetsAt":null}]})";
}
// The payload mirrors the source's accounts: three rows, no fixed slots.
inline std::string three_response() {
  return R"({"schema":"rcink.usage-limits.v2","servedAt":1794298000,"providers":[
    {"id":"codex","status":"ok","usedPercent":41.5,"remainingPercent":58.5,"fetchedAt":1794297800,"expiresAt":1794298400,"resetsAt":1794298100},
    {"id":"kimi","status":"ok","usedPercent":0,"remainingPercent":100,"fetchedAt":1794297720,"expiresAt":1794298800,"resetsAt":null},
    {"id":"gemini","status":"ok","usedPercent":75,"remainingPercent":25,"fetchedAt":1794297780,"expiresAt":1794298380,"resetsAt":1794735000}]})";
}
inline std::string four_response() {
  return R"({"schema":"rcink.usage-limits.v2","servedAt":1794297900,"providers":[
    {"id":"claude","status":"ok","usedPercent":33.75,"remainingPercent":66.25,"fetchedAt":1794297600,"expiresAt":1794298200,"resetsAt":1794632400},
    {"id":"codex","status":"ok","usedPercent":41.5,"remainingPercent":58.5,"fetchedAt":1794297660,"expiresAt":1794298260,"resetsAt":1794298000},
    {"id":"kimi","status":"ok","usedPercent":0,"remainingPercent":100,"fetchedAt":1794297720,"expiresAt":1794298800,"resetsAt":null},
    {"id":"gemini","status":"ok","usedPercent":75,"remainingPercent":25,"fetchedAt":1794297780,"expiresAt":1794298380,"resetsAt":1794735000}]})";
}
