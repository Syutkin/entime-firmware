#include "../support/application/environment.h"
#include "command/command_parser.h"
#include "command/command_router.h"
#include "hal/ble/nus_service.h"
#include <vector>

static std::vector<std::string> commands;
static std::vector<int> errors;
static std::vector<std::string> routed_payloads;
// The route is the observable output of the framing/parser under test.
void CommandRouter::route(JsonDocument &doc, Stream &) {
  commands.push_back(doc["cmd"].as<std::string>());
  std::string payload;
  serializeJson(doc, payload);
  routed_payloads.push_back(payload);
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

std::string captured_logs() {
  std::string result;
  for (const auto &line : host_project::logs) result += line + '\n';
  return result;
}

template <bool Nested, bool SerialSource> void log_masks_wifi_password() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, SerialSource ? "Serial" : "BLE");
  const char *request = Nested
      ? R"({"cmd":"save_config","data":{"wifi":{"ssid":"RaceNetwork","passwd":"private-wifi-value"}}})"
      : R"({"cmd":"wifi","enable":true,"ssid":"RaceNetwork","passwd":"private-wifi-value"})";
  stream.feed(std::string(request) + '\n'); parser.update();
  require(routed_payloads.size() == 1, "valid command must reach its handler");
  JsonDocument routed;
  require(!deserializeJson(routed, routed_payloads.front()), "routed JSON must remain valid");
  const std::string password = Nested
      ? routed["data"]["wifi"]["passwd"].as<std::string>()
      : routed["passwd"].as<std::string>();
  expect(password == "private-wifi-value",
         "masking logs must not change the password sent to the handler");
  const auto log = captured_logs();
  expect(log.find("private-wifi-value") == std::string::npos,
         "password must not occur in any log level");
  expect(log.find(R"("passwd":"***")") != std::string::npos &&
             log.find("RaceNetwork") != std::string::npos,
         "log must retain command details with the password replaced by ***");
}

void log_masks_escaped_password_and_key() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "BLE");
  stream.feed(R"({"cmd":"wifi","pa\u0073swd":"hidden\"quote\\slash"})" "\n");
  parser.update();
  require(routed_payloads.size() == 1, "escaped JSON must still be routed");
  JsonDocument routed;
  require(!deserializeJson(routed, routed_payloads.front()), "routed JSON must be valid");
  expect(routed["passwd"] == "hidden\"quote\\slash", "handler must receive decoded original password");
  const auto log = captured_logs();
  expect(log.find("hidden") == std::string::npos &&
             log.find(R"("passwd":"***")") != std::string::npos,
         "escaped field names and password characters must not bypass masking");
}

void log_masks_fragmented_password() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "BLE");
  stream.feed(R"({"cmd":"wifi","passwd":"fragmented-)"); parser.update();
  require(routed_payloads.empty(), "first fragment must not be routed");
  stream.feed("private-value\"}\n"); parser.update();
  require(routed_payloads.size() == 1, "complete fragmented request must be routed");
  expect(routed_payloads.front().find("fragmented-private-value") != std::string::npos,
         "handler must receive complete original password");
  expect(captured_logs().find("fragmented-private-value") == std::string::npos &&
             captured_logs().find(R"("passwd":"***")") != std::string::npos,
         "fragmented passwords must be masked in logs");
}

void log_omits_malformed_password_payload() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "Serial");
  stream.feed("{\"cmd\":\"wifi\",\"passwd\":\"malformed-private-value\"\n");
  parser.update();
  require(errors == std::vector<int>{101} && routed_payloads.empty(),
          "malformed JSON must return error without routing");
  expect(captured_logs().find("malformed-private-value") == std::string::npos,
         "invalid-JSON diagnostics must not dump raw password bytes");
}

void log_omits_non_json_payload() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "Serial");
  stream.feed("passwd=non-json-private-value\n"); parser.update();
  require(routed_payloads.empty(), "non-JSON input must not be routed");
  expect(captured_logs().find("non-json-private-value") == std::string::npos,
         "raw command diagnostics must not expose non-JSON input");
}

void log_masks_passwords_in_arrays() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "BLE");
  stream.feed(R"({"cmd":"save_config","extra":[{"passwd":"array-private-value"},{"wifi":{"passwd":"nested-private-value"}}]})" "\n");
  parser.update();
  require(routed_payloads.size() == 1, "command with extra fields must be routed");
  const auto log = captured_logs();
  expect(log.find("array-private-value") == std::string::npos &&
             log.find("nested-private-value") == std::string::npos &&
             log.find(R"("passwd":"***")") != std::string::npos,
         "all password fields in nested containers must be masked");
}

void log_masks_empty_password() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "BLE");
  stream.feed("{\"cmd\":\"wifi\",\"passwd\":\"\"}\n"); parser.update();
  require(routed_payloads.size() == 1, "empty password command must be routed");
  expect(captured_logs().find(R"("passwd":"***")") != std::string::npos,
         "empty password fields must use the same mask");
}

void log_preserves_non_secret_fields() {
  host_project::MemoryStream stream;
  CommandParser parser(stream, "Serial");
  stream.feed(R"({"cmd":"ping","message":"passwd is a field name","id":17})" "\n");
  parser.update();
  require(commands == std::vector<std::string>{"ping"}, "ordinary command must route");
  const auto log = captured_logs();
  expect(log.find("passwd is a field name") != std::string::npos &&
             log.find(R"("id":17)") != std::string::npos,
         "logging must preserve unrelated strings and command fields");
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
  {"parser_log_masks_wifi_ble", log_masks_wifi_password<false, false>},
  {"parser_log_masks_wifi_serial", log_masks_wifi_password<false, true>},
  {"parser_log_masks_save_config_ble", log_masks_wifi_password<true, false>},
  {"parser_log_masks_save_config_serial", log_masks_wifi_password<true, true>},
  {"parser_log_masks_escaped_password_and_key", log_masks_escaped_password_and_key},
  {"parser_log_masks_fragmented_password", log_masks_fragmented_password},
  {"parser_log_omits_malformed_password_payload", log_omits_malformed_password_payload},
  {"parser_log_omits_non_json_payload", log_omits_non_json_payload},
  {"parser_log_masks_passwords_in_arrays", log_masks_passwords_in_arrays},
  {"parser_log_masks_empty_password", log_masks_empty_password},
  {"parser_log_preserves_non_secret_fields", log_preserves_non_secret_fields},
};
} // namespace
int main(int argc, char **argv) { return run_tests(argc, argv, kTests); }
