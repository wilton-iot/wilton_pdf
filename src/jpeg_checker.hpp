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
 * File:   jpeg_checker.hpp
 * Author: alex
 *
 * Created on October 9, 2020, 8:12 PM
 */

#ifndef WILTON_PDF_JPEG_CHECKER_HPP
#define WILTON_PDF_JPEG_CHECKER_HPP


#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "jpeglib.h"

#include "staticlib/io.hpp"
#include "staticlib/support.hpp"

namespace wilton {
namespace pdf {

namespace { // anonymous

struct error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf jmpbuf;
};

void error_cb(j_common_ptr cinfo) {
    auto emgr_ptr = reinterpret_cast<error_mgr*>(cinfo->err);
    std::longjmp(emgr_ptr->jmpbuf, 1);
}

void message_cb(j_common_ptr) {
    // no-op
}

} // namespace

void check_jpeg_valid(sl::io::span<char> span) {
    struct jpeg_decompress_struct cinfo;
    struct error_mgr emgr;
    cinfo.err = jpeg_std_error(std::addressof(emgr.pub));
    emgr.pub.error_exit = error_cb;
    emgr.pub.output_message = message_cb;
    jpeg_create_decompress(std::addressof(cinfo));
    auto deferred = sl::support::defer([&cinfo]() STATICLIB_NOEXCEPT {
        jpeg_destroy_decompress(std::addressof(cinfo));
    });
    jpeg_mem_src(std::addressof(cinfo), reinterpret_cast<unsigned char*>(span.data()), span.size());
    if (0 == setjmp(emgr.jmpbuf)) {
        // jpeg error will be longjumping through this scope
        // auto vars with destructors are UB here
        jpeg_read_header(std::addressof(cinfo), true);
        jpeg_start_decompress(std::addressof(cinfo));
        int row_stride = cinfo.output_width * cinfo.output_components;
        auto buffer = (*cinfo.mem->alloc_sarray)
                (reinterpret_cast<j_common_ptr>(std::addressof(cinfo)), JPOOL_IMAGE, row_stride, 1);
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(std::addressof(cinfo), buffer, 1);
        }
        jpeg_finish_decompress(std::addressof(cinfo));
    } else {
        auto msg = std::string();
        msg.resize(JMSG_LENGTH_MAX);
        (emgr.pub.format_message)(reinterpret_cast<j_common_ptr>(std::addressof(cinfo)), std::addressof(msg.front()));
        msg.resize(std::strlen(msg.c_str()));
        throw support::exception(TRACEMSG("JPEG read error, message: [" + msg + "]"));
    }
 
}

} // namespace
}

#endif /* WILTON_PDF_JPEG_CHECKER_HPP */

