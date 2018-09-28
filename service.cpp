#define LOG_TAG "hwservicemanager"

#include <utils/Log.h>

#include <inttypes.h>
#include <unistd.h>

#include <android/hidl/manager/1.1/BnHwServiceManager.h>
#include <android/hidl/token/1.0/ITokenManager.h>
#include <cutils/properties.h>
#include <hidl/Status.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Errors.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

#include "ServiceManager.h"
#include "TokenManager.h"

// libutils:
using android::sp;
using android::Looper;
using android::LooperCallback;

// libhwbinder:
using android::hardware::IPCThreadState;
using android::hardware::ProcessState;

// libhidl
using android::hardware::handleTransportPoll;
using android::hardware::setupTransportPolling;

// hidl types
using android::hidl::manager::V1_1::BnHwServiceManager;
using android::hidl::token::V1_0::ITokenManager;

// implementations
using android::hidl::manager::implementation::ServiceManager;
using android::hidl::token::V1_0::implementation::TokenManager;

static std::string serviceName = "default";

class HwBinderCallback : public LooperCallback {
public:
    static sp<HwBinderCallback> setupTo(const sp<Looper>& looper) {
        sp<HwBinderCallback> cb = new HwBinderCallback;

        int fdHwBinder = setupTransportPolling();
        LOG_ALWAYS_FATAL_IF(fdHwBinder < 0, "Failed to setupTransportPolling: %d", fdHwBinder);

        // Flush after setupPolling(), to make sure the binder driver
        // knows about this thread handling commands.
        IPCThreadState::self()->flushCommands();

        int ret = looper->addFd(fdHwBinder,
                                Looper::POLL_CALLBACK,
                                Looper::EVENT_INPUT,
                                cb,
                                nullptr /*data*/);
        LOG_ALWAYS_FATAL_IF(ret != 1, "Failed to add binder FD to Looper");

        return cb;
    }

    int handleEvent(int fd, int /*events*/, void* /*data*/) override {
        handleTransportPoll(fd);
        return 1;  // Continue receiving callbacks.
    }
};

int main() {
    ServiceManager *manager = new ServiceManager();

    if (!manager->add(serviceName, manager)) {
        ALOGE("Failed to register hwservicemanager with itself.");
    }

    TokenManager *tokenManager = new TokenManager();

    if (!manager->add(serviceName, tokenManager)) {
        ALOGE("Failed to register ITokenManager with hwservicemanager.");
    }

    // Tell IPCThreadState we're the service manager
    sp<BnHwServiceManager> service = new BnHwServiceManager(manager);
    IPCThreadState::self()->setTheContextObject(service);
    // Then tell the kernel
    ProcessState::self()->becomeContextManager(nullptr, nullptr);

    int rc = property_set("hwservicemanager.ready", "true");
    if (rc) {
        ALOGE("Failed to set \"hwservicemanager.ready\" (error %d). "\
              "HAL services will not start!\n", rc);
    }

    sp<Looper> looper = Looper::prepare(0 /* opts */);

    (void)HwBinderCallback::setupTo(looper);

    ALOGI("hwservicemanager is ready now.");

    while (true) {
        looper->pollAll(-1 /* timeoutMillis */);
    }

    return 0;
}
