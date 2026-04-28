#pragma once
#include "position.h"
#include "search.h"
#include <string>

// ============================================================
// UCI protocol handler
// ============================================================

namespace UCI {

// Parse and execute UCI commands in a loop (blocking)
void loop();

// Parse "position" command and update pos
void parsePosition(const std::string& line, Position& pos, StateInfo* states, int& stateIdx);

// Parse "go" command and return limits
SearchLimits parseGo(const std::string& line);

// Print engine info
void printId();

} // namespace UCI
