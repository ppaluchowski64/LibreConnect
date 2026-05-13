#include <SmsBridgeModule.h>
#include <SmsBridgeEvents.h>
#include <SMS_Handler.h>
#include <TransferChannelPool.h>

#include <cstdint>
#include <filesystem>

#ifdef ANDROID_DEVICE
#include <PermissionManager.h>
#endif

constexpr size_t FUTURES_WAIT_DELAY = 10;

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

void SmsBridgeModule::OnInitialize() {}

asio::awaitable<void> SmsBridgeModule::OnEnable() {
    asio::steady_timer timer(m_context.get_executor());

#ifdef ANDROID_DEVICE
    while (!ShouldAbortEnable()) {
        const bool permissionsGranted =
            PermissionManager::IsReceiveSmsPermissionGranted() &&
            PermissionManager::IsReadContactsPermissionGranted() &&
            PermissionManager::IsReadSmsPermissionGranted() &&
            PermissionManager::IsSendSmsPermissionGranted();

        if (permissionsGranted) {
            break;
        }

        timer.expires_after(std::chrono::milliseconds(FUTURES_WAIT_DELAY));
        co_await timer.async_wait();
    }

    if (ShouldAbortEnable()) {
        co_return;
    }
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
