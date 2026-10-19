#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

struct CheckFailure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline void expect(bool condition, const std::string &message) {
  if (!condition)
    throw CheckFailure(message);
}

inline void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error("setup: " + message);
}

struct Test {
  const char *name;
  void (*run)();
  const char *known_issue = "-";
};

template <size_t N>
int run_tests(int argc, char **argv, const Test (&tests)[N]) {
  if (argc != 2)
    return 2;
  const std::string name = argv[1];
  if (name == "--list") {
    for (const auto &test : tests)
      std::cout << test.name << '\t' << test.known_issue << '\n';
    return 0;
  }
  for (const auto &test : tests) {
    if (name != test.name)
      continue;
    try {
      test.run();
      return 0;
    } catch (const CheckFailure &failure) {
      std::cerr << failure.what() << '\n';
      return 10;
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 2;
    }
  }
  std::cerr << "Unknown test: " << name << '\n';
  return 2;
}
