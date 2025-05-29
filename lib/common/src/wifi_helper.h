#pragma once
#ifndef _WIFI_HELPER_H
#define _WIFI_HELPER_H
#include <WiFi.h>
#include <Streaming.h>
#include "settings.h"
#include "bluetooth_helper.h"

void setupWiFi();

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

#endif
