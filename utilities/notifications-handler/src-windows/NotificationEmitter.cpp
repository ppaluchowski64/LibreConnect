#include <NotificationEmitter.h>
#include <ThreadPool.h>
#include <wintoastlib.h>
#include <DebugLog.h>

#include <mutex>
#include <thread>
#include <queue>
#include <future>
#include <functional>
#include <windows.h>

namespace {

static constexpr UINT WM_TOAST_TASK = WM_USER + 1;

class ToastDispatcher {
public:
    static ToastDispatcher& Instance() {
        static ToastDispatcher s_instance;
        return s_instance;
    }

    template<typename F>
    std::invoke_result_t<std::decay_t<F>> Dispatch(F&& f) {
        using R = std::invoke_result_t<std::decay_t<F>>;

        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();

        {
            std::lock_guard lock(m_queueMutex);
            m_queue.push([task]() { (*task)(); });
        }
        PostThreadMessageW(m_threadId, WM_TOAST_TASK, 0, 0);

        return fut.get();
    }

private:
    ToastDispatcher() {
        std::promise<DWORD> idPromise;
        auto idFuture = idPromise.get_future();

        m_thread = std::thread([this, &idPromise]() {
            if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
                Debug::LogError("CoInitializeEx failed");
                exit(-1);
            }

            MSG msg;
            PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

            idPromise.set_value(GetCurrentThreadId());
            RunMessageLoop();
            CoUninitialize();
        });

        m_threadId = idFuture.get();
    }

    ~ToastDispatcher() {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void RunMessageLoop() {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.hwnd == nullptr && msg.message == WM_TOAST_TASK) {
                DrainQueue();
            } else {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        DrainQueue();
    }

    void DrainQueue() {
        while (true) {
            std::function<void()> task;
            {
                std::lock_guard lock(m_queueMutex);
                if (m_queue.empty()) break;
                task = std::move(m_queue.front());
                m_queue.pop();
            }
            task();
        }
    }

    std::thread                        m_thread;
    DWORD                              m_threadId{0};
    std::mutex                         m_queueMutex;
    std::queue<std::function<void()>>  m_queue;
};

std::atomic<bool> g_toastInitialized{false};
std::atomic<bool> g_toastInitAttempted{false};

bool EnsureToastInitializedOnSTAThread() {
    if (g_toastInitialized.load(std::memory_order_acquire))
        return true;

    if (g_toastInitAttempted.exchange(true, std::memory_order_acq_rel))
        return g_toastInitialized.load(std::memory_order_acquire);

    WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

    constexpr const wchar_t* APPNAME = L"LibreConnect";
    instance->setAppName(APPNAME);
    const auto aumi = WinToastLib::WinToast::configureAUMI(L"Default", APPNAME, L"main", L"1.0");
    instance->setAppUserModelId(aumi);

    const bool ok = instance->initialize();
    if (!ok) {
        Debug::LogError("NotificationEmitter: WinToast initialization failed");
    }

    g_toastInitialized.store(ok, std::memory_order_release);
    return ok;
}

} // namespace

class NotificationHandler : public WinToastLib::IWinToastHandler {
public:
    explicit NotificationHandler(std::vector<NotificationEmitter::ButtonAction> buttons)
        : actions(std::move(buttons)) {}

    void toastActivated(const int actionIndex) const override {
        if (actionIndex < 0 || static_cast<size_t>(actionIndex) >= actions.size()) {
            return;
        }

        auto action = actions[static_cast<size_t>(actionIndex)].action;
        if (!action) {
            return;
        }

        ThreadPool::Post([action]() {
            try {
                action();
            } catch (const std::exception& e) {
                Debug::LogError("Exception during notification action: {}", e.what());
            } catch (...) {
                Debug::LogError("Unknown exception during notification action");
            }
        });
    }

    void toastActivated() const override {}
    void toastActivated(std::wstring response) const override {}
    void toastDismissed(WinToastDismissalReason reason) const override {}

    void toastFailed() const override {}

private:
    std::vector<NotificationEmitter::ButtonAction> actions;
};

int64_t NotificationEmitter::Emit(
    const std::wstring& notificationName,
    const std::wstring& notificationContent,
    const std::optional<std::filesystem::path>& appIconPath,
    const std::optional<std::filesystem::path>& mainImagePath,
    const std::vector<ButtonAction>& buttons)
{
    return ToastDispatcher::Instance().Dispatch([&]() -> int64_t {
        if (!EnsureToastInitializedOnSTAThread()) {
            return -1;
        }

        WinToastLib::WinToast* instance = WinToastLib::WinToast::instance();

        const WinToastLib::WinToastTemplate::WinToastTemplateType templateType = appIconPath
            ? WinToastLib::WinToastTemplate::ImageAndText02
            : WinToastLib::WinToastTemplate::Text02;

        WinToastLib::WinToastTemplate templ(templateType);
        templ.setTextField(notificationName,    WinToastLib::WinToastTemplate::FirstLine);
        templ.setTextField(notificationContent, WinToastLib::WinToastTemplate::SecondLine);

        if (appIconPath) {
            templ.setImagePath(appIconPath->wstring());
        }
        if (mainImagePath) {
            templ.setHeroImagePath(mainImagePath->wstring());
        }
        for (const auto& [name, _] : buttons) {
            templ.addAction(name);
        }

        const int64_t toastID = instance->showToast(templ, new NotificationHandler(buttons));
        if (toastID < 0) {
            Debug::LogWarning("NotificationEmitter: showToast failed with id {}", toastID);
        }

        return toastID;
    });
}

bool NotificationEmitter::RequestPermission() {
    return true;
}

bool NotificationEmitter::IsPermissionGranted() {
    return true;
}

void NotificationEmitter::Remove(const int64_t id) {
    if (id < 0) return;

    ToastDispatcher::Instance().Dispatch([id]() {
        if (!g_toastInitialized.load(std::memory_order_acquire)) return;
        WinToastLib::WinToast::instance()->hideToast(static_cast<INT64>(id));
    });
}
