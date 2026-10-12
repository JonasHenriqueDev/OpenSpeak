#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Process {
    HANDLE handle = nullptr;
    Process() = default;
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    Process(Process &&other) noexcept : handle(other.handle) { other.handle = nullptr; }
    Process &operator=(Process &&other) noexcept {
        if (handle) CloseHandle(handle);
        handle = other.handle;
        other.handle = nullptr;
        return *this;
    }
    ~Process() { if (handle) CloseHandle(handle); }
    DWORD wait() const {
        WaitForSingleObject(handle, INFINITE);
        DWORD result = 1;
        GetExitCodeProcess(handle, &result);
        return result;
    }
};

static Process launch(const fs::path &exe, const std::wstring &args, const fs::path &cwd, bool hidden = false)
{
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        hidden ? CREATE_NO_WINDOW : 0, nullptr, cwd.c_str(), &startup, &info))
        throw std::runtime_error("Não foi possível iniciar um componente do OpenSpeak.");
    CloseHandle(info.hThread);
    Process result;
    result.handle = info.hProcess;
    return result;
}

int WINAPI wWinMain(HINSTANCE module, HINSTANCE, PWSTR arguments, int)
{
    fs::path root;
    bool ownsRoot = false;
    Process testServer;
    int result = 1;
    try {
        wchar_t temp[MAX_PATH + 1]{};
        if (!GetTempPathW(MAX_PATH, temp)) throw std::runtime_error("Pasta temporária indisponível.");
        GUID id{};
        if (FAILED(CoCreateGuid(&id))) throw std::runtime_error("Não foi possível criar a sessão portátil.");
        wchar_t guid[40]{};
        StringFromGUID2(id, guid, 40);
        root = fs::path(temp) / (std::wstring(L"OpenSpeak-") + guid);
        if (!fs::create_directory(root)) throw std::runtime_error("Não foi possível criar a pasta portátil.");
        ownsRoot = true;
        const auto resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
        const auto size = SizeofResource(module, resource);
        const auto data = LockResource(LoadResource(module, resource));
        if (!resource || !data || !size) throw std::runtime_error("Pacote portátil inválido.");
        {
            std::ofstream zip(root / L"payload.zip", std::ios::binary);
            zip.write(static_cast<const char *>(data), size);
            if (!zip) throw std::runtime_error("Não foi possível extrair o pacote portátil.");
        }
        // No user-controlled paths are interpolated into PowerShell source.
        {
            std::ofstream script(root / L"expand.ps1");
            script << "$ErrorActionPreference='Stop'\n"
                      "Expand-Archive -LiteralPath (Join-Path $PSScriptRoot 'payload.zip') -DestinationPath $PSScriptRoot\n";
        }
        wchar_t system[MAX_PATH + 1]{};
        GetSystemDirectoryW(system, MAX_PATH);
        auto extractor = launch(fs::path(system) / L"WindowsPowerShell/v1.0/powershell.exe",
            L"-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + (root / L"expand.ps1").wstring() + L"\"", root, true);
        if (extractor.wait()) throw std::runtime_error("A extração das dependências falhou.");
        fs::remove(root / L"payload.zip");
        const fs::path app = root / L"OpenSpeak";
        std::wstring args(arguments);
        if (args == L"--local-test") {
            testServer = launch(app / L"openspeak-server.exe", L"--host 127.0.0.1 --port 19988", app, true);
            if (WaitForSingleObject(testServer.handle, 500) != WAIT_TIMEOUT)
                throw std::runtime_error("A porta do servidor de teste está ocupada.");
            // Opening the windows gives the local server time to start. Users
            // can click Connect again if their machine starts it more slowly.
            auto sender = launch(app / L"OpenSpeak.exe",
                L"--profile portable-emissor --nickname Emissor --server ws://localhost:19988/ws --autoconnect", app);
            auto viewer = launch(app / L"OpenSpeak.exe",
                L"--profile portable-espectador --nickname Espectador --server ws://localhost:19988/ws --autoconnect", app);
            sender.wait();
            viewer.wait();
            result = 0;
        } else {
            const bool serverOnly = args.starts_with(L"--server-only");
            if (serverOnly) args.erase(0, std::wstring(L"--server-only").size());
            auto child = launch(app / (serverOnly ? L"openspeak-server.exe" : L"OpenSpeak.exe"), args, app, serverOnly);
            result = static_cast<int>(child.wait());
        }
    } catch (const std::exception &) {
        MessageBoxW(nullptr, L"Não foi possível abrir o OpenSpeak portátil. Verifique o espaço livre na pasta temporária e tente novamente.",
                    L"OpenSpeak", MB_OK | MB_ICONERROR);
    }
    if (testServer.handle) {
        TerminateProcess(testServer.handle, 0); // only the server started by this test session
        testServer.wait();
    }
    if (ownsRoot) {
        // This exact, randomly-created directory is owned by this invocation.
        std::error_code error;
        fs::remove_all(root, error);
    }
    return result;
}
