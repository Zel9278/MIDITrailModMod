#include "MTLinuxCompat.h"
#include "../MTFont2Bmp.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<unsigned long> decode_utf8(const char* text)
{
    std::vector<unsigned long> codepoints;
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text ? text : "");
    while (*cursor != 0) {
        unsigned long codepoint = 0;
        int length = 1;
        if ((*cursor & 0x80) == 0) codepoint = *cursor;
        else if ((*cursor & 0xE0) == 0xC0) { codepoint = *cursor & 0x1F; length = 2; }
        else if ((*cursor & 0xF0) == 0xE0) { codepoint = *cursor & 0x0F; length = 3; }
        else if ((*cursor & 0xF8) == 0xF0) { codepoint = *cursor & 0x07; length = 4; }
        else { ++cursor; continue; }
        bool valid = true;
        for (int index = 1; index < length; ++index) {
            if ((cursor[index] & 0xC0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (cursor[index] & 0x3F);
        }
        if (valid) codepoints.push_back(codepoint);
        cursor += valid ? length : 1;
    }
    return codepoints;
}

std::string resolve_font(const char* family, bool fixed_pitch)
{
    if (!FcInit()) return {};
    FcPattern* pattern = FcPatternCreate();
    if (family && family[0] != '\0') {
        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family));
    }
    if (fixed_pitch) FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    std::string path;
    FcChar8* file = nullptr;
    if (match && FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
        path = reinterpret_cast<const char*>(file);
    }
    if (match) FcPatternDestroy(match);
    FcPatternDestroy(pattern);
    return path;
}

} // namespace

MTFont2Bmp::MTFont2Bmp()
    : m_FontSize(0), m_isForceFixedPitch(false), m_hFont(nullptr),
      m_pBmpBuf(nullptr), m_BmpHeight(0), m_BmpWidth(0)
{
    m_FontName[0] = '\0';
    ZeroMemory(&m_TextMetric, sizeof(m_TextMetric));
}

MTFont2Bmp::~MTFont2Bmp()
{
    Clear();
}

void MTFont2Bmp::Clear()
{
    delete[] m_pBmpBuf;
    m_pBmpBuf = nullptr;
    m_BmpHeight = 0;
    m_BmpWidth = 0;
    m_GlyphBmpList.clear();
}

int MTFont2Bmp::SetFont(const TCHAR* font_name, unsigned long font_size, bool fixed_pitch)
{
    std::snprintf(m_FontName, LF_FACESIZE, "%s", font_name ? font_name : "sans-serif");
    m_FontSize = std::max(1UL, font_size);
    m_isForceFixedPitch = fixed_pitch;
    return 0;
}

int MTFont2Bmp::CreateBmp(const TCHAR* text)
{
    Clear();
    const std::vector<unsigned long> codepoints = decode_utf8(text);
    if (codepoints.empty()) return 0;

    const std::string font_path = resolve_font(m_FontName, m_isForceFixedPitch);
    if (font_path.empty()) return -1;

    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library) != 0) return -1;
    if (FT_New_Face(library, font_path.c_str(), 0, &face) != 0) {
        FT_Done_FreeType(library);
        return -1;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(m_FontSize));

    long pen_x = 0;
    long min_x = 0;
    long max_x = 0;
    long max_top = 0;
    long min_bottom = 0;
    for (unsigned long codepoint : codepoints) {
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) continue;
        const FT_GlyphSlot glyph = face->glyph;
        min_x = std::min(min_x, pen_x + glyph->bitmap_left);
        max_x = std::max(max_x, pen_x + glyph->bitmap_left + static_cast<long>(glyph->bitmap.width));
        max_top = std::max(max_top, static_cast<long>(glyph->bitmap_top));
        min_bottom = std::min(min_bottom,
            static_cast<long>(glyph->bitmap_top) - static_cast<long>(glyph->bitmap.rows));
        pen_x += glyph->advance.x >> 6;
        max_x = std::max(max_x, pen_x);
    }

    const unsigned long raw_width = static_cast<unsigned long>(std::max(1L, max_x - min_x));
    m_BmpWidth = (raw_width + 3UL) & ~3UL;
    m_BmpHeight = static_cast<unsigned long>(std::max(1L, max_top - min_bottom));
    m_BmpWidth = std::min(m_BmpWidth, static_cast<unsigned long>(MTFONT2BMP_MAX_BMP_WIDTH));
    m_pBmpBuf = new BYTE[static_cast<std::size_t>(m_BmpWidth) * m_BmpHeight]();

    pen_x = -min_x;
    for (unsigned long codepoint : codepoints) {
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) continue;
        const FT_GlyphSlot glyph = face->glyph;
        const long destination_x = pen_x + glyph->bitmap_left;
        const long destination_y = max_top - glyph->bitmap_top;
        for (unsigned int row = 0; row < glyph->bitmap.rows; ++row) {
            const unsigned char* source = glyph->bitmap.buffer + row * std::abs(glyph->bitmap.pitch);
            for (unsigned int column = 0; column < glyph->bitmap.width; ++column) {
                const long x = destination_x + column;
                const long y = destination_y + row;
                if (x < 0 || y < 0 || x >= static_cast<long>(m_BmpWidth)
                    || y >= static_cast<long>(m_BmpHeight)) continue;
                const BYTE coverage = static_cast<BYTE>((source[column] * 16U + 127U) / 255U);
                BYTE& destination = m_pBmpBuf[y * m_BmpWidth + x];
                destination = std::max(destination, coverage);
            }
        }
        pen_x += glyph->advance.x >> 6;
        if (pen_x >= static_cast<long>(m_BmpWidth)) break;
    }

    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return 0;
}

void MTFont2Bmp::GetBmpSize(unsigned long* height, unsigned long* width)
{
    if (height) *height = m_BmpHeight;
    if (width) *width = m_BmpWidth;
}

BYTE MTFont2Bmp::GetBmpPixcel(unsigned long x, unsigned long y)
{
    if (!m_pBmpBuf || x >= m_BmpWidth || y >= m_BmpHeight) return 0xFF;
    return m_pBmpBuf[y * m_BmpWidth + x];
}

int MTFont2Bmp::_CreateLogFont() { return 0; }
int MTFont2Bmp::_CreateGlyphBmp(unsigned long, MTGlyphBmp*) { return -1; }
int MTFont2Bmp::_CreateGlyphBmpList(const TCHAR*) { return -1; }
int MTFont2Bmp::_CreateBmpBuf() { return -1; }
int MTFont2Bmp::_WriteGlyphToBmpBuf() { return -1; }
