#pragma once
#include "timing/event_timestamp.h"
class MainArea { public: unsigned displayed=0; void displayEventTimestamp(const EventTimestampData &) { ++displayed; } };
