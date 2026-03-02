#pragma once

#include <string>
#include <utility>

namespace vsfclone::core {

template <typename T>
struct Result {
    bool ok = false;
    T value {};
    std::string error;

    static Result<T> Ok(T v) {
        Result<T> out;
        out.ok = true;
        out.value = std::move(v);
        return out;
    }

    static Result<T> Fail(std::string err) {
        Result<T> out;
        out.ok = false;
        out.error = std::move(err);
        return out;
    }
};

}  // namespace vsfclone::core
