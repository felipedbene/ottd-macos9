// M1: stub the NewGRF profiler. date.cpp's OnNewDay checks
// !_newgrf_profilers.empty() (default-empty -> dead branch) and may call
// NewGRFProfiler::FinishAll(). No NewGRFs are loaded headless.
#include "stdafx.h"
#include "newgrf_profiling.h"

std::vector<NewGRFProfiler> _newgrf_profilers;
Date _newgrf_profile_end_date;
uint32 NewGRFProfiler::FinishAll() { return 0; }
NewGRFProfiler::~NewGRFProfiler() {}   // vector<NewGRFProfiler> dtor refs it; vector is always empty
