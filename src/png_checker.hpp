/*
 * Copyright 2020, alex at staticlibs.net
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * File:   png_checker.hpp
 * Author: alex
 *
 * Created on October 9, 2020, 8:11 PM
 */

#ifndef WILTON_PDF_PNG_CHECKER_HPP
#define WILTON_PDF_PNG_CHECKER_HPP

#include <cstdint>
#include <array>
#include <string>
#include <vector>

#include "png.h"

// must go after png.h
#include <csetjmp>

#include "staticlib/io.hpp"
#include "staticlib/support.hpp"

namespace wilton {
namespace pdf {

namespace { // anonymous

void png_src_read_cb(png_structp png_ptr, png_bytep out_ptr, png_size_t to_read) STATICLIB_NOEXCEPT {
    // png_error() will be longjumping through this scope
    // auto vars with destructors are UB here
    auto src_ptr = png_get_io_ptr(png_ptr);
    if (nullptr == src_ptr) {
        png_error(png_ptr, TRACEMSG("Error obtaining IO data source").c_str());
        return;
    }
    auto& read_ctx = *static_cast<std::pair<sl::support::observer_ptr<sl::io::array_source>, std::string>*>(src_ptr);
    auto& src = *read_ctx.first;
    auto& errmsg = read_ctx.second;
    try {
        size_t count = 0;
        std::array<char, 1024> buf;
        while(to_read - count > 0) {
            size_t to_read_max = to_read - count;
            size_t to_read_now = to_read_max <= buf.size() ? to_read_max : buf.size();
            auto read = sl::io::read_all(src, {buf.data(), to_read_now});
            if (read == to_read_now) {
                std::memcpy(out_ptr + count, buf.data(), read);
                count += read;
            } else {
                std::memset(out_ptr + count, '\0', to_read_max);
                auto msg = TRACEMSG("Not enough data in input PNG buffer," +
                        " bytes requested: [" + sl::support::to_string(to_read) + "]," +
                        " read actual: [" + sl::support::to_string(read) + "]," +
                        " already read: [" + sl::support::to_string(count) + "]");
                errmsg.append(msg);
                break;
            }
        }
    } catch(const std::exception& e) {
        std::memset(out_ptr, '\0', to_read);
        png_error(png_ptr, TRACEMSG(e.what()).c_str());
    }
    if (!errmsg.empty()) {
        png_error(png_ptr, errmsg.c_str());
    }
}

// must be no-return
void png_error_cb(png_structp png_ptr, const char* msg) {
    auto err_ctx_ptr = png_get_error_ptr(png_ptr);
    if (nullptr == err_ctx_ptr) {
        // gave up
        return;
    }
    auto& err_ctx = *reinterpret_cast<std::pair<std::jmp_buf, std::string>*>(err_ctx_ptr);
    std::string& errmsg = err_ctx.second;
    errmsg.append(TRACEMSG(msg));
    std::jmp_buf& jmpbuf = err_ctx.first;
    // returning non-zero
    std::longjmp(jmpbuf, 42);
}

} // namespace

void check_png_valid(sl::io::span<char> span) {
    auto src = sl::io::array_source(span);
    // long jump setup for no-return err_cb
    auto read_ctx = std::pair<sl::support::observer_ptr<sl::io::array_source>, std::string>();
    read_ctx.first.reset(std::addressof(src));
    auto err_ctx = sl::support::make_unique<std::pair<std::jmp_buf, std::string>>();
    std::jmp_buf& jmpbuf = err_ctx->first;
    
    // check signature
    std::array<char, 8> sigbuf;
    sl::io::read_all(src, {sigbuf.data(), sigbuf.size()});
    auto sigbuf_ptr = reinterpret_cast<unsigned char*>(sigbuf.data());
    auto err_sig = png_sig_cmp(sigbuf_ptr, 0, sigbuf.size());
    if (0 != err_sig) throw support::exception(TRACEMSG(
            "Invalid PNG signature"));

    // create structs
    auto png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, err_ctx.get(), png_error_cb, nullptr);
    if (nullptr == png_ptr) throw support::exception(TRACEMSG(
            "Error creating PNG read struct"));
    auto info_ptr = png_create_info_struct(png_ptr);
    auto end_info_ptr = png_create_info_struct(png_ptr);
    auto deferred = sl::support::defer([png_ptr, info_ptr, end_info_ptr]() STATICLIB_NOEXCEPT {
        // reinterpret_cast won't compile here for some reason
        auto png_ptr_pass = const_cast<png_structpp>(std::addressof(png_ptr));
        auto info_ptr_pass = nullptr != info_ptr ? const_cast<png_infopp>(std::addressof(info_ptr)) : nullptr;
        auto end_info_ptr_pass = nullptr != end_info_ptr ? const_cast<png_infopp>(std::addressof(end_info_ptr)) : nullptr;
        png_destroy_read_struct(png_ptr_pass, info_ptr_pass, end_info_ptr_pass);
    });
    if (nullptr == info_ptr || nullptr == end_info_ptr) throw support::exception(TRACEMSG(
            "Error creating PNG structs"));

    auto row = std::vector<png_bytep>();
    // read info
    if (0 == setjmp(jmpbuf)) {
        // png_error() will be longjumping through this scope
        // auto vars with destructors are UB here
        png_set_read_fn(png_ptr, std::addressof(read_ctx), png_src_read_cb);
        png_set_sig_bytes(png_ptr, 8);
        png_read_info(png_ptr, info_ptr);
        png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);

        // read data
        size_t height = png_get_image_height(png_ptr, info_ptr);
        size_t width = png_get_image_width(png_ptr, info_ptr);
        if (width > 1<<16) throw support::exception(TRACEMSG(
                "PNG error, invalid image width: [" + sl::support::to_string(width) + "]"));
        row.resize(width);
        for (size_t i = 0; i < height; i++) {
            png_read_rows(png_ptr, row.data(), nullptr, 1);
        }
        // read end info
        png_read_end(png_ptr, end_info_ptr);
    } else {
        throw support::exception(TRACEMSG("PNG read error, message: [" + err_ctx->second + "]"));
    }
}

} // namespace
}

#endif /* WILTON_PDF_PNG_CHECKER_HPP */

