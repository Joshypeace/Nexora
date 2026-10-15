#pragma once

#include <objbase.h>

// Initialises a single-threaded COM apartment for the lifetime of the scope.
class ComApartment {
public:
    ComApartment()
        : m_initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
    {
    }
    ~ComApartment()
    {
        if (m_initialized)
            CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    bool m_initialized;
};
