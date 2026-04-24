#pragma once
#include "config.h"

// launches the interactive TUI
// blocks until user presses q
// returns 0 on clean exit
int run_tui(const Config& cfg);