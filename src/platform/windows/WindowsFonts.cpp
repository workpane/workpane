#include "platform/windows/WindowsFonts.h"

#include "platform/windows/WindowsTextHelper.h"

#include <dwrite_2.h>
#include <wrl/client.h>

#include <filesystem>

namespace workpane::platform {

// Every family DirectWrite knows whose regular face is monospaced is offered with the file that face is read from.
std::vector<InstalledFont> WindowsFonts::monospace() {
    std::vector<InstalledFont> fonts;
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) || FAILED(factory->GetSystemFontCollection(collection.GetAddressOf()))) {
        return fonts;
    }

    for (UINT32 index = 0; index < collection->GetFontFamilyCount(); ++index) {
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        Microsoft::WRL::ComPtr<IDWriteFont> font;
        Microsoft::WRL::ComPtr<IDWriteFont1> described;

        if (FAILED(collection->GetFontFamily(index, family.GetAddressOf())) || FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())) || FAILED(font.As(&described)) || !described->IsMonospacedFont()) {
            continue;
        }

        const std::wstring name = familyName(*family.Get());
        const std::wstring path = fontFile(*font.Get());

        if (!name.empty() && !path.empty()) {
            fonts.push_back({WindowsTextHelper::narrow(name.c_str()), std::filesystem::path(path)});
        }
    }

    return fonts;
}

// The families of Windows that draw color emoji, symbols, historic scripts and the scripts of Asia are offered in that order, each with the face of its regular weight and its index inside its file.
std::vector<FallbackFace> WindowsFonts::fallbacks() {
    std::vector<FallbackFace> faces;
    Microsoft::WRL::ComPtr<IDWriteFactory> factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf()))) || FAILED(factory->GetSystemFontCollection(collection.GetAddressOf()))) {
        return faces;
    }

    for (const wchar_t* name : fallbackFamilies) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        Microsoft::WRL::ComPtr<IDWriteFont> font;
        Microsoft::WRL::ComPtr<IDWriteFont2> colored;
        Microsoft::WRL::ComPtr<IDWriteFontFace> face;

        if (FAILED(collection->FindFamilyName(name, &index, &exists)) || exists == FALSE || FAILED(collection->GetFontFamily(index, family.GetAddressOf())) || FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.GetAddressOf())) || FAILED(font->CreateFontFace(face.GetAddressOf()))) {
            continue;
        }

        const std::wstring path = fontFile(*font.Get());

        if (!path.empty()) {
            faces.push_back({std::filesystem::path(path), face->GetIndex(), SUCCEEDED(font.As(&colored)) && colored->IsColorFont() == TRUE});
        }
    }

    return faces;
}

// The English name of a family is the one the reader finds everywhere, and the first name answers when the family has none.
std::wstring WindowsFonts::familyName(IDWriteFontFamily& family) {
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
    UINT32 index = 0;
    BOOL exists = FALSE;
    UINT32 length = 0;

    if (FAILED(family.GetFamilyNames(names.GetAddressOf()))) {
        return {};
    }

    names->FindLocaleName(L"en-us", &index, &exists);
    index = exists == TRUE ? index : 0;

    if (FAILED(names->GetStringLength(index, &length))) {
        return {};
    }

    std::wstring name(length + 1, L'\0');

    if (FAILED(names->GetString(index, name.data(), length + 1))) {
        return {};
    }

    name.resize(length);

    return name;
}

// A face read from a file on the disk answers its path through the loader of local files, and a face from anywhere else answers nothing.
std::wstring WindowsFonts::fontFile(IDWriteFont& font) {
    Microsoft::WRL::ComPtr<IDWriteFontFace> face;
    Microsoft::WRL::ComPtr<IDWriteFontFile> file;
    Microsoft::WRL::ComPtr<IDWriteFontFileLoader> loader;
    Microsoft::WRL::ComPtr<IDWriteLocalFontFileLoader> local;
    UINT32 count = 1;
    const void* key = nullptr;
    UINT32 keySize = 0;
    UINT32 length = 0;

    if (FAILED(font.CreateFontFace(face.GetAddressOf())) || FAILED(face->GetFiles(&count, file.GetAddressOf())) || FAILED(file->GetReferenceKey(&key, &keySize)) || FAILED(file->GetLoader(loader.GetAddressOf())) || FAILED(loader.As(&local)) || FAILED(local->GetFilePathLengthFromKey(key, keySize, &length))) {
        return {};
    }

    std::wstring path(length + 1, L'\0');

    if (FAILED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1))) {
        return {};
    }

    path.resize(length);

    return path;
}

} // namespace workpane::platform
