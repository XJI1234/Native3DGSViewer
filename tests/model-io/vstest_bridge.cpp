#include <CppUnitTest.h>
#include <Windows.h>

#include <filesystem>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

extern "C" IMAGE_DOS_HEADER __ImageBase;

TEST_CLASS(ModelIoGoogleTestBridge){
    public : TEST_METHOD(RunCompleteGoogleTestSuite){wchar_t modulePath[32768]{};
const DWORD size = GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath,
                                      static_cast<DWORD>(std::size(modulePath)));
Assert::IsTrue(size > 0 && size < std::size(modulePath));
const auto executable =
    std::filesystem::path(modulePath).parent_path() / L"Native3DGSViewer.Tests.exe";
std::wstring args = L"\"" + executable.wstring() + L"\" --gtest_brief=1";
STARTUPINFOW startup{sizeof(startup)};
PROCESS_INFORMATION process{};
const BOOL started = CreateProcessW(executable.c_str(), args.data(), nullptr, nullptr, FALSE,
                                    CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
Assert::IsTrue(started != FALSE, L"Cannot start GoogleTest executable");
CloseHandle(process.hThread);
const DWORD wait = WaitForSingleObject(process.hProcess, 240000);
DWORD code = 1;
if (wait == WAIT_TIMEOUT)
    TerminateProcess(process.hProcess, 1);
if (wait == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &code);
CloseHandle(process.hProcess);
Assert::AreEqual(DWORD(0), code, L"GoogleTest suite failed");
}
}
;
