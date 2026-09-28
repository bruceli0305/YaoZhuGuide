// Run from the addon directory; see README.md for the build command.
#include "../YaoZhuGuide.cpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main()
{
    // Read the actual release DLL without executing its entry point.
    g_module = LoadLibraryExW(L"build\\x64\\Release\\YaoZhuGuide.dll",
        nullptr, LOAD_LIBRARY_AS_DATAFILE);
    assert(g_module);
    const auto resource = FindResourceW(g_module,
        MAKEINTRESOURCEW(IDR_YAOZHU_ICON), L"PNG");
    assert(resource);
    const auto bytes = LockResource(LoadResource(g_module, resource));
    assert(bytes);
    std::ifstream input("assets/YaoZhuGuideIcon.png", std::ios::binary);
    const std::vector<char> original{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    assert(!original.empty());
    assert(SizeofResource(g_module, resource) == original.size());
    assert(std::memcmp(bytes, original.data(), original.size()) == 0);

    const auto addonModule = g_module;
    g_module = GetModuleHandleW(nullptr);
    const auto configPath = GetConfigPath();
    assert(GetFileAttributesW(configPath.c_str()) == INVALID_FILE_ATTRIBUTES);
    assert(ReadConfiguredUrl() == kDefaultUrl);
    assert(WritePrivateProfileStringW(L"Browser", L"Url",
        L"https://v2.gw2.org.cn/guides/bilibili-1846648930-c-8136102",
        configPath.c_str()));
    assert(ReadConfiguredUrl() == kDefaultUrl);
    assert(WritePrivateProfileStringW(L"Browser", L"Url",
        L"https://example.com/custom", configPath.c_str()));
    assert(ReadConfiguredUrl() == L"https://example.com/custom");
    assert(DeleteFileW(configPath.c_str()));
    g_module = addonModule;

    static std::string selectedTexture;
    static int textureRequests = 0;
    AddonAPI_t api{};
    api.InputBinds_RegisterWithString = [](const char*, NexusKeybindHandlerFn, const char*) {};
    api.InputBinds_Deregister = [](const char*) {};
    api.QuickAccess_Remove = [](const char*) {};
    api.QuickAccess_Add = [](const char*, const char* normal, const char* hover,
        const char*, const char*) {
        assert(std::strcmp(normal, hover) == 0);
        selectedTexture = normal;
    };
    api.Textures_GetOrCreateFromResource = [](const char* id, unsigned resourceId,
        HMODULE module) -> NexusTexture_t* {
        assert(std::strcmp(id, kNexusTextureIdentifier) == 0);
        assert(resourceId == IDR_YAOZHU_ICON && module == g_module);
        ++textureRequests;
        return nullptr; // Normal first-load behavior: GPU creation is queued.
    };
    g_nexusApi = &api;
    RegisterQuickAccess();
    assert(textureRequests == 1 && selectedTexture == kNexusTextureIdentifier);

    g_module = GetModuleHandleW(nullptr); // Test executable has no PNG resource.
    RegisterQuickAccess();
    assert(textureRequests == 1 && selectedTexture == kNexusIconIdentifier);
    g_module = addonModule;
    api.Textures_GetOrCreateFromResource = nullptr;
    RegisterQuickAccess();
    assert(selectedTexture == kNexusIconIdentifier);
    g_nexusApi = nullptr;
    FreeLibrary(g_module);
    g_module = nullptr;

    const auto liveModule = LoadLibraryW(L"build\\x64\\Release\\YaoZhuGuide.dll");
    assert(liveModule);
    const auto liveDefinition = reinterpret_cast<AddonDefinition_t* (*)()>(
        GetProcAddress(liveModule, "GetAddonDef"));
    assert(liveDefinition);
    const auto definition = liveDefinition();
    assert(definition->Signature == 0xEA4022D7u);
    assert(definition->Version.Major == 1 && definition->Version.Minor == 0
        && definition->Version.Build == 3);
    FreeLibrary(liveModule);
}
