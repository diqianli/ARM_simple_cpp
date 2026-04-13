#pragma once

/// @file static_vector.hpp
/// @brief Small-buffer optimized vector: stores N elements inline, falls back to heap.
///        Drop-in replacement for SmallVec<[T; N]> / boost::small_vector.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace arm_cpu {

template<typename T, std::size_t N>
class StaticVector {
    static_assert(N > 0, "N must be > 0");

    union Storage {
        alignas(T) unsigned char inline_buf[sizeof(T) * N];
        T* heap_ptr;
    };

    Storage storage_;
    std::size_t size_ = 0;
    std::size_t capacity_ = N;
    bool on_heap_ = false;

    T* data_ptr() noexcept { return on_heap_ ? storage_.heap_ptr : reinterpret_cast<T*>(storage_.inline_buf); }
    const T* data_ptr() const noexcept { return on_heap_ ? storage_.heap_ptr : reinterpret_cast<const T*>(storage_.inline_buf); }

    void grow(std::size_t new_cap) {
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));
        T* old_data = data_ptr();
        for (std::size_t i = 0; i < size_; ++i) {
            new (new_data + i) T(std::move(old_data[i]));
            old_data[i].~T();
        }
        if (on_heap_) {
            ::operator delete(old_data);
        }
        storage_.heap_ptr = new_data;
        on_heap_ = true;
        capacity_ = new_cap;
    }

    void destroy_all() noexcept {
        T* d = data_ptr();
        for (std::size_t i = 0; i < size_; ++i) {
            d[i].~T();
        }
        if (on_heap_) {
            ::operator delete(d);
            on_heap_ = false;
        }
        size_ = 0;
    }

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    StaticVector() noexcept = default;

    StaticVector(std::initializer_list<T> init) {
        reserve(init.size());
        for (const auto& v : init) {
            push_back(v);
        }
    }

    StaticVector(const StaticVector& other) {
        reserve(other.size_);
        for (std::size_t i = 0; i < other.size_; ++i) {
            push_back(other.data_ptr()[i]);
        }
    }

    StaticVector(StaticVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (other.on_heap_) {
            storage_.heap_ptr = other.storage_.heap_ptr;
            on_heap_ = true;
            other.on_heap_ = false;
        } else {
            for (std::size_t i = 0; i < other.size_; ++i) {
                new (data_ptr() + i) T(std::move(other.data_ptr()[i]));
            }
        }
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.size_ = 0;
    }

    StaticVector& operator=(const StaticVector& other) {
        if (this != &other) {
            destroy_all();
            reserve(other.size_);
            for (std::size_t i = 0; i < other.size_; ++i) {
                push_back(other.data_ptr()[i]);
            }
        }
        return *this;
    }

    StaticVector& operator=(StaticVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            destroy_all();
            if (other.on_heap_) {
                storage_.heap_ptr = other.storage_.heap_ptr;
                on_heap_ = true;
                other.on_heap_ = false;
            } else {
                for (std::size_t i = 0; i < other.size_; ++i) {
                    new (data_ptr() + i) T(std::move(other.data_ptr()[i]));
                }
            }
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.size_ = 0;
        }
        return *this;
    }

    ~StaticVector() { destroy_all(); }

    void push_back(const T& value) {
        if (size_ >= capacity_) grow(capacity_ * 2);
        new (data_ptr() + size_) T(value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ >= capacity_) grow(capacity_ * 2);
        new (data_ptr() + size_) T(std::move(value));
        ++size_;
    }

    template<typename... Args>
    T& emplace_back(Args&&... args) {
        if (size_ >= capacity_) grow(capacity_ * 2);
        new (data_ptr() + size_) T(std::forward<Args>(args)...);
        return data_ptr()[size_++];
    }

    void pop_back() {
        --size_;
        data_ptr()[size_].~T();
    }

    void reserve(std::size_t new_cap) {
        if (new_cap > capacity_) grow(new_cap);
    }

    void clear() noexcept { destroy_all(); }

    bool empty() const noexcept { return size_ == 0; }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return capacity_; }

    T& operator[](std::size_t i) { return data_ptr()[i]; }
    const T& operator[](std::size_t i) const { return data_ptr()[i]; }

    T& front() { return data_ptr()[0]; }
    const T& front() const { return data_ptr()[0]; }
    T& back() { return data_ptr()[size_ - 1]; }
    const T& back() const { return data_ptr()[size_ - 1]; }

    T* data() noexcept { return data_ptr(); }
    const T* data() const noexcept { return data_ptr(); }

    iterator begin() noexcept { return data_ptr(); }
    iterator end() noexcept { return data_ptr() + size_; }
    const_iterator begin() const noexcept { return data_ptr(); }
    const_iterator end() const noexcept { return data_ptr() + size_; }
    const_iterator cbegin() const noexcept { return data_ptr(); }
    const_iterator cend() const noexcept { return data_ptr() + size_; }

    bool contains(const T& value) const {
        return std::find(begin(), end(), value) != end();
    }

    bool operator==(const StaticVector& other) const {
        if (size_ != other.size_) return false;
        for (std::size_t i = 0; i < size_; ++i) {
            if (!(data_ptr()[i] == other.data_ptr()[i])) return false;
        }
        return true;
    }

    bool operator!=(const StaticVector& other) const { return !(*this == other); }
};

} // namespace arm_cpu
