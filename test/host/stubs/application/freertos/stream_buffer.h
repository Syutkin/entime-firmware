#pragma once
#include <deque>
#include <cstdint>
#include <cstddef>
#include <functional>
namespace host_project { inline std::function<void()> after_rx_read; }
struct HostStreamBuffer { size_t capacity; std::deque<uint8_t> bytes; };
using StreamBufferHandle_t=HostStreamBuffer *;
inline StreamBufferHandle_t xStreamBufferCreate(size_t size,unsigned) { return new HostStreamBuffer{size,{}}; }
inline void vStreamBufferDelete(StreamBufferHandle_t stream) { delete stream; }
inline int xStreamBufferReset(StreamBufferHandle_t stream) { stream->bytes.clear(); return 1; }
inline size_t xStreamBufferBytesAvailable(StreamBufferHandle_t stream) { return stream->bytes.size(); }
inline size_t xStreamBufferReceive(StreamBufferHandle_t s,void *out,size_t len,unsigned) {
  size_t n=0; auto *buf=static_cast<uint8_t *>(out);
  while(n<len&&!s->bytes.empty()) { buf[n++]=s->bytes.front();s->bytes.pop_front(); }
  const auto callback = host_project::after_rx_read;
  if (n > 0 && callback) callback();
  return n;
}
inline size_t xStreamBufferSend(StreamBufferHandle_t s,const void *data,size_t len,unsigned) {
  size_t n=0; const auto *buf=static_cast<const uint8_t *>(data);
  while(n<len&&s->bytes.size()<s->capacity) s->bytes.push_back(buf[n++]);
  return n;
}
