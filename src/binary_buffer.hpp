
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>


namespace generic {
    class BinaryBuffer;

    template <typename T> struct BinaryDataInterface {
        static void   write(T&&, BinaryBuffer&)          = delete;
        static T      read(BinaryBuffer&)                = delete;
        static size_t size(const std::optional<T>& data) = delete;
    };

    template <typename T>
    concept BinaryData = requires(BinaryBuffer& buffer) {
        { BinaryDataInterface<T>::write(std::declval<T>(), buffer) } -> std::same_as<void>;
        { BinaryDataInterface<T>::read(buffer) } -> std::same_as<T>;
        { BinaryDataInterface<T>::size(std::declval<T>()) } -> std::same_as<size_t>;
    };

    class BinaryBuffer {
    public:
        explicit BinaryBuffer(std::span<uint8_t> buffer) : bytes(buffer) {};

        template <BinaryData T> T read_next() {
            this->bounds_check(BinaryDataInterface<T>::size(std::nullopt));

            return BinaryDataInterface<T>::read(*this);
        }

        template <BinaryData T> T read_next_peak() {
            this->bounds_check(BinaryDataInterface<T>::size(std::nullopt));

            const auto current_idx = this->index;

            const auto return_val = BinaryDataInterface<T>::read(*this);
            ;
            this->index = current_idx;
            return return_val;
        }

        template <BinaryData T> void write(T&& val) {
            this->bounds_check(BinaryDataInterface<T>::size(val));

            BinaryDataInterface<T>::write(std::forward<T>(val), *this);
        }

        uint8_t next_byte() {
            this->bounds_check(1);
            return this->bytes[this->index++];
        }

        void write_byte(uint8_t val) {
            this->bounds_check(1);
            this->bytes[this->index++] = val;
        }

        uint8_t* raw() const { return this->bytes.data(); }

        size_t consumed() const { return this->index; }

        size_t clear() {
            auto index  = this->index;
            this->index = 0;
            return index;
        }

        std::span<uint8_t> underlying() { return this->bytes; }

        size_t size() const noexcept {
            return this->bytes.size();
        }

    private:
        [[maybe_unused]] bool bounds_check(size_t extra) {
            if (this->index + extra - 1 >= bytes.size()) {
                throw std::out_of_range("Not enough space in type");
            }
            return true;
        }

    private:
        std::span<uint8_t> bytes{};
        size_t             index{0};
    };
} // namespace generic

#define INTEGRAL_BINARY(type)                                                                  \
    namespace generic {                                                                        \
        template <> struct BinaryDataInterface<type> {                                         \
            static void write(const type& val, BinaryBuffer& buffer) {                         \
                auto bytes =                                                                   \
                    std::bit_cast<std::array<uint8_t, sizeof(type)>>(std::byteswap(val));      \
                for (const auto byte : bytes) {                                                \
                    buffer.write_byte(byte);                                                   \
                };                                                                             \
            }                                                                                  \
                                                                                               \
            static type read(BinaryBuffer& buffer) {                                           \
                type value{};                                                                  \
                for (auto x = 0; x < sizeof(type); x++)                                        \
                    value |= (buffer.next_byte() << x * 8);                                    \
                return std::byteswap(value);                                                   \
            }                                                                                  \
                                                                                               \
            static size_t size(const std::optional<type>& /*unused*/) { return sizeof(type); } \
        };                                                                                     \
    }

INTEGRAL_BINARY(uint16_t);
INTEGRAL_BINARY(int16_t);
INTEGRAL_BINARY(uint32_t);
INTEGRAL_BINARY(int32_t);
INTEGRAL_BINARY(uint64_t);
INTEGRAL_BINARY(int64_t);
INTEGRAL_BINARY(uint8_t);
INTEGRAL_BINARY(int8_t);

namespace generic {
    template <> struct BinaryDataInterface<std::string> {
        static void write(const std::string& val, BinaryBuffer& buffer) {
            for (const auto val : val) {
                buffer.write_byte(val);
            }

            buffer.write_byte(0);
        }

        static std::string read(BinaryBuffer& buffer) {
            std::string out;
            out.reserve(32);

            for (auto val = buffer.next_byte(); val != 0; val = buffer.next_byte()) {
                out.push_back(val);
            }

            return out;
        }

        static size_t size(const std::optional<std::string>& string) {
            if (string.has_value()) {
                return string.value().size() + 1;
            } else {
                return 1; // this is a guess
            }
        }
    };
} // namespace generic

#undef INTEGRAL_BINARY