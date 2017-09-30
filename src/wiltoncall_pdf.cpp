/* 
 * File:   wiltoncall_pdf.cpp
 * Author: alex
 *
 * Created on September 30, 2017, 2:06 PM
 */
#include <functional>
#include <vector>

#include "hpdf.h"

#include "staticlib/io.hpp"
#include "staticlib/ranges.hpp"
#include "staticlib/support.hpp"
#include "staticlib/utils.hpp"
#include "staticlib/tinydir.hpp"

#include "wilton/support/buffer.hpp"
#include "wilton/support/exception.hpp"
#include "wilton/support/handle_registry.hpp"
#include "wilton/support/registrar.hpp"

namespace wilton {
namespace pdf {

namespace { // anonymous

support::handle_registry<_HPDF_Doc_Rec>& static_registry() {
    static support::handle_registry<_HPDF_Doc_Rec> registry {
        [] (HPDF_Doc doc) STATICLIB_NOEXCEPT {
            HPDF_Free(doc);
        }};
    return registry;
}

float ungarble_float(const sl::json::value& val, const std::string& context) {
    float res = [&val, &context]() -> float {
        switch(val.json_type()) {
        case sl::json::type::real: return val.as_float_or_throw(context);
        case sl::json::type::integer: return static_cast<float>(val.as_int64_or_throw(context));
        default: throw support::exception(TRACEMSG(
                "Invalid RGB color element specified," +
                " type: [" + sl::json::stringify_json_type(val.json_type()) + "]," +
                " value: [" + val.dumps() + "]"));
        }
    } ();
    return res;
}

class rgb_color {
public:
    float r = 0;
    float g = 0;
    float b = 0;

    rgb_color() { }

