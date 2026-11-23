#include "SurveyDownloadGlue.h"
#include <cstdint>
#include <cstring>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/OsLibrary.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/WowServices/Login.h"
#include "WoW/Source/WowServices/LoginResponse.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/FileDownload.h"
#include <zlib.h>

// STATIC: 0x009F6418
static const uint8_t s_surveyExponent[4] = {
    0x01, 0x00, 0x01, 0x00,
};

// STATIC: 0x00AC4430
static uint8_t s_surveyModulus[256] = {
    0x9D, 0x6B, 0x29, 0x8C, 0x12, 0x0B, 0x73, 0x4D, 0x94, 0x5F, 0x17, 0xB5, 0xB2, 0xC8, 0x21, 0xB2,
    0x52, 0xE8, 0x01, 0x96, 0x39, 0x3D, 0xE2, 0xD5, 0xB8, 0xE3, 0x28, 0x0D, 0x30, 0x49, 0x57, 0x2B,
    0x32, 0x25, 0xD7, 0xF9, 0x29, 0x2E, 0x1B, 0xF5, 0x19, 0xF2, 0xA9, 0xBB, 0x3E, 0x4D, 0x1F, 0xC5,
    0xBD, 0x9A, 0xBE, 0x14, 0x09, 0xB3, 0xB3, 0xDB, 0x44, 0x6C, 0x31, 0xCF, 0xC6, 0xFA, 0x9A, 0x5A,
    0x02, 0xCB, 0xAC, 0xC5, 0x78, 0x3C, 0x68, 0x27, 0x52, 0xCE, 0xD5, 0x4E, 0x92, 0x1C, 0x6C, 0x23,
    0x82, 0xF5, 0xF4, 0x61, 0x26, 0x98, 0xB3, 0x52, 0x42, 0xE3, 0xC9, 0x29, 0xA5, 0x65, 0x2B, 0xF8,
    0xCD, 0xED, 0x7C, 0x8B, 0x70, 0x80, 0x47, 0x0B, 0x6A, 0x8A, 0x95, 0xD9, 0xD4, 0xFA, 0xE3, 0x9D,
    0x35, 0x0B, 0xEC, 0x67, 0x09, 0x70, 0x7F, 0xE0, 0x40, 0xD7, 0xCF, 0xE3, 0xE1, 0x88, 0x82, 0xD2,
    0x09, 0x36, 0x09, 0xF9, 0xE2, 0xE7, 0x86, 0x3F, 0x10, 0x7B, 0xF0, 0xAE, 0xC6, 0x47, 0x8C, 0x1E,
    0x05, 0x14, 0xE8, 0x4C, 0x48, 0xE4, 0x3C, 0x73, 0xE9, 0xC7, 0x3B, 0xAD, 0xFF, 0xE2, 0xD3, 0xA4,
    0xBD, 0x8D, 0xFC, 0x3D, 0x7C, 0x80, 0xEB, 0xD6, 0x45, 0x31, 0x76, 0xE8, 0xF4, 0x14, 0x06, 0xB1,
    0xF5, 0xD8, 0x19, 0x65, 0x3D, 0x75, 0xF8, 0xDC, 0x1C, 0x6A, 0xF7, 0xD4, 0x60, 0x7C, 0xE6, 0x74,
    0xEB, 0x9C, 0xDD, 0x89, 0x27, 0xA6, 0xA2, 0x2D, 0x71, 0x21, 0x0F, 0x92, 0x57, 0x3F, 0x3B, 0x30,
    0x04, 0x10, 0x74, 0x93, 0x26, 0xED, 0x89, 0x39, 0x71, 0x32, 0x48, 0x67, 0xA3, 0x2F, 0x2B, 0x71,
    0xB0, 0xC0, 0x3B, 0x7F, 0xC0, 0xDC, 0xE1, 0x89, 0xB1, 0xF2, 0x9F, 0xAD, 0xC6, 0x69, 0xED, 0xA2,
    0x30, 0xDE, 0x11, 0x6A, 0xAA, 0x2D, 0xB3, 0x8A, 0x2C, 0x47, 0xA7, 0x1E, 0x0D, 0x75, 0x8B, 0x9C,
};

// STATIC: 0x00B6B248
uint32_t CGlueMgr::m_surveyTimer;

// STATIC: 0x00B6B24C
uint8_t CGlueMgr::m_executedSurvey;

// STATIC: 0x00B6B250
FileDownload* CGlueMgr::m_surveyDownload;

// STATIC: 0x00B6B254
uint8_t* CGlueMgr::m_surveyData;

// STATIC: 0x00B6B258
uint16_t CGlueMgr::m_surveyDataSize;

