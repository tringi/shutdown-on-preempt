#include "Windows_MinimalVersionInfo.hpp"
#include <cstring>
#include <cwchar>

namespace {
    struct VS_HEADER {
        WORD wLength;
        WORD wValueLength;
        WORD wType;
    };
}

bool Windows::MinimalVersionInfo::initialize (HINSTANCE hInstance) {
    if (HRSRC hRsrc = FindResource (hInstance, MAKEINTRESOURCE (1), RT_VERSION)) {
        if (HGLOBAL hGlobal = LoadResource (hInstance, hRsrc)) {
            auto data = LockResource (hGlobal);
            auto size = SizeofResource (hInstance, hRsrc);

            struct VS_VERSIONINFO : public VS_HEADER {
                WCHAR szKey [sizeof "VS_VERSION_INFO"]; // 15 characters
                WORD  Padding1 [1];
                VS_FIXEDFILEINFO Value;
            };

            if (size >= sizeof (VS_VERSIONINFO)) {
                const auto * vi = static_cast <const VS_HEADER *> (data);
                const auto * vp = static_cast <const unsigned char *> (data)
                    + sizeof (VS_VERSIONINFO) + sizeof (VS_HEADER) - sizeof (VS_FIXEDFILEINFO)
                    + vi->wValueLength;

                if (!std::wcscmp (reinterpret_cast <const wchar_t *> (vp), L"StringFileInfo")) {
                    vp += sizeof (L"StringFileInfo");

                    this->size = reinterpret_cast <const VS_HEADER *> (vp)->wLength / 2 - std::size_t (12);
                    this->data = reinterpret_cast <const wchar_t *> (vp) + 12;
                }

                if (vi->wValueLength) {
                    auto p = reinterpret_cast <const DWORD *> (LockResource (hGlobal));
                    auto e = p + (size - sizeof (VS_FIXEDFILEINFO)) / sizeof (DWORD);

                    for (; p != e; ++p)
                        if (*p == 0xFEEF04BDu)
                            break;

                    if (p != e)
                        this->version = reinterpret_cast <const VS_FIXEDFILEINFO *> (p);
                }
            }
        }
    }

    return this->version && this->data;
}

const wchar_t * Windows::MinimalVersionInfo::operator [] (const wchar_t * name) const {
    if (this->data) {

        const VS_HEADER * header;
        auto p = this->data;
        auto e = this->data + this->size;

        while ((p < e) && ((header = reinterpret_cast <const VS_HEADER *> (p))->wLength != 0)) {

            auto length = header->wLength / 2;
            if (std::wcscmp (p + 3, name) == 0) {
                return p + length - header->wValueLength;
            }

            p += length;
            if (length % 2) {
                ++p;
            }
        }
    }
    return nullptr;
}