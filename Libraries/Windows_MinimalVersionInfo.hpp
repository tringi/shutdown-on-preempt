#ifndef WINDOWS_MINIMALVERSIONINFO_HPP
#define WINDOWS_MINIMALVERSIONINFO_HPP

#include <Windows.h>
#include <cstdint>

namespace Windows {

    class MinimalVersionInfo {
        const VS_FIXEDFILEINFO * version = nullptr;
        const wchar_t *          data = nullptr;
        std::uint16_t            size = 0;

    public:
        bool initialize (HINSTANCE);

        const wchar_t *          operator [] (const wchar_t * name) const;
        const VS_FIXEDFILEINFO * operator -> () const { return this->version; }
    };
}

#endif