// VERIFY: 01b93246 auto
// OFFSET: 0x004E4880
void CGlueMgr::SurveyDownloadFeed(const uint8_t* data, uint32_t size) {
    if (CGlueMgr::m_surveyDownload) {
        CGlueMgr::m_surveyDownload->Proceed(data, size);
    }
}

// VERIFY: 58660bf2 auto
// OFFSET: 0x004E48A0
void CGlueMgr::SurveySendResults() {
    if (CGlueMgr::m_surveyData && CGlueMgr::m_surveyDataSize) {
        DebugLog("Sending survey result\n");
        ClientServices::SendSurveyData(CGlueMgr::m_surveyDataSize, CGlueMgr::m_surveyData);
    }
}

// VERIFY: 65419e70 auto
// OFFSET: 0x004E48E0
void CGlueMgr::SurveyDownloadStart() {
    if (ClientServices::LoginConnection()->GetLoginServerType()) {
        return;
    }

    char path[260];
    CGlueMgr::GetPatchFolder(path, sizeof(path));
    SStrPack(path, "/", sizeof(path));
    SStrPack(path, "Cache", sizeof(path));
    OsCreateDirectory(path, 1);

    CGlueMgr::m_idleState = IDLE_12;
    CGlueMgr::m_showedDisconnect = 0;

    if (CGlueMgr::m_surveyDownload) {
        delete CGlueMgr::m_surveyDownload;
    }

    CGlueMgr::m_surveyDownload = new (__FILE__, __LINE__) GruntFileDownload;

    Login* login = ClientServices::LoginConnection();
    uint64_t size = login->m_fileSize;

    char fileName[260];
    CGlueMgr::GetPatchFolder(fileName, sizeof(fileName));
    SStrPack(fileName, "/", sizeof(fileName));
    SStrPack(fileName, "Cache\\Survey.mpq", sizeof(fileName));

    uint64_t offset;
    bool accept = CGlueMgr::m_surveyDownload->Initialize(fileName, ClientServices::LoginConnection()->m_fileMD5, size, offset);
    ClientServices::FileAccept(accept, offset);
}

// VERIFY: 13cc02a9 manual
// OFFSET: 0x004E4A40
void CGlueMgr::SurveyDownloadCancel() {
    CGlueMgr::m_surveyDownload->StopDownload();

    if (CGlueMgr::m_surveyDownload) {
        delete CGlueMgr::m_surveyDownload;
    }

    CGlueMgr::m_surveyDownload = nullptr;
    ClientServices::LoginConnection()->FileComplete(false);
    CGlueMgr::m_idleState = IDLE_NONE;
    CGlueMgr::m_showedDisconnect = 0;
    ClientServices::LoginConnection()->Logoff();
}

// VERIFY: fdd18e3f auto
// OFFSET: 0x004E4AA0
void CGlueMgr::SurveyDownloadIdle() {
    int32_t status = CGlueMgr::m_surveyDownload->GetStatus();

    if (status == 2) {
        if (CGlueMgr::m_surveyDownload->Accepted()) {
            ClientServices::LoginConnection()->FileComplete(true);
        }

        CGlueMgr::m_surveyDownload->CompleteDownload();
    } else {
        if (status != 3) {
            return;
        }

        if (CGlueMgr::m_surveyDownload->Accepted()) {
            ClientServices::LoginConnection()->FileComplete(false);
        }

        CGlueMgr::m_surveyDownload->Destroy(true);
    }

    CGlueMgr::m_showedDisconnect = 0;
    CGlueMgr::m_idleState = IDLE_13;
}