    rgb_color(const sl::json::value& val) :
    r(check01(ungarble_float(val["r"], "color.r"))),
    g(check01(ungarble_float(val["g"], "color.g"))),
    b(check01(ungarble_float(val["b"], "color.b"))) { }

private:
    static float check01(float val) {
        if (val < static_cast<float>(0) || val > static_cast<float>(1)) {
            throw support::exception(TRACEMSG(
                    "Invalid RGB color element specified," +
                    " value: [" + sl::support::to_string(val) + "]"));
        }
        return val;
    }
};

} // namespace

support::buffer create_document(sl::io::span<const char>) {
    HPDF_Doc doc = HPDF_New([](HPDF_STATUS error_no, HPDF_STATUS detail_no, void*) {
        throw support::exception(TRACEMSG("PDF generation error: code: [" + sl::support::to_string(error_no) + "]," +
                " detail: [" + sl::support::to_string(detail_no) + "]"));
    }, nullptr);
    if (nullptr == doc) throw support::exception(TRACEMSG("'HPDF_New' error"));
    HPDF_UseUTFEncodings(doc);
    HPDF_SetCompressionMode(doc, HPDF_COMP_ALL);
    HPDF_SetPageMode(doc, HPDF_PAGE_MODE_USE_OUTLINE);
    int64_t handle = static_registry().put(doc);
    return support::make_json_buffer({
        { "pdfDocumentHandle", handle}
    });
}

support::buffer load_font(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    auto rpath = std::ref(sl::utils::empty_string());
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("ttfPath" == name) {
            rpath = fi.as_string_nonempty_or_throw(name);
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (rpath.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'ttfPath' not specified"));
    const std::string& path = rpath.get();
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    auto font_name = HPDF_LoadTTFontFromFile(doc, path.c_str(), HPDF_TRUE);
    return support::make_json_buffer({
        { "fontName", font_name }
    });
}

support::buffer add_page(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    auto rformat = std::ref(sl::utils::empty_string());
    auto rorient = std::ref(sl::utils::empty_string());
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("format" == name) {
            rformat = fi.as_string_nonempty_or_throw(name);
        } else if ("orientation" == name) {
            rorient = fi.as_string_nonempty_or_throw(name);
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (rformat.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'format' not specified"));
    const std::string& format = rformat.get();
    if (rorient.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'orientation' not specified"));
    const std::string& orient = rorient.get();
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_PageSizes hformat = [&format] () -> HPDF_PageSizes {
       if ("A3" == format) {
           return HPDF_PAGE_SIZE_A3;
       } else if ("A4" == format) {
           return HPDF_PAGE_SIZE_A4;
       } else if ("A5" == format) {
           return HPDF_PAGE_SIZE_A5;
       } else if ("B4" == format) {
           return HPDF_PAGE_SIZE_B4;
       } else if ("B5" == format) {
           return HPDF_PAGE_SIZE_B5;
       } else throw support::exception(TRACEMSG("Unsupported PDF page format specified, format: [" + format + "]"));
    } ();
    HPDF_PageDirection horient = [&orient] () -> HPDF_PageDirection {
       if ("PORTRAIT" == orient) {
           return HPDF_PAGE_PORTRAIT;
       } else if ("LANDSCAPE" == orient) {
           return HPDF_PAGE_LANDSCAPE;
       } else throw support::exception(TRACEMSG("Unsupported PDF page orientation specified, orientation: [" + orient + "]"));
    } ();
    HPDF_Page page = HPDF_AddPage(doc);
    if (nullptr == page) throw support::exception(TRACEMSG("'HPDF_AddPage' error"));
    HPDF_Page_SetSize(page, hformat, horient);
    return support::make_empty_buffer();
}

support::buffer write_text(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    auto rfont_name = std::ref(sl::utils::empty_string());
    float font_size = -1;
    auto rtext = std::ref(sl::utils::empty_string());
    int32_t x = -1;
    int32_t y = -1;
    auto color = rgb_color();
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("fontName" == name) {
            rfont_name = fi.as_string_nonempty_or_throw(name);
        } else if ("fontSize" == name) {
            font_size = ungarble_float(fi.val(), name);
        } else if ("text" == name) {
            rtext = fi.as_string_nonempty_or_throw(name);
        } else if ("x" == name) {
            x = fi.as_uint16_or_throw(name);
        } else if ("y" == name) {
            y = fi.as_uint16_or_throw(name);
        } else if ("color" == name) {
            color = rgb_color(fi.val());
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (rfont_name.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'fontName' not specified"));
    if (font_size < 0) throw support::exception(TRACEMSG(
            "Required parameter 'fontSize' not specified"));
    if (-1 == x) throw support::exception(TRACEMSG(
            "Required parameter 'x' not specified"));
    if (-1 == y) throw support::exception(TRACEMSG(
            "Required parameter 'y' not specified"));
    if (rtext.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'text' not specified"));
    const std::string& font_name = rfont_name.get();
    const std::string& text = rtext.get();
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_Page page = HPDF_GetCurrentPage(doc);
    if (nullptr == page) throw support::exception(TRACEMSG(
            "PDF generation error, cannot access current page," +
            " please add at least one page to the document first"));
    HPDF_Page_SetRGBFill(page, color.r, color.g, color.b);
    auto font = HPDF_GetFont(doc, font_name.c_str(), "UTF-8");
    HPDF_Page_SetFontAndSize(page, font, font_size);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextOut(page, static_cast<float>(x), static_cast<float>(y), text.c_str());
    HPDF_Page_EndText(page);
    return support::make_empty_buffer();
}

support::buffer write_text_inside_rectangle(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    auto rfont_name = std::ref(sl::utils::empty_string());
    float font_size = -1;
    auto rtext = std::ref(sl::utils::empty_string());
    int32_t left = -1;
    int32_t top = -1;
    int32_t right = -1;
    int32_t bottom = -1;
    auto ralign = std::ref(sl::utils::empty_string());
    auto color = rgb_color();
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("fontName" == name) {
            rfont_name = fi.as_string_nonempty_or_throw(name);
        } else if ("fontSize" == name) {
            font_size = ungarble_float(fi.val(), name);
        } else if ("text" == name) {
            rtext = fi.as_string_nonempty_or_throw(name);
        } else if ("left" == name) {
            left = fi.as_uint16_or_throw(name);
        } else if ("top" == name) {
            top = fi.as_uint16_or_throw(name);
        } else if ("right" == name) {
            right = fi.as_uint16_or_throw(name);
        } else if ("bottom" == name) {
            bottom = fi.as_uint16_or_throw(name);
        } else if ("align" == name) {
            ralign = fi.as_string_nonempty_or_throw(name);
        } else if ("color" == name) {
            color = rgb_color(fi.val());
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (rfont_name.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'fontName' not specified"));
    if (font_size < 0) throw support::exception(TRACEMSG(
            "Required parameter 'fontSize' not specified"));
    if (-1 == left) throw support::exception(TRACEMSG(
            "Required parameter 'left' not specified"));
    if (-1 == top) throw support::exception(TRACEMSG(
            "Required parameter 'top' not specified"));
    if (-1 == right) throw support::exception(TRACEMSG(
            "Required parameter 'right' not specified"));
    if (-1 == bottom) throw support::exception(TRACEMSG(
            "Required parameter 'bottom' not specified"));
    if (rtext.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'text' not specified"));
    if (ralign.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'align' not specified"));
    const std::string& font_name = rfont_name.get();
    const std::string& text = rtext.get();
    const std::string& align = ralign.get();
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_TextAlignment halign = [&align]() -> HPDF_TextAlignment {
        if ("LEFT" == align) {
            return HPDF_TALIGN_LEFT;
        } else if ("RIGHT" == align) {
            return HPDF_TALIGN_RIGHT;
        } else if ("CENTER" == align) {
            return HPDF_TALIGN_CENTER;
        } else if ("JUSTIFY" == align) {
            return HPDF_TALIGN_JUSTIFY;
        } else throw support::exception(TRACEMSG(
                "Invalid 'align' parameter specified, value: [" + align + "]"));
    } ();
    HPDF_Page page = HPDF_GetCurrentPage(doc);
    if (nullptr == page) throw support::exception(TRACEMSG(
            "PDF generation error, cannot access current page," +
            " please add at least one page to the document first"));
    HPDF_Page_SetRGBFill(page, color.r, color.g, color.b);
    auto font = HPDF_GetFont(doc, font_name.c_str(), "UTF-8");
    HPDF_Page_SetFontAndSize(page, font, font_size);
    HPDF_Page_BeginText(page);
    HPDF_Page_TextRect(page, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom), text.c_str(), halign, nullptr);
    HPDF_Page_EndText(page);
    return support::make_empty_buffer();
}

support::buffer draw_line(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    int32_t beginX = -1;
    int32_t beginY = -1;
    int32_t endX = -1;
    int32_t endY = -1;
    float lineWidth = 1;
    auto color = rgb_color();
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("beginX" == name) {
            beginX = fi.as_uint16_or_throw(name);
        } else if ("beginY" == name) {
            beginY = fi.as_uint16_or_throw(name);
        } else if ("endX" == name) {
            endX = fi.as_uint16_or_throw(name);
        } else if ("endY" == name) {
            endY = fi.as_uint16_or_throw(name);
        } else if ("color" == name) {
            color = rgb_color(fi.val());
        } else if ("lineWidth" == name) {
            lineWidth = ungarble_float(fi.val(), "lineWidth");
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (-1 == beginX) throw support::exception(TRACEMSG(
            "Required parameter 'beginX' not specified"));
    if (-1 == beginY) throw support::exception(TRACEMSG(
            "Required parameter 'beginY' not specified"));
    if (-1 == endX) throw support::exception(TRACEMSG(
            "Required parameter 'endX' not specified"));
    if (-1 == endY) throw support::exception(TRACEMSG(
            "Required parameter 'endY' not specified"));
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_Page page = HPDF_GetCurrentPage(doc);
    if (nullptr == page) throw support::exception(TRACEMSG(
            "PDF generation error, cannot access current page," +
            " please add at least one page to the document first"));
    HPDF_Page_SetRGBStroke(page, color.r, color.g, color.b);
    HPDF_Page_SetLineWidth(page, lineWidth);
    HPDF_Page_MoveTo(page, static_cast<float>(beginX), static_cast<float>(beginY));
    HPDF_Page_LineTo(page, static_cast<float>(endX), static_cast<float>(endY));
    HPDF_Page_Stroke(page);
    return support::make_empty_buffer();
}

support::buffer draw_rectangle(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    int32_t x = -1;
    int32_t y = -1;
    int32_t width = -1;
    int32_t height = -1;
    float lineWidth = 1;
    auto color = rgb_color();
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("x" == name) {
            x = fi.as_uint16_or_throw(name);
        } else if ("y" == name) {
            y = fi.as_uint16_or_throw(name);
        } else if ("width" == name) {
            width = fi.as_uint16_or_throw(name);
        } else if ("height" == name) {
            height = fi.as_uint16_or_throw(name);
        } else if ("color" == name) {
            color = rgb_color(fi.val());
        } else if ("lineWidth" == name) {
            lineWidth = ungarble_float(fi.val(), "lineWidth");
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (-1 == x) throw support::exception(TRACEMSG(
            "Required parameter 'x' not specified"));
    if (-1 == y) throw support::exception(TRACEMSG(
            "Required parameter 'y' not specified"));
    if (-1 == width) throw support::exception(TRACEMSG(
            "Required parameter 'width' not specified"));
    if (-1 == height) throw support::exception(TRACEMSG(
            "Required parameter 'height' not specified"));
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_Page page = HPDF_GetCurrentPage(doc);
    if (nullptr == page) throw support::exception(TRACEMSG(
            "PDF generation error, cannot access current page," +
            " please add at least one page to the document first"));
    HPDF_Page_SetRGBStroke(page, color.r, color.g, color.b);
    HPDF_Page_SetLineWidth(page, lineWidth);
    HPDF_Page_Rectangle(page, static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    HPDF_Page_Stroke(page);
    return support::make_empty_buffer();
}

support::buffer save_to_file(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    auto rpath = std::ref(sl::utils::empty_string());
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else if ("path" == name) {
            rpath = fi.as_string_nonempty_or_throw(name);
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    if (rpath.get().empty()) throw support::exception(TRACEMSG(
            "Required parameter 'path' not specified"));
    const std::string& path = rpath.get();
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    auto deferred = sl::support::defer([doc]() STATICLIB_NOEXCEPT {
        static_registry().put(doc);
    });
    // call haru
    HPDF_SaveToFile(doc, path.c_str());
    return support::make_empty_buffer();
}

support::buffer destroy_document(sl::io::span<const char> data) {
    // json parse
    auto json = sl::json::load(data);
    int64_t handle = -1;
    for (const sl::json::field& fi : json.as_object()) {
        auto& name = fi.name();
        if ("pdfDocumentHandle" == name) {
            handle = fi.as_int64_or_throw(name);
        } else {
            throw support::exception(TRACEMSG("Unknown data field: [" + name + "]"));
        }
    }
    if (-1 == handle) throw support::exception(TRACEMSG(
            "Required parameter 'pdfDocumentHandle' not specified"));
    // get handle
    HPDF_Doc doc = static_registry().remove(handle);
    if (nullptr == doc) throw support::exception(TRACEMSG(
            "Invalid 'pdfDocumentHandle' parameter specified"));
    // call haru
    HPDF_Free(doc);
    return support::make_empty_buffer();
}

/*
support::buffer test(sl::io::span<const char> data) {
    (void) data;
    std::cout << "pdf::test" << std::endl;
    // pdf gen
    HPDF_Doc pdf = HPDF_New([](HPDF_STATUS error_no, HPDF_STATUS detail_no, void*) {
        throw support::exception(TRACEMSG("PDF generation error: code: [" + sl::support::to_string(error_no) + "]," +
                " detail: [" + sl::support::to_string(detail_no) + "]"));
    }, nullptr);
    if (nullptr == pdf) throw support::exception(TRACEMSG("'HPDF_New' error"));
    HPDF_UseUTFEncodings(pdf);
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);
    HPDF_SetPageMode(pdf, HPDF_PAGE_MODE_USE_OUTLINE);

    HPDF_LoadTTFontFromFile(pdf, "../modules/wilton_pdf/test/fonts/DejaVuSans.ttf", HPDF_TRUE);
    auto font_name = HPDF_LoadTTFontFromFile(pdf, "../modules/wilton_pdf/test/fonts/DejaVuSans.ttf", HPDF_TRUE);
    
    HPDF_AddPage(pdf);
    HPDF_Page_SetSize(HPDF_GetCurrentPage(pdf), HPDF_PAGE_SIZE_A4, HPDF_PAGE_PORTRAIT);

    HPDF_Page_SetRGBStroke(HPDF_GetCurrentPage(pdf), 0, 1, 0);
    HPDF_Page_SetLineWidth(HPDF_GetCurrentPage(pdf), 1.8);
    HPDF_Page_Rectangle(HPDF_GetCurrentPage(pdf), 200, 200, 100, 100);
    HPDF_Page_Stroke(HPDF_GetCurrentPage(pdf));

    HPDF_Page_MoveTo(HPDF_GetCurrentPage(pdf), 310, 310);
    HPDF_Page_LineTo(HPDF_GetCurrentPage(pdf), 350, 350);
    HPDF_Page_Stroke(HPDF_GetCurrentPage(pdf));
    
    HPDF_Page_SetRGBFill(HPDF_GetCurrentPage(pdf), 0, 0, 1);
    HPDF_Page_SetFontAndSize(HPDF_GetCurrentPage(pdf), HPDF_GetFont(pdf, font_name, "UTF-8"), 42);
    HPDF_Page_BeginText(HPDF_GetCurrentPage(pdf));
    HPDF_Page_TextOut(HPDF_GetCurrentPage(pdf), 100, 100, "hello from pdf!");
    HPDF_Page_EndText(HPDF_GetCurrentPage(pdf));
    HPDF_SaveToFile(pdf, "test.pdf");
    
    return support::make_empty_buffer();
}
 * */

} // namespace
}

extern "C" char* wilton_module_init() {
    try {
        wilton::support::register_wiltoncall("pdf_create_document", wilton::pdf::create_document);
        wilton::support::register_wiltoncall("pdf_load_font", wilton::pdf::load_font);
        wilton::support::register_wiltoncall("pdf_add_page", wilton::pdf::add_page);
        wilton::support::register_wiltoncall("pdf_write_text", wilton::pdf::write_text);
        wilton::support::register_wiltoncall("pdf_write_text_inside_rectangle", wilton::pdf::write_text_inside_rectangle);
        wilton::support::register_wiltoncall("pdf_draw_line", wilton::pdf::draw_line);
        wilton::support::register_wiltoncall("pdf_draw_rectangle", wilton::pdf::draw_rectangle);
        wilton::support::register_wiltoncall("pdf_save_to_file", wilton::pdf::save_to_file);
        wilton::support::register_wiltoncall("pdf_destroy_document", wilton::pdf::destroy_document);
        return nullptr;
    } catch (const std::exception& e) {
        return wilton::support::alloc_copy(TRACEMSG(e.what() + "\nException raised"));
    }
}
