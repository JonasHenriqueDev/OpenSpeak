#include "WindowGrabber.h"

#include <QAbstractVideoBuffer>
#include <QDebug>
#include <QTimer>

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace openspeak {

namespace {

constexpr UINT kPrintWindowFullContent = 0x2; // PW_RENDERFULLCONTENT (Windows 8.1+)
constexpr ULONGLONG kOcclusionIntervalMs = 250;

RECT frameBounds(HWND hwnd)
{
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    return r;
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

// Alguma janela acima de hwnd na ordem Z cobre parte dela? Ignora overlays que
// deixam o clique passar (WS_EX_TRANSPARENT) e janelas totalmente transparentes.
bool isOccluded(HWND hwnd, const RECT &bounds)
{
    int guard = 0;
    for (HWND w = GetWindow(hwnd, GW_HWNDPREV); w && guard < 2000; w = GetWindow(w, GW_HWNDPREV), ++guard) {
        if (!IsWindowVisible(w) || IsIconic(w) || isCloaked(w))
            continue;
        const auto ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        if (ex & WS_EX_TRANSPARENT)
            continue;
        if (ex & WS_EX_LAYERED) {
            BYTE alpha = 255;
            DWORD flags = 0;
            if (GetLayeredWindowAttributes(w, nullptr, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0)
                continue;
        }
        const RECT r = frameBounds(w);
        RECT hit;
        if (IntersectRect(&hit, &r, &bounds))
            return true;
    }
    return false;
}

struct Dib {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    quint32 *bits = nullptr;
    int w = 0, h = 0;

    Dib() = default;
    Dib(const Dib &) = delete;
    Dib &operator=(const Dib &) = delete;
    ~Dib() { reset(); }

    void reset()
    {
        if (dc) {
            SelectObject(dc, old);
            DeleteDC(dc);
        }
        if (bmp)
            DeleteObject(bmp);
        dc = nullptr;
        bmp = nullptr;
        old = nullptr;
        bits = nullptr;
        w = h = 0;
    }
    bool ensure(int width, int height)
    {
        if (dc && w == width && h == height)
            return true;
        reset();
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = width;
        bi.bmiHeader.biHeight = -height; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *p = nullptr;
        bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
        if (!bmp)
            return false;
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bmp);
        bits = static_cast<quint32 *>(p);
        w = width;
        h = height;
        return true;
    }
};

// Buffers BGRX reaproveitados: o QVideoFrame devolve o seu ao pool quando o último
// consumidor (encoder, preview, miniatura) o solta. Sem alocação grande por quadro.
class FramePool : public std::enable_shared_from_this<FramePool> {
public:
    QVideoFrame acquire(int w, int h)
    {
        std::vector<uchar> mem;
        {
            std::lock_guard lock(m_mutex);
            if (m_w != w || m_h != h) {
                m_free.clear();
                m_w = w;
                m_h = h;
            }
            if (!m_free.empty()) {
                mem = std::move(m_free.back());
                m_free.pop_back();
            }
        }
        mem.resize(size_t(w) * h * 4);
        return QVideoFrame(std::make_unique<Buffer>(weak_from_this(), std::move(mem), w, h));
    }

private:
    class Buffer : public QAbstractVideoBuffer {
    public:
        Buffer(std::weak_ptr<FramePool> pool, std::vector<uchar> mem, int w, int h)
            : m_pool(std::move(pool)), m_mem(std::move(mem)), m_w(w), m_h(h) {}
        ~Buffer() override
        {
            if (auto pool = m_pool.lock())
                pool->release(std::move(m_mem), m_w, m_h);
        }
        MapData map(QVideoFrame::MapMode) override
        {
            MapData d;
            d.planeCount = 1;
            d.bytesPerLine[0] = m_w * 4;
            d.data[0] = m_mem.data();
            d.dataSize[0] = int(m_mem.size());
            return d;
        }
        QVideoFrameFormat format() const override
        {
            return QVideoFrameFormat(QSize(m_w, m_h), QVideoFrameFormat::Format_BGRX8888);
        }

    private:
        std::weak_ptr<FramePool> m_pool;
        std::vector<uchar> m_mem;
        int m_w, m_h;
    };

    void release(std::vector<uchar> mem, int w, int h)
    {
        std::lock_guard lock(m_mutex);
        if (w == m_w && h == m_h && m_free.size() < 8)
            m_free.push_back(std::move(mem));
    }

    std::mutex m_mutex;
    std::vector<std::vector<uchar>> m_free;
    int m_w = 0, m_h = 0;
};

} // namespace

struct WindowGrabber::Impl {
    enum class Method { None, Duplication, PrintWindow, Paused };

    HWND hwnd;
    int fps;
    Dib out;   // quadro final (área visível da janela)
    Dib print; // destino do PrintWindow (inclui as bordas invisíveis de redimensionamento)
    std::shared_ptr<FramePool> pool = std::make_shared<FramePool>();
    QVideoFrame lastFrame;
    Method lastMethod = Method::None;
    int blackStreak = 0;
    bool closed = false;
    bool occluded = false;
    ULONGLONG occlusionCheckedAt = 0;

    // Duplicação da tela do monitor onde está a maior parte da janela.
    HMONITOR dupMonitor = nullptr;
    RECT dupDesktop{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> dupl;
    ComPtr<ID3D11Texture2D> desktopCopy; // último quadro do monitor (GPU), para WAIT_TIMEOUT
    ComPtr<ID3D11Texture2D> crop;        // recorte da janela (GPU); base para o zero-copy futuro
    ComPtr<ID3D11Texture2D> staging;     // recorte copiado para a CPU
    int cropW = 0, cropH = 0;
    bool hasDesktop = false;
    ULONGLONG dupRetryAt = 0;

    HCURSOR cursor = nullptr;
    POINT hotspot{};

    Impl(HWND h, int f) : hwnd(h), fps(f) {}

    void resetDuplication()
    {
        dupl.Reset();
        desktopCopy.Reset();
        crop.Reset();
        staging.Reset();
        context.Reset();
        device.Reset();
        dupMonitor = nullptr;
        cropW = cropH = 0;
        hasDesktop = false;
    }

    bool ensureDuplication(HMONITOR monitor)
    {
        if (dupl && dupMonitor == monitor)
            return true;
        if (GetTickCount64() < dupRetryAt)
            return false;
        resetDuplication();
        dupRetryAt = GetTickCount64() + 1000; // em caso de falha (UAC, tela bloqueada), tenta de novo em 1 s

        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            return false;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC desc{};
                if (FAILED(output->GetDesc(&desc)) || desc.Monitor != monitor)
                    continue;
                ComPtr<IDXGIOutput1> output1;
                if (FAILED(output.As(&output1)))
                    return false;
                if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                             nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)))
                    return false;
                if (FAILED(output1->DuplicateOutput(device.Get(), &dupl))) {
                    resetDuplication();
                    return false;
                }
                DXGI_OUTDUPL_DESC dd{};
                dupl->GetDesc(&dd);
                if (dd.Rotation != DXGI_MODE_ROTATION_IDENTITY && dd.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
                    resetDuplication(); // monitor girado: deixa para o PrintWindow
                    return false;
                }
                dupMonitor = monitor;
                dupDesktop = desc.DesktopCoordinates;
                dupRetryAt = 0;
                return true;
            }
        }
        return false;
    }

    bool ensureTexture(ComPtr<ID3D11Texture2D> &tex, int w, int h, D3D11_USAGE usage, UINT cpu)
    {
        D3D11_TEXTURE2D_DESC td{};
        desktopCopy->GetDesc(&td);
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.Usage = usage;
        td.BindFlags = 0;
        td.CPUAccessFlags = cpu;
        td.MiscFlags = 0;
        tex.Reset();
        return SUCCEEDED(device->CreateTexture2D(&td, nullptr, &tex));
    }

    // Copia para out a parte da janela (b) que está no monitor duplicado. O resto
    // (janela atravessando outro monitor) fica preto; o tamanho do quadro não muda.
    bool grabDuplication(const RECT &b)
    {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const HRESULT hr = dupl->AcquireNextFrame(0, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            if (!hasDesktop)
                return false; // nada mudou desde a criação; o PrintWindow cobre este quadro
        } else if (FAILED(hr)) {
            qInfo() << "WindowGrabber: duplicação perdida (" << Qt::hex << hr << "), recriando";
            resetDuplication(); // ACCESS_LOST: resolução, UAC, tela bloqueada, fullscreen...
            return false;
        } else {
            ComPtr<ID3D11Texture2D> frame;
            resource.As(&frame);
            if (frame && (info.AccumulatedFrames > 0 || !hasDesktop)) {
                if (!desktopCopy) {
                    D3D11_TEXTURE2D_DESC td{};
                    frame->GetDesc(&td);
                    td.Usage = D3D11_USAGE_DEFAULT;
                    td.BindFlags = 0;
                    td.CPUAccessFlags = 0;
                    td.MiscFlags = 0;
                    device->CreateTexture2D(&td, nullptr, &desktopCopy);
                }
                if (desktopCopy) {
                    context->CopyResource(desktopCopy.Get(), frame.Get());
                    hasDesktop = true;
                }
            }
            dupl->ReleaseFrame();
            if (!hasDesktop)
                return false;
        }

        RECT part;
        if (!IntersectRect(&part, &b, &dupDesktop))
            return false;
        const int w = part.right - part.left, h = part.bottom - part.top;
        if (!crop || cropW != w || cropH != h) {
            if (!ensureTexture(crop, w, h, D3D11_USAGE_DEFAULT, 0) ||
                !ensureTexture(staging, w, h, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ))
                return false;
            cropW = w;
            cropH = h;
        }
        const D3D11_BOX box{UINT(part.left - dupDesktop.left), UINT(part.top - dupDesktop.top), 0,
                            UINT(part.right - dupDesktop.left), UINT(part.bottom - dupDesktop.top), 1};
        context->CopySubresourceRegion(crop.Get(), 0, 0, 0, 0, desktopCopy.Get(), 0, &box);
        context->CopyResource(staging.Get(), crop.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;
        GdiFlush();
        if (w != out.w || h != out.h)
            std::memset(out.bits, 0, size_t(out.w) * out.h * 4);
        const int dx = part.left - b.left, dy = part.top - b.top;
        for (int y = 0; y < h; ++y)
            std::memcpy(out.bits + size_t(y + dy) * out.w + dx,
                        static_cast<const uchar *>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(w) * 4);
        context->Unmap(staging.Get(), 0);
        return true;
    }

    bool grabPrintWindow(const RECT &b)
    {
        RECT wr{};
        GetWindowRect(hwnd, &wr);
        if (!print.ensure(wr.right - wr.left, wr.bottom - wr.top))
            return false;
        if (!PrintWindow(hwnd, print.dc, kPrintWindowFullContent))
            return false;
        BitBlt(out.dc, 0, 0, out.w, out.h, print.dc, b.left - wr.left, b.top - wr.top, SRCCOPY);
        GdiFlush();
        return true;
    }

    bool outIsBlack() const
    {
        for (int sy = 0; sy < 16; ++sy)
            for (int sx = 0; sx < 16; ++sx) {
                const int x = (out.w - 1) * sx / 15, y = (out.h - 1) * sy / 15;
                if (out.bits[size_t(y) * out.w + x] & 0x00FFFFFF)
                    return false;
            }
        return true;
    }

    // Nem a duplicação nem o PrintWindow incluem o cursor. DrawIconEx desenha qualquer
    // tipo de cursor (monocromático, colorido, com máscara) nos dois modos.
    void drawCursor(const RECT &b)
    {
        CURSORINFO ci{sizeof(ci)};
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor || !PtInRect(&b, ci.ptScreenPos))
            return;
        if (GetAncestor(WindowFromPoint(ci.ptScreenPos), GA_ROOT) != hwnd)
            return;
        if (ci.hCursor != cursor) {
            ICONINFO ii{};
            if (!GetIconInfo(ci.hCursor, &ii))
                return;
            if (ii.hbmMask)
                DeleteObject(ii.hbmMask);
            if (ii.hbmColor)
                DeleteObject(ii.hbmColor);
            cursor = ci.hCursor;
            hotspot = {LONG(ii.xHotspot), LONG(ii.yHotspot)};
        }
        DrawIconEx(out.dc, ci.ptScreenPos.x - hotspot.x - b.left, ci.ptScreenPos.y - hotspot.y - b.top, ci.hCursor,
                   0, 0, 0, nullptr, DI_NORMAL);
        GdiFlush();
    }

    QVideoFrame toFrame()
    {
        QVideoFrame frame = pool->acquire(out.w, out.h);
        if (!frame.map(QVideoFrame::WriteOnly))
            return {};
        std::memcpy(frame.bits(0), out.bits, size_t(out.w) * out.h * 4);
        frame.unmap();
        return frame;
    }
};