// VERIFY: 47cdb6c7 manual
// OFFSET: 0x004E4B30
bool CGlueMgr::SurveyExecute() {
    char currentDirectory[260];
    OsGetCurrentDirectory(sizeof(currentDirectory), currentDirectory);

    char patchFolder[260];
    CGlueMgr::GetPatchFolder(patchFolder, sizeof(patchFolder));
    OsSetCurrentDirectory(patchFolder);

    SArchive* archive = nullptr;
    HOSFILE file = OS_INVALID_FILE;
    uint32_t written = 0;
    void* dll = nullptr;
    uint32_t dllSize = 0;
    void* library = nullptr;
    bool success = false;

    SFileOpenArchive("Cache\\Survey.mpq", 100, 0, &archive);

    if (archive) {
        uint32_t authResult;
        SFileAuthenticateArchiveEx(archive, &authResult, s_surveyModulus, sizeof(s_surveyModulus), s_surveyExponent, sizeof(s_surveyExponent));

        if (authResult >= 5) {
            file = OsCreateFile("Cache\\Survey.dll", OS_GENERIC_WRITE, 0, OS_CREATE_ALWAYS, OS_FILE_ATTRIBUTE_NORMAL, OS_FILE_TYPE_ANY);

            if (file != OS_INVALID_FILE && SFileLoadFileEx(archive, "Survey.dll", &dll, &dllSize, 0, 0, nullptr) && dll) {
                OsWriteFile(file, dll, dllSize, &written);

                if (written == dllSize) {
                    OsCloseFile(file);
                    file = OS_INVALID_FILE;
                    DebugLog("Cache\\Survey.dll");
                    library = OsLoadLibrary("Cache\\Survey.dll");

                    int32_t (*extraWork)(uint16_t* data) = library ? reinterpret_cast<int32_t (*)(uint16_t*)>(OsGetLibraryEntryPoint(library, "ExtraWork")) : nullptr;
                    uint16_t* data = extraWork ? static_cast<uint16_t*>(SMemAlloc(0x1004, __FILE__, __LINE__, 0)) : nullptr;

                    if (data) {
                        memset(data, 0, 0x1004);

                        uint32_t desktopWidth = 0;
                        uint32_t desktopHeight = 0;
                        uint32_t desktopBpp = 0;
                        uint32_t width = 0;
                        uint32_t height = 0;
                        uint32_t bpp = 0;

                        if (g_theGxDevicePtr) {
                            g_theGxDevicePtr->GetSurveyModeInfo(desktopWidth, desktopHeight, desktopBpp, width, height, bpp);
                        }

                        data[0] = 4;
                        data[1] = 32;
                        reinterpret_cast<uint32_t*>(data)[1] = SFile::s_locale;
                        reinterpret_cast<uint32_t*>(data)[2] = ClientServices::GetSurveyId();
                        reinterpret_cast<uint32_t*>(data)[3] = desktopWidth;
                        reinterpret_cast<uint32_t*>(data)[4] = desktopHeight;
                        reinterpret_cast<uint32_t*>(data)[5] = desktopBpp;
                        reinterpret_cast<uint32_t*>(data)[6] = width;
                        reinterpret_cast<uint32_t*>(data)[7] = height;
                        reinterpret_cast<uint32_t*>(data)[8] = bpp;

                        if (extraWork(data) && data[1] <= 0x1000) {
                            uLongf compressedSize = 0x2000;
                            uint8_t* compressed = static_cast<uint8_t*>(SMemAlloc(0x2000, __FILE__, __LINE__, 0));

                            if (compressed) {
                                if (!compress2(compressed, &compressedSize, reinterpret_cast<const Bytef*>(data + 2), data[1], 9) && compressedSize <= 1000) {
                                    CGlueMgr::m_surveyDataSize = static_cast<uint16_t>(compressedSize);
                                    CGlueMgr::m_surveyData = static_cast<uint8_t*>(SMemAlloc(CGlueMgr::m_surveyDataSize, __FILE__, __LINE__, 0));

                                    if (CGlueMgr::m_surveyData) {
                                        memcpy(CGlueMgr::m_surveyData, compressed, CGlueMgr::m_surveyDataSize);
                                    }

                                    success = true;
                                }

                                SMemFree(compressed, __FILE__, __LINE__, 0);
                            }
                        }

                        delete data;
                    }
                }
            }
        }
    }

    if (!success) {
        ClientServices::SendSurveyFailed();
    }

    if (library) {
        OsFreeLibrary(library);
    }

    if (dll) {
        SFileUnloadFile(dll);
    }

    if (file != OS_INVALID_FILE) {
        OsCloseFile(file);
    }

    if (OsFileExists("Cache\\Survey.dll")) {
        OsDeleteFile("Cache\\Survey.dll");
    }

    if (archive) {
        SFileCloseArchive(archive);
    }

    OsSetCurrentDirectory(currentDirectory);
    return success;
}

// VERIFY: d2090c19 manual
// OFFSET: 0x004E4E80
void CGlueMgr::SurveyDownloadEnd() {
    CGlueMgr::m_executedSurvey = 0;

    if (CGlueMgr::m_surveyData) {
        SMemFree(CGlueMgr::m_surveyData, __FILE__, __LINE__, 0);
    }

    CGlueMgr::m_surveyData = nullptr;
    CGlueMgr::m_surveyDataSize = 0;

    if (CGlueMgr::m_surveyDownload) {
        delete CGlueMgr::m_surveyDownload;
        CGlueMgr::m_surveyDownload = nullptr;
    }

    ClientServices::CreateLoginResponse()->UpdateLoginStatus(LOGIN_STATE_AUTHENTICATED, LOGIN_OK, nullptr, 0);
    CGlueMgr::m_showedDisconnect = 0;
    CGlueMgr::m_idleState = IDLE_LOGIN_SERVER_LOGIN;
}
