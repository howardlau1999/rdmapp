#pragma once

#include <algorithm>
#include <cstdint>
#include <netinet/in.h>
#include <type_traits>

namespace rdmapp {
namespace detail {

static inline uint16_t ntoh(uint16_t const &value) { return ::ntohs(value); }

static inline uint32_t ntoh(uint32_t const &value) { return ::ntohl(value); }

static inline uint16_t hton(uint16_t const &value) { return ::htons(value); }

static inline uint32_t hton(uint32_t const &value) { return ::htonl(value); }

template <class T, class It,
          class U = typename std::enable_if<std::is_integral<T>::value>::type>
void serialize(T const &value, It &it) {
  T nvalue = hton(value);
  std::copy_n(reinterpret_cast<uint8_t *>(&nvalue), sizeof(T), it);
}

template <class T, class It,
          class U = typename std::enable_if<std::is_integral<T>::value>::type>
void deserialize(It &it, T &value) {
  std::copy_n(it, sizeof(T), reinterpret_cast<uint8_t *>(&value));
  it += sizeof(T);
  value = ntoh(value);
}

} // namespace detail
} // namespace rdmapp