#pragma once
#include <deque>
#include <cstdint>
#include <cstddef>
struct HostStreamBuffer { size_t capacity; std::deque<uint8_t> bytes; };
using StreamBufferHandle_t=HostStreamBuffer *;
inline StreamBufferHandle_t xStreamBufferCreate(size_t size,unsigned) { return new HostStreamBuffer{size,{}}; }
inline void vStreamBufferDelete(StreamBufferHandle_t stream) { delete stream; }
inline size_t xStreamBufferBytesAvailable(StreamBufferHandle_t stream) { return stream->bytes.size(); }
inline size_t xStreamBufferReceive(StreamBufferHandle_t s,void *out,size_t len,unsigned) {
  size_t n=0; auto *buf=static_cast<uint8_t *>(out);
  while(n<len&&!s->bytes.empty()) { buf[n++]=s->bytes.front();s->bytes.pop_front(); }
  return n;
}
inline size_t xStreamBufferSend(StreamBufferHandle_t s,const void *data,size_t len,unsigned) {
  size_t n=0; const auto *buf=static_cast<const uint8_t *>(data);
  while(n<len&&s->bytes.size()<s->capacity) s->bytes.push_back(buf[n++]);
  return n;
}
