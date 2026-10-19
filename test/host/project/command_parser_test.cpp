#include "support.h"
#include "command/command_parser.h"
#include "command/command_router.h"
#include "hal/ble/nus_service.h"
#include <vector>

static std::vector<std::string> commands;
static std::vector<int> errors;
// The route is the observable output of the framing/parser under test.
void CommandRouter::route(JsonDocument &doc, Stream &) {
  commands.push_back(doc["cmd"].as<std::string>());
}
void sendError(const char *, int code, const char *, const JsonVariant &, Stream &) {
  errors.push_back(code);
}

namespace {
constexpr size_t kFrameSize = 32;
const std::string ping = "{\"cmd\":\"ping\"}\n";
void overflow(CommandParser &parser, host_project::MemoryStream &stream) {
  stream.feed(std::string(kFrameSize, 'x'));
  parser.update();
  require(errors == std::vector<int>{104}, "oversized frame must be detected");
}

template <bool Rollover> void discard_timeout_preserves_next_command() {
  if constexpr (Rollover) host_project::now_ms = UINT32_MAX - 100;
  host_project::MemoryStream stream;
  CommandParser parser(stream, "audit", kFrameSize);
  overflow(parser, stream);
  host_project::now_ms += 1500;
  stream.feed(ping); parser.update();
  expect(commands == std::vector<std::string>{"ping"},
         "AU008: the first valid command after discard timeout must be routed");
}

void newline_recovers_oversized_frame() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "audit", kFrameSize);
  overflow(parser, stream);
  stream.feed("discarded tail\n" + ping); parser.update();
  expect(commands == std::vector<std::string>{"ping"},
         "newline must end oversized frame and preserve next command");
}

void normal_partial_timeout_recovers() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "audit", kFrameSize);
  stream.feed("{\"cmd\":"); parser.update();
  host_project::now_ms += 1500;
  stream.feed(ping); parser.update();
  expect(commands == std::vector<std::string>{"ping"},
         "ordinary partial-frame timeout must preserve the next frame");
}

void fragmented_command_routes_once() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "audit", kFrameSize);
  stream.feed("{\"cmd\":"); parser.update();
  require(commands.empty(), "incomplete frame cannot be routed");
  stream.feed("\"ping\"}\r\n"); parser.update();
  expect(commands == std::vector<std::string>{"ping"}, "fragments must reassemble");
}

template <bool Peeked> void reconnect_clears_old_rx_bytes() {
  NimBLEServer server;
  require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client; nusService.onConnect(client, 23);
  const std::string old = "{\"cmd\":\"old\"";
  nusService.onReceive(reinterpret_cast<const uint8_t *>(old.data()), old.size());
  if constexpr (Peeked) require(nusService.peek() == '{', "peek must cache first byte");
  nusService.onDisconnect(0); nusService.onConnect(client, 23);
  expect(nusService.available() == 0 && nusService.peek() == -1 && nusService.read() == -1,
         "AU005: a new BLE session must start with an empty RX stream and peek cache");
}

void reconnect_does_not_mix_commands() {
  NimBLEServer server;
  require(nusService.init(&server), "NUS must initialize");
  NimBLEConnInfo client; nusService.onConnect(client, 23);
  CommandParser parser(nusService, "BLE", kFrameSize);
  const std::string old = "{\"cmd\":\"old\"";
  nusService.onReceive(reinterpret_cast<const uint8_t *>(old.data()), old.size());
  nusService.onDisconnect(0); nusService.onConnect(client, 23);
  nusService.onReceive(reinterpret_cast<const uint8_t *>(ping.data()), ping.size());
  parser.update();
  expect(commands == std::vector<std::string>{"ping"},
         "AU005: next client's command must not concatenate with previous client's bytes");
}

const Test kTests[] = {
  {"parser_discard_timeout_preserves_command", discard_timeout_preserves_next_command<false>, "AU008"},
  {"parser_discard_timeout_millis_rollover", discard_timeout_preserves_next_command<true>, "AU008"},
  {"parser_oversized_newline_recovery_control", newline_recovers_oversized_frame},
  {"parser_partial_timeout_recovery_control", normal_partial_timeout_recovers},
  {"parser_fragmented_command_control", fragmented_command_routes_once},
  {"ble_reconnect_clears_rx", reconnect_clears_old_rx_bytes<false>, "AU005"},
  {"ble_reconnect_clears_peek", reconnect_clears_old_rx_bytes<true>, "AU005"},
  {"ble_reconnect_command_isolation", reconnect_does_not_mix_commands, "AU005"},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
