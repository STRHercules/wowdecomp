// Inn select hang watchdog: writes a stalled thread's call stack (module + offset per frame) so a freeze can be symbolized
// offline against the exe's PDB. Kept apart from CharacterSelection.cpp so <windows.h> stays out of the glue code.
#include <windows.h>
#include <cstdio>
#include <cstring>

void* InnWatchdogOpenCurrentThread() {
    return OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
}

void InnWatchdogWriteStack(void* thread, FILE* file) {
#if defined(_M_X64)
    HANDLE handle = static_cast<HANDLE>(thread);
    if (!handle || !file) return;
    if (SuspendThread(handle) == static_cast<DWORD>(-1)) return;
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(handle, &ctx)) {
        for (int frame = 0; frame < 48 && ctx.Rip; frame++) {
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(ctx.Rip), &module) && module) {
                GetModuleFileNameA(module, name, MAX_PATH);
            }
            const char* base = std::strrchr(name, '\\');
            std::fprintf(file, "  #%02d %s+0x%llx\n", frame, base ? base + 1 : name,
                static_cast<unsigned long long>(ctx.Rip - reinterpret_cast<DWORD64>(module)));

            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            if (!entry) {
                // leaf function: the return address is on top of the stack
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
                continue;
            }
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, entry, &ctx, &handlerData, &establisher, nullptr);
        }
    }
    ResumeThread(handle);
#else
    (void)thread; (void)file;
#endif
}