WindowGrabber::WindowGrabber(quintptr hwnd, int targetFps, QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(reinterpret_cast<HWND>(hwnd), std::max(1, targetFps))),
      m_fps(std::max(1, targetFps))
{
    m_thread.setObjectName(QStringLiteral("WindowGrabber"));
}

WindowGrabber::~WindowGrabber() { stop(); }

void WindowGrabber::start()
{
    if (m_context || !m_impl)
        return;
    m_context = new QObject;
    m_context->moveToThread(&m_thread);
    m_thread.start();
    QMetaObject::invokeMethod(m_context, [this] {
        // Coordenadas em pixels físicos em qualquer monitor, independente da escala.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        m_timer = new QTimer(m_context);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, m_context, [this] { tick(); });
        m_timer->start(1000 / m_fps);
        tick(); // primeiro quadro imediato (miniaturas)
    });
}

void WindowGrabber::stop()
{
    if (!m_context)
        return;
    QMetaObject::invokeMethod(m_context, [this] {
        delete m_timer;
        m_timer = nullptr;
        m_impl.reset(); // libera D3D/GDI na própria thread
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_context;
    m_context = nullptr;
}

void WindowGrabber::tick()
{
    Impl &d = *m_impl;
    if (d.closed)
        return;
    if (!IsWindow(d.hwnd)) {
        d.closed = true;
        m_timer->stop();
        emit windowClosed();
        return;
    }
    if (IsIconic(d.hwnd)) {
        // Minimizada: mantém o modo e reenvia o último quadro para a transmissão não parar.
        if (d.lastMethod != Impl::Method::Paused) {
            d.lastMethod = Impl::Method::Paused;
            emit methodChanged(QStringLiteral("Pausada (janela minimizada)"));
        }
        if (d.lastFrame.isValid())
            emit frameReady(d.lastFrame);
        return;
    }

    const RECT b = frameBounds(d.hwnd);
    const int w = b.right - b.left, h = b.bottom - b.top;
    if (w <= 0 || h <= 0 || !d.out.ensure(w, h))
        return;

    const ULONGLONG now = GetTickCount64();
    if (now - d.occlusionCheckedAt >= kOcclusionIntervalMs) {
        d.occlusionCheckedAt = now;
        d.occluded = isOccluded(d.hwnd, b);
    }

    // A) duplicação do monitor com a maior parte da janela; B) PrintWindow.
    bool ok = false;
    auto method = Impl::Method::PrintWindow;
    if (!d.occluded && d.ensureDuplication(MonitorFromWindow(d.hwnd, MONITOR_DEFAULTTONEAREST))) {
        ok = d.grabDuplication(b);
        if (ok && !d.outIsBlack())
            method = Impl::Method::Duplication;
        else
            ok = false; // preto ou falhou: tenta o PrintWindow neste mesmo quadro
    }
    if (!ok)
        ok = d.grabPrintWindow(b);
    if (!ok)
        return;

    // C) preto nos dois modos por ~1 s: PW_RENDERFULLCONTENT não funciona para esta janela.
    if (method == Impl::Method::PrintWindow && d.outIsBlack()) {
        if (++d.blackStreak >= m_fps) {
            d.closed = true;
            m_timer->stop();
            qInfo() << "WindowGrabber: duplicação e PrintWindow só devolvem preto há 1 s; voltando para o Windows.Graphics.Capture";
            emit fallbackRequested();
            return;
        }
    } else {
        d.blackStreak = 0;
    }

    if (method != d.lastMethod) {
        d.lastMethod = method;
        const QString name = method == Impl::Method::Duplication ? QStringLiteral("Duplicação de tela (DXGI)")
                                                                 : QStringLiteral("PrintWindow");
        qInfo() << "WindowGrabber:" << name;
        emit methodChanged(name);
    }
    d.drawCursor(b);
    d.lastFrame = d.toFrame();
    if (d.lastFrame.isValid())
        emit frameReady(d.lastFrame);
}

} // namespace openspeak
