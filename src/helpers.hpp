#pragma once
#include <cstdint>
#include <iostream>
#include <cstring>
#include <functional>
#include <map>

// replicate the behavior of itertools.product in python
class Product {
public:
    char* str_buffer;
    const char* end_str_buffer;
    size_t str_size;

public:
    Product() = default;
    Product(Product&&) = default;

    Product(const std::string& charset, const uint32_t repeat) {
        this->str_size = static_cast<size_t>(repeat) + 1;

        uint64_t arr_size = 1;
        if (!charset.empty()) {
            for (uint32_t i = 0; i < repeat; ++i) {
                arr_size *= charset.size();
            }
        }

        // each string needs repeat + null terminator bytes
        this->str_buffer = new char[arr_size * this->str_size];

        // buffer to generate strings to copy to str_buffer
        char cur_product[this->str_size];
        char* cur_buf_ptr = this->str_buffer;

        std::function<void(const uint32_t)> generate = [&](const uint32_t depth) {
            if (depth == repeat) {
                std::memcpy(cur_buf_ptr, cur_product, repeat);
                cur_buf_ptr[repeat] = 0; // have to make sure to null terminate
                cur_buf_ptr += this->str_size;
                return;
            }

            for (const char c : charset) {
                cur_product[depth] = c;
                generate(depth + 1);
            }
            };

        generate(0);
        this->end_str_buffer = cur_buf_ptr;
    }

    ~Product() {
        delete[] this->str_buffer;
        this->str_buffer = nullptr;
        this->end_str_buffer = nullptr;
        this->str_size = 0;
    }

    // basic c++ iterator implementation for special for loops
    class Iterator {
    private:
        char* ptr;
        const char* end;
        const size_t str_size;

    public:
        Iterator(char* start, const char* end, const size_t str_size)
            : ptr{ start }, end{ end }, str_size{ str_size }
        {}

        char* operator*() const {
            return ptr;
        }

        Iterator& operator++() {
            this->ptr += this->str_size;
            return *this;
        }

        Iterator operator++(int) {
            Iterator ret = *this;
            ret.ptr += ret.str_size;
            return ret;
        }

        bool operator!=(const Iterator& other) const {
            return this->ptr != other.ptr;
        }

        bool operator==(const Iterator& other) const {
            return this->ptr == other.ptr;
        }
    };

    Iterator begin() {
        return Iterator(this->str_buffer, this->end_str_buffer, this->str_size);
    }

    Iterator end() {
        return Iterator(const_cast<char*>(this->end_str_buffer), this->end_str_buffer, this->str_size);
    }
};

// manager for product objects to cache old results since odds are they'll be used again in the future
class ProductManager {
private:
    std::map<uint64_t /* hash */, Product> cache;

    uint64_t hash_arg(const std::string& charset, uint32_t repeat) {
        // just banking on there being no collisions (oh the irony)
        return std::hash<std::string>{}(charset) + repeat;
    }

    inline Product& get_empty_product() {
        static Product empty_product = Product("", 0);
        return empty_product;
    }

public:
    static ProductManager& singleton() {
        static ProductManager manager{};
        return manager;
    }

    Product& get(const std::string& charset, const uint32_t repeat) {
        if (repeat == 0) {
            return get_empty_product();
        }

        const uint64_t hashed_arg = hash_arg(charset, repeat);
        const auto charset_it = this->cache.find(hashed_arg);
        if (charset_it != cache.end()) {
            return charset_it->second;
        }

        // construct Product in place to prevent destructor from being called prematurely
        this->cache.emplace(std::piecewise_construct, std::make_tuple(hashed_arg), std::make_tuple(charset, repeat));

        return this->cache[hashed_arg];
    }
};

template <uint64_t num, uint64_t pow_mod>
class InverseHelper {
    // just to make sure
    static_assert(pow_mod <= 64, "The hard maximum on the pow_mod value is 64");

public:
    static constexpr uint64_t inverse();
};

template <uint64_t num>
class InverseHelper<num, 64> {
public:
    static constexpr uint64_t inverse();
};

#define __HELPER_TPP
#include "helpers.tpp"
#undef __HELPER_TPP