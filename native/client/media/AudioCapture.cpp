#include "AudioCapture.h"

#include <QMediaDevices>

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#pragma comment(lib, "mmdevapi.lib")

using Microsoft::WRL::ComPtr;

namespace openspeak {

namespace {

class ActivationHandler
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler() { done = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~ActivationHandler() override { CloseHandle(done); }

    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *op) override
    {
        ComPtr<IUnknown> unk;
        HRESULT activateResult = E_FAIL;
        hr = op->GetActivateResult(&activateResult, &unk);
        if (SUCCEEDED(hr))
            hr = activateResult;
        if (SUCCEEDED(hr))
            hr = unk.As(&client);
        SetEvent(done);
        return S_OK;
    }

    HANDLE done = nullptr;
    HRESULT hr = E_FAIL;
    ComPtr<IAudioClient> client;
};

struct WindowSearch {
    std::wstring title;
    DWORD pid = 0;
};

BOOL CALLBACK findWindow(HWND hwnd, LPARAM param)
{
    auto *search = reinterpret_cast<WindowSearch *>(param);
    wchar_t buffer[512];
    const int len = GetWindowTextW(hwnd, buffer, 512);
    if (len > 0 && IsWindowVisible(hwnd) && search->title == std::wstring(buffer, len)) {
        GetWindowThreadProcessId(hwnd, &search->pid);
        return FALSE;
    }
    return TRUE;
}

} // namespace

AudioCapture::AudioCapture(QObject *parent) : QObject(parent) {}

AudioCapture::~AudioCapture() { stop(); }

quint32 AudioCapture::pidForWindowTitle(const QString &title)
{
    WindowSearch search{title.toStdWString()};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
    return search.pid;
}

bool AudioCapture::start(Mode mode, quint32 pid)
{
    stop();
    m_running = true;
    if (mode == Mode::Microphone) {
        const QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            m_running = false;
            emit errorOccurred(QStringLiteral("nenhum microfone"));
            return false;
        }
        QAudioFormat fmt;
        fmt.setSampleRate(SampleRate);
        fmt.setChannelCount(Channels);
        fmt.setSampleFormat(QAudioFormat::Float);
        if (!device.isFormatSupported(fmt)) {
            fmt = device.preferredFormat();
            fmt.setSampleFormat(QAudioFormat::Float);
        }
        m_mic = std::make_unique<QAudioSource>(device, fmt);
        m_micDevice = m_mic->start();
        const int rate = fmt.sampleRate(), channels = fmt.channelCount();
        connect(m_micDevice, &QIODevice::readyRead, this, [this, rate, channels] {
            const QByteArray data = m_micDevice->readAll();
            if (!data.isEmpty())
                emit samplesAvailable(data, rate, channels);
        });
        return true;
    }
    m_thread = std::thread(&AudioCapture::loopbackThread, this, mode, pid);
    return true;
}

void AudioCapture::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    if (m_mic) {
        m_mic->stop();
        m_mic.reset();
        m_micDevice = nullptr;
    }
}

void AudioCapture::loopbackThread(Mode mode, quint32 pid)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto fail = [this](const QString &what, HRESULT hr) {
        emit errorOccurred(QStringLiteral("%1 (0x%2)").arg(what).arg(quint32(hr), 8, 16, QLatin1Char('0')));
    };

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = mode == Mode::SystemExcludingSelf ? GetCurrentProcessId() : pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = mode == Mode::SystemExcludingSelf
        ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
        : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE *>(&params);

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    ComPtr<IActivateAudioInterfaceAsyncOperation> op;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                             handler.Get(), &op);
    if (FAILED(hr) || WaitForSingleObject(handler->done, 5000) != WAIT_OBJECT_0 || FAILED(handler->hr)) {
        fail(QStringLiteral("falha ao ativar captura de áudio"), FAILED(hr) ? hr : handler->hr);
        CoUninitialize();
        return;
    }
    ComPtr<IAudioClient> client = handler->client;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = Channels;
    fmt.nSamplesPerSec = SampleRate;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            200'000 /* 20 ms */, 0, &fmt, nullptr);
    ComPtr<IAudioCaptureClient> capture;
    if (SUCCEEDED(hr))
        hr = client->SetEventHandle(event);
    if (SUCCEEDED(hr))
        hr = client->GetService(IID_PPV_ARGS(&capture));
    if (SUCCEEDED(hr))
        hr = client->Start();
    if (FAILED(hr)) {
        fail(QStringLiteral("falha ao iniciar captura de áudio"), hr);
        CloseHandle(event);
        CoUninitialize();
        return;
    }

    while (m_running) {
        if (WaitForSingleObject(event, 100) != WAIT_OBJECT_0)
            continue;
        UINT32 frames = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&frames)) && frames > 0) {
            BYTE *data = nullptr;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;
            QByteArray pcm(int(frames * fmt.nBlockAlign), Qt::Uninitialized);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                pcm.fill(0);
            else
                memcpy(pcm.data(), data, pcm.size());
            capture->ReleaseBuffer(frames);
            emit samplesAvailable(pcm, SampleRate, Channels);
        }
    }
    client->Stop();
    CloseHandle(event);
    CoUninitialize();
}

} // namespace openspeak
