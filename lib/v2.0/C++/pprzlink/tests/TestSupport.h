#pragma once

#include <pprzlink/Device.h>
#include <stdexcept>
#include <string>
#include <utility>

inline void require(bool condition, const std::string &description)
{
  if (!condition) throw std::runtime_error(description);
}

template<class Exception, class Callback>
std::string expectException(Callback callback)
{
  try { callback(); }
  catch (const Exception &error) { return error.what(); }
  throw std::runtime_error("Expected exception was not thrown");
}

class MemoryDevice : public pprzlink::Device {
public:
  pprzlink::BytesBuffer incoming, outgoing;
  size_t availableBytes() override { return incoming.size(); }
  pprzlink::BytesBuffer readAll() override { return std::exchange(incoming, {}); }
  void writeBuffer(const pprzlink::BytesBuffer &bytes) override { outgoing = bytes; }
};
