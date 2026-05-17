#include <SmsBridgeModule.h>
#include <SmsBridgeEvents.h>
#include <SMS_Handler.h>
#include <TransferChannelPool.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <utility>

#ifdef ANDROID_DEVICE
#include <PermissionManager.h>
#include <QObject>
#endif

constexpr size_t FUTURES_WAIT_DELAY = 10;

#ifdef ANDROID_DEVICE
class SmsPermissionChangeGate {
public:
    explicit SmsPermissionChangeGate(asio::any_io_executor executor)
        : m_executor(std::move(executor)), m_timer(m_executor)
    {
        m_timer.expires_at(asio::steady_timer::time_point::max());
    }

    uint64_t Generation() const
    {
        return m_generation.load(std::memory_order_acquire);
    }

    void Signal()
    {
        asio::post(m_executor, [this]() {
            m_generation.fetch_add(1, std::memory_order_acq_rel);
            m_timer.cancel();
        });
    }

    asio::awaitable<void> WaitForChange(const uint64_t generation)
    {
        if (Generation() != generation) {
            co_return;
        }

        m_timer.expires_at(asio::steady_timer::time_point::max());
        asio::error_code ec;
        co_await m_timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }

private:
    asio::any_io_executor m_executor;
    asio::steady_timer m_timer;
    std::atomic<uint64_t> m_generation{0};
};

class SmsPermissionEventListener final : public QObject {
public:
    explicit SmsPermissionEventListener(std::function<void()> onSmsPermissionChanged)
        : m_onSmsPermissionChanged(std::move(onSmsPermissionChanged))
    {
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == ModuleRequestedPermission::Type) {
            const auto* permissionEvent = static_cast<ModuleRequestedPermission*>(event);
            if (permissionEvent->GetPermissionType() == PermissionType::Sms) {
                m_onSmsPermissionChanged();
                return true;
            }
        }

        if (event->type() == ModuleRequestedPermissionGranted::Type) {
            const auto* permissionEvent = static_cast<ModuleRequestedPermissionGranted*>(event);
            if (permissionEvent->GetPermissionType() == PermissionType::Sms) {
                m_onSmsPermissionChanged();
                return true;
            }
        }

        if (event->type() == ModuleRequestedPermissionRejected::Type) {
            const auto* permissionEvent = static_cast<ModuleRequestedPermissionRejected*>(event);
            if (permissionEvent->GetPermissionType() == PermissionType::Sms) {
                m_onSmsPermissionChanged();
                return true;
            }
        }

        return QObject::event(event);
    }

private:
    std::function<void()> m_onSmsPermissionChanged;
};
#else
class SmsPermissionChangeGate {};
class SmsPermissionEventListener {};
#endif

namespace {
#ifdef ANDROID_DEVICE
bool AreSmsBridgePermissionsGranted()
{
    return PermissionManager::IsReceiveSmsPermissionGranted() &&
        PermissionManager::IsReadContactsPermissionGranted() &&
        PermissionManager::IsReadSmsPermissionGranted() &&
        PermissionManager::IsSendSmsPermissionGranted();
}
#endif
}

extern "C" {
    JNIEXPORT void JNICALL Java_com_LibreConnect_mobile_SmsReceiver_onSmsReceivedCPP(JNIEnv* env, jobject, jstring sender, jstring body, jlong timestamp) {
        const char* senderChars = sender ? env->GetStringUTFChars(sender, nullptr) : nullptr;
        const char* bodyChars = body ? env->GetStringUTFChars(body, nullptr) : nullptr;

        std::string senderStr = senderChars ? std::string(senderChars) : std::string();
        std::string bodyStr = bodyChars ? std::string(bodyChars) : std::string();

        if (senderChars)
            env->ReleaseStringUTFChars(sender, senderChars);
        if (bodyChars)
            env->ReleaseStringUTFChars(body, bodyChars);

        Debug::Log("SmsBridgeModule: New SMS received from: {}", senderStr);
        ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_NEW_SMS_RECEIVED, senderStr, bodyStr, static_cast<int64_t>(timestamp));
    }
}


SmsBridgeModule::~SmsBridgeModule() = default;

