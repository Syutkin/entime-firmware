#include "command_parser.h"
#include "command_router.h"
#include "handlers.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "CommandParser";

static void maskPasswords(JsonVariant value) {
  if (value.is<JsonObject>()) {
    for (JsonPair entry : value.as<JsonObject>()) {
      if (std::strcmp(entry.key().c_str(), "passwd") == 0)
        entry.value().set("***");
      else
        maskPasswords(entry.value());
    }
  } else if (value.is<JsonArray>()) {
    for (JsonVariant entry : value.as<JsonArray>())
      maskPasswords(entry);
  }
}

static String jsonForLog(const JsonDocument &request) {
  JsonDocument masked;
  if (!masked.set(request))
    return String("<payload omitted>");

  // Меняем только копию: обработчик должен получить исходный пароль.
  maskPasswords(masked.as<JsonVariant>());
  String result;
  serializeJson(masked, result);
  return result;
}

CommandParser::CommandParser(Stream &stream, const char *sourceName,
                             size_t bufferSize, SessionIdProvider sessionIdProvider)
    : _stream(&stream), _sourceName(sourceName), _bufferSize(bufferSize),
      _bufferPos(0), _discarding(false), _sessionIdProvider(sessionIdProvider),
      _sessionId(sessionIdProvider ? sessionIdProvider() : 0) {
  _buffer = (char *)malloc(_bufferSize);
  if (!_buffer) {
    ESP_LOGE(TAG, "[%s] Failed to allocate buffer of size %u", _sourceName,
             _bufferSize);
    _bufferSize = 0;
  } else {
    _buffer[0] = '\0';
    ESP_LOGI(TAG, "[%s] Parser initialized with buffer size %u", _sourceName,
             _bufferSize);
  }
}

CommandParser::~CommandParser() {
  if (_buffer) {
    free(_buffer);
    _buffer = nullptr;
  }
}

void CommandParser::update() {
  if (!_stream || !_buffer || _bufferSize == 0) {
    return;
  }

  resetIfSessionChanged();

  // Проверка таймаута незавершённого кадра
  if (_bufferPos > 0) {
    if (millis() - _frameStartMs > FRAME_TIMEOUT_MS) {
      ESP_LOGW(TAG, "[%s] Frame timeout, clearing buffer (len=%u)", _sourceName,
               (unsigned)_bufferPos);
      clearBuffer();
    }
  }

  if (_discarding) {
    if (millis() - _frameStartMs > FRAME_TIMEOUT_MS) {
      ESP_LOGW(TAG, "[%s] Discard timeout, resuming parser", _sourceName);
      _discarding = false;
      clearBuffer();
    }
    while (_discarding && _stream->available() > 0) {
      int c = _stream->read();
      if (resetIfSessionChanged())
        return;
      if (c < 0) {
        break;
      }
      if (c == '\n') {
        _discarding = false;
        clearBuffer();
        break;
      }
    }
    if (_discarding) {
      return;
    }
  }

  // Читаем доступные байты из потока
  while (_stream->available() > 0) {
    int c = _stream->read();
    if (resetIfSessionChanged())
      return;
    if (c < 0) {
      break;
    }

    // игнорируем CR
    if (c == '\r')
      continue;

    // если это первый байт кадра — старт таймера
    if (_bufferPos == 0) {
      _frameStartMs = millis();
    }

    if (_bufferPos >= _bufferSize - 1) {
      const bool terminatorSeen = (c == '\n');
      ESP_LOGW(TAG, "[%s] Frame too large%s (limit=%u)",
               _sourceName,
               terminatorSeen ? "" : ", discarding until newline",
               (unsigned)(_bufferSize - 1));
      JsonVariant nullId;
      sendError("", 104, "Frame too large", nullId, *_stream);
      clearBuffer();
      _discarding = !terminatorSeen;
      return;
    }

    // Сохраняем символ в буфер
    _buffer[_bufferPos++] = (char)c;
    _buffer[_bufferPos] = '\0';

    // Если встретили символ новой строки - это конец команды
    if (c == '\n') {
      // Сырой кадр может содержать пароль даже при невалидном JSON.
      ESP_LOGD(TAG, "[%s] Received command (length: %u)", _sourceName,
               (unsigned)_bufferPos);

      // Пытаемся парсить JSON
      processJson();
      // Очищаем буфер после обработки
      clearBuffer();
      return; // выходим из функции после обработки команды
    }
  }
}

void CommandParser::processJson() {
  if (resetIfSessionChanged())
    return;
  if (_bufferPos == 0) {
    return;
  }

  // Пропускаем пробелы и символы новой строки в начале и конце
  size_t startPos = 0;
  while (startPos < _bufferPos && isspace((unsigned char)_buffer[startPos])) {
    startPos++;
  }

  // Удаляем завершающие пробелы и символы новой строки
  size_t endPos = _bufferPos;
  while (endPos > startPos && isspace((unsigned char)_buffer[endPos - 1])) {
    endPos--;
  }

  if (startPos >= endPos) {
    return; // Только пробелы в буфере
  }

  // Пытаемся парсить только если начинается с '{' или '['
  if (_buffer[startPos] != '{' && _buffer[startPos] != '[') {
    return;
  }

  _buffer[endPos] = '\0';

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, _buffer + startPos);

  if (error) {
    // Невалидный JSON - логируем ошибку
    ESP_LOGW(TAG, "[%s] Invalid JSON: %s", _sourceName, error.c_str());
    JsonVariant nullId;
    sendError("", 101, "Invalid JSON", nullId, *_stream);
    return;
  }

  ESP_LOGI(TAG, "[%s] Received JSON: %s", _sourceName,
           jsonForLog(doc).c_str());
  
  // Маршрутизация команды к обработчику
  if (resetIfSessionChanged())
    return;
  CommandRouter::route(doc, *_stream);
}

bool CommandParser::resetIfSessionChanged() {
  if (!_sessionIdProvider)
    return false;
  const uint32_t sessionId = _sessionIdProvider();
  if (sessionId == _sessionId)
    return false;
  _sessionId = sessionId;
  clearBuffer();
  _discarding = false;
  _frameStartMs = 0;
  return true;
}

void CommandParser::clearBuffer() {
  _bufferPos = 0;
  if (_buffer) {
    _buffer[0] = '\0';
  }
}
