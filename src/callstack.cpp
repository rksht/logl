#include <learnogl/essential_headers.h>

#include <learnogl/callstack.h>

#if defined(__linux__)

#    include <cxxabi.h>
#    include <execinfo.h>
#    include <unistd.h>

static const char *addr2line(const char *addr, char *line, int len) {
    char buf[256];
    snprintf(buf, sizeof(buf), "addr2line -e /proc/%u/exe %s", getpid(), addr);
    FILE *f = popen(buf, "r");
    if (f) {
        char *ret = fgets(line, len, f);
        line[strlen(line) - 1] = '\0';
        pclose(f);
        return line;
    }
    return "<addr2line missing>";
}

#endif

namespace eng {

#if defined(WIN32)
#    include <Windows.h>
#    include <dbghelp.h>

#endif

using namespace fo;
using namespace string_stream;

#if defined(WIN32)
void print_callstack(Buffer &ss) {
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    DWORD mtype;
    CONTEXT ctx;
    ZeroMemory(&ctx, sizeof(CONTEXT));
    ctx.ContextFlags = CONTEXT_CONTROL;
    RtlCaptureContext(&ctx);

    STACKFRAME64 stack;
    ZeroMemory(&stack, sizeof(STACKFRAME64));
#    if defined(_M_IX86)
    mtype = IMAGE_FILE_MACHINE_I386;
    stack.AddrPC.Offset = ctx.Eip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = ctx.Ebp;
    stack.AddrFrame.Mode = AddrModeFlat;
    stack.AddrStack.Offset = ctx.Esp;
    stack.AddrStack.Mode = AddrModeFlat;
#    elif defined(_M_X64)
    mtype = IMAGE_FILE_MACHINE_AMD64;
    stack.AddrPC.Offset = ctx.Rip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = ctx.Rsp;
    stack.AddrFrame.Mode = AddrModeFlat;
    stack.AddrStack.Offset = ctx.Rsp;
    stack.AddrStack.Mode = AddrModeFlat;
#    endif

    DWORD ldsp = 0;
    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(IMAGEHLP_LINE64));
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO sym = (PSYMBOL_INFO)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = MAX_SYM_NAME;

    UINT num = 0;
    while (StackWalk64(mtype,
                       GetCurrentProcess(),
                       GetCurrentThread(),
                       &stack,
                       &ctx,
                       NULL,
                       SymFunctionTableAccess64,
                       SymGetModuleBase64,
                       NULL)) {
        if (stack.AddrPC.Offset == 0) {
            break;
        }

        ++num;

        BOOL res = SymGetLineFromAddr64(GetCurrentProcess(), stack.AddrPC.Offset, &ldsp, &line);
        res = res && SymFromAddr(GetCurrentProcess(), stack.AddrPC.Offset, 0, sym);

        char buf[512];

        if (res == TRUE) {
            snprintf(
                buf, sizeof(buf), "    [%2i] %s in %s:%d\n", num, sym->Name, line.FileName, line.LineNumber);
        } else {
            snprintf(buf, sizeof(buf), "    [%2i] 0x%p\n", num, stack.AddrPC.Offset);
        }

        ss << buf;
    }

    SymCleanup(GetCurrentProcess());
}

#elif defined(__linux__)

void print_callstack(Buffer &ss) {
    void *array[64];
    int size = backtrace(array, ARRAY_SIZE(array));
    char **messages = backtrace_symbols(array, size);

    // skip first stack frame (points here)
    for (int i = 1; i < size && messages != NULL; ++i) {
        char *msg = messages[i];
        char *mangled_name = strchr(msg, '(');
        char *offset_begin = strchr(msg, '+');
        char *offset_end = strchr(msg, ')');
        char *addr_begin = strchr(msg, '[');
        char *addr_end = strchr(msg, ']');

        char buf[512];

        // Attempt to demangle the symbol
        if (mangled_name && offset_begin && offset_end && mangled_name < offset_begin) {
            *mangled_name++ = '\0';
            *offset_begin++ = '\0';
            *offset_end++ = '\0';
            *addr_begin++ = '\0';
            *addr_end++ = '\0';

            int demangle_ok = 1;
            char *real_name = abi::__cxa_demangle(mangled_name, 0, 0, &demangle_ok);
            char line[256];
            memset(line, 0, sizeof(line));

            snprintf(buf,
                     sizeof(buf),
                     "    [%2d] %s: (%s)+%s in %s\n",
                     i,
                     msg,
                     demangle_ok ? real_name : mangled_name,
                     // mangled_name,
                     offset_begin,
                     addr2line(offset_begin, line, sizeof(line)));

            free(real_name);
        } else {
            snprintf(buf, sizeof(buf), "    [%2d] %s\n", i, msg);
        }

        ss << buf;
    }
    free(messages);
}

#else

#    warning "Function print_callstack is no-op for this platform"

#endif

void print_callstack() {
    fo::TempAllocator1024 ta(fo::memory_globals::default_allocator());
    fo::string_stream::Buffer ss(ta);

    print_callstack(ss);

    LOG_F(ERROR, "Callstack - \n%s", fo::string_stream::c_str(ss));
}

} // namespace eng