void SmsBridgeModule::EnableResponseCallbacks() {
    const std::shared_ptr<SmsBridgeModule> instance = std::static_pointer_cast<SmsBridgeModule>(shared_from_this());

    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SmsBridgeModule: Received enable request");
        if (instance->GetModuleState() == ModuleState::Enabled) {
            Debug::Log("SmsBridgeModule: Already enabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, true);
            return;
        }
        instance->Enable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE, [instance](PC_Package&& package) mutable {
        Debug::Log("SmsBridgeModule: Received disable request");
        if (instance->GetModuleState() == ModuleState::Disabled) {
            Debug::Log("SmsBridgeModule: Already disabled, sending state confirmation");
            ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, false);
            return;
        }
        instance->Disable(true);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, [instance](PC_Package&& package) mutable {
        const bool peerEnabled = package->GetValue<bool>();
        Debug::Log("SmsBridgeModule: Peer module state changed: {}", peerEnabled);
        instance->m_peerModuleEnabled.store(peerEnabled);
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_CONTACTS_REQUEST, [instance](PC_Package&& package) mutable {
        const size_t requestID = package->GetValue<size_t>();
        Debug::Log("SmsBridgeModule: Received fetch contacts request. RequestID: {}", requestID);
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_CONTACTS_RESPONSE, SmsUtilsWrapper::GetContactList());
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_MESSAGES_REQUEST, [instance](PC_Package&& package) mutable {
        const size_t requestID   = package->GetValue<size_t>();
        const std::string target = package->GetValue<std::string>();
        Debug::Log("SmsBridgeModule: Received fetch messages request for {}. RequestID: {}", target, requestID);
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_MESSAGES_RESPONSE, SmsUtilsWrapper::GetMessagesFromNumber(target));
    });
    ConnectionManager::AddResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_SEND_SMS_REQUEST, [instance](PC_Package&& package) mutable {
        const size_t requestID   = package->GetValue<size_t>();
        const std::string target = package->GetValue<std::string>();
        const std::string body   = package->GetValue<std::string>();

        Debug::Log("SmsBridgeModule: Received send SMS request to {}. RequestID: {}", target, requestID);
        const bool result = SmsUtilsWrapper::SendSMS(target, body);
        Debug::Log("SmsBridgeModule: Send SMS to {} result: {}", target, result);
        ConnectionManager::SendRequestResponse(requestID, PC_PackageType::SMS_BRIDGE_MODULE_SEND_SMS_RESPONSE, result);
    });
    ConnectionManager::AddAwaitableResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_REQUEST, [instance](PC_Package&& package) mutable -> asio::awaitable<void> {
        const size_t requestID   = package->GetValue<size_t>();
        const std::string target = package->GetValue<std::string>();
        std::string path{};

#ifdef ANDROID_DEVICE
        const QJniObject context = QNativeInterface::QAndroidApplication::context();
        const QJniObject jUri = QJniObject::fromString(QString::fromStdString(target));

        const QJniObject result = QJniObject::callStaticObjectMethod(
            "com/LibreConnect/mobile/SmsUtils",
            "saveAttachmentToCache",
            "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
            context.object(),
            jUri.object<jstring>()
        );

        if (result.isValid()) {
            path = result.toString().toStdString();
        }
#endif

        if (path.empty()) {
            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_RESPONSE,
                false,
                size_t{0},
                std::string{}
            );
            co_return;
        }

        const std::filesystem::path attachmentPath(path);
        if (!std::filesystem::exists(attachmentPath) || !std::filesystem::is_regular_file(attachmentPath)) {
            Debug::LogError("SmsBridgeModule: Cached MMS attachment is missing: {}", path);
            ConnectionManager::SendRequestResponse(
                requestID,
                PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_RESPONSE,
                false,
                size_t{0},
                std::string{}
            );
            co_return;
        }

        const BorrowedTransferChannel acquiredChannel = co_await TransferChannelPool::BorrowTransferChannel();
        const size_t transferChannelIndex = acquiredChannel.index;
        const std::shared_ptr<TransferChannel> channel = acquiredChannel.channel;
        const std::string fileName = attachmentPath.filename().string();

        ConnectionManager::SendRequestResponse(
            requestID,
            PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_RESPONSE,
            true,
            transferChannelIndex,
            fileName
        );

        co_await channel->SendFile(attachmentPath);
    });
}

void SmsBridgeModule::DisableResponseCallbacks() {
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_CONTACTS_REQUEST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_FETCH_ALL_MESSAGES_REQUEST);
    ConnectionManager::RemoveResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_SEND_SMS_REQUEST);
    ConnectionManager::RemoveAwaitableResponseHandler(PC_PackageType::SMS_BRIDGE_MODULE_MMS_FILE_CONTENT_REQUEST);
}

void SmsBridgeModule::OnInitialize() {
#ifdef ANDROID_DEVICE
    if (!m_smsPermissionGate) {
        m_smsPermissionGate = std::make_shared<SmsPermissionChangeGate>(m_moduleStrand);
    }

    if (!m_smsPermissionEventListener) {
        m_smsPermissionEventListener = std::make_shared<SmsPermissionEventListener>([this]() {
            if (m_smsPermissionGate) {
                m_smsPermissionGate->Signal();
            }
        });
        ConnectionManager::AddEventListener(QPointer<QObject>(m_smsPermissionEventListener.get()));
    }
#endif
}

asio::awaitable<void> SmsBridgeModule::OnEnable() {
    asio::steady_timer timer(m_context.get_executor());

#ifdef ANDROID_DEVICE
    while (!AreSmsBridgePermissionsGranted()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        if (!m_smsPermissionRequestAnnounced) {
            ConnectionManager::Send(PC_PackageType::PERMISSION_REQUESTED, PermissionType::Sms);
            m_smsPermissionRequestAnnounced = true;
        }

        const uint64_t generation = m_smsPermissionGate ? m_smsPermissionGate->Generation() : 0;
        if (AreSmsBridgePermissionsGranted()) {
            break;
        }

        if (!m_smsPermissionGate) {
            Disable();
            co_return;
        }

        co_await m_smsPermissionGate->WaitForChange(generation);
    }

    if (ShouldAbortEnable()) {
        co_return;
    }

    ConnectionManager::Send(PC_PackageType::PERMISSION_GRANTED, PermissionType::Sms);
#endif

    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_ENABLE);
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, true);

    while (!ShouldAbortEnable()) {
        if (ShouldAbortEnable()) {
            co_return;
        }

        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }

    co_return;
}

asio::awaitable<void> SmsBridgeModule::OnDisable() {
#ifdef ANDROID_DEVICE
    m_smsPermissionRequestAnnounced = false;
    if (m_smsPermissionGate) {
        m_smsPermissionGate->Signal();
    }
#endif
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_DISABLE);
    ConnectionManager::Send(PC_PackageType::SMS_BRIDGE_MODULE_STATE_CHANGED, false);
    co_return;
}

asio::awaitable<void> SmsBridgeModule::OnShutdown() {
    co_return;
}

const char* SmsBridgeModule::GetModuleName() const {
    return "SmsBridgeModule";
}

ModuleType SmsBridgeModule::GetModuleType() const {
    return ModuleType::SmsBridge;
}
