#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#define __CASCLIB_SELF__
#include <CascLib.h>
#include <CascCommon.h>

int main(int argc, char** argv)
{
    if (argc != 5)
        return 2;
    TCascStorage storage;
    storage.dwFeatures = CASC_FEATURE_ONLINE;
    storage.FileOffsetBits = 32;
    storage.szDataPath = _strdup(argv[1]);
    storage.szCdnServers = _strdup(argv[2]);
    storage.szCdnPath = _strdup(argv[3]);
    storage.ArchivesKey.pbData = CASC_ALLOC<BYTE>(MD5_HASH_SIZE);
    storage.ArchivesKey.cbData = MD5_HASH_SIZE;
    memset(storage.ArchivesKey.pbData, 0x22, MD5_HASH_SIZE);
    const auto payload = std::string("BLTE\0\0\0\0N", 9) + "range fixture";
    CASC_EKEY_ENTRY entry = {};
    CascHash_MD5(payload.data(), payload.size(), entry.EKey);
    entry.StorageOffset = 123;
    entry.EncodedSize = DWORD(payload.size());
    if (storage.IndexMap.Create(1, MD5_HASH_SIZE, FIELD_OFFSET(CASC_EKEY_ENTRY, EKey)) != ERROR_SUCCESS
        || !storage.IndexMap.InsertObject(&entry, entry.EKey))
        return 3;
    CASC_PATH<TCHAR> path;
    CASC_ARCHIVE_INFO info;
    memset(&info, 0xff, sizeof(info));
    const DWORD error = FetchCascFile(&storage, PathTypeData, entry.EKey, nullptr, path, &info);
    const bool expected = std::string(argv[4]) == "success";
    if ((error == ERROR_SUCCESS) != expected)
    {
        fprintf(stderr, "Unexpected fetch result: %lu\n", error);
        return 4;
    }
    if (!expected)
        return std::filesystem::exists(static_cast<const char*>(path)) ? 5 : 0;
    const CASC_ARCHIVE_INFO zero = {};
    if (memcmp(&info, &zero, sizeof(info)))
        return 6;
    std::ifstream input(static_cast<const char*>(path), std::ios::binary);
    const std::string actual{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (actual != payload)
        return 7;
    // A second read must reuse the standalone stream without contacting any server.
    free(storage.szCdnServers);
    storage.szCdnServers = _strdup("127.0.0.1:1");
    return FetchCascFile(&storage, PathTypeData, entry.EKey, nullptr, path, &info) == ERROR_SUCCESS ? 0 : 8;
}
