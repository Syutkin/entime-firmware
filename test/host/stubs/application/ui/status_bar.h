#pragma once
#include <ctime>
#include <vector>
class StatusBar {
public:
  std::vector<std::tm> times;
  void updateTime(const std::tm &value) { times.push_back(value); }
};
