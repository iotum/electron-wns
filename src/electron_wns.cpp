#include <napi.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include <Windows.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.PushNotifications.h>
#include <winrt/Windows.UI.Notifications.h>

#if __has_include(<winrt/Microsoft.Windows.PushNotifications.h>)
#define ELECTRON_WNS_HAS_WINAPPSDK_PUSH 1
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <winrt/Microsoft.Windows.PushNotifications.h>
#else
#define ELECTRON_WNS_HAS_WINAPPSDK_PUSH 0
#endif

namespace {
using winrt::Windows::Networking::PushNotifications::PushNotificationChannel;
using winrt::Windows::Networking::PushNotifications::PushNotificationChannelManager;
using winrt::Windows::Networking::PushNotifications::PushNotificationReceivedEventArgs;
using winrt::Windows::Networking::PushNotifications::PushNotificationType;

#if ELECTRON_WNS_HAS_WINAPPSDK_PUSH
using winrt::Microsoft::Windows::PushNotifications::PushNotificationChannelStatus;
using WinAppSdkPushNotificationManager = winrt::Microsoft::Windows::PushNotifications::PushNotificationManager;
using WinAppSdkPushNotificationReceivedEventArgs = winrt::Microsoft::Windows::PushNotifications::PushNotificationReceivedEventArgs;
#endif

struct NotificationPayload {
  std::string type;
  std::string content;
  std::unordered_map<std::string, std::string> headers;
};

std::mutex g_mutex;
std::once_flag g_apartmentInitFlag;
PushNotificationChannel g_channel{nullptr};
winrt::event_token g_notificationToken{};
bool g_isListening = false;
Napi::ThreadSafeFunction g_notificationTsfn;

#if ELECTRON_WNS_HAS_WINAPPSDK_PUSH
winrt::event_token g_winAppSdkNotificationToken{};
bool g_winAppSdkRegistered = false;
std::once_flag g_winAppSdkBootstrapInitFlag;
bool g_winAppSdkBootstrapAttempted = false;
HRESULT g_winAppSdkBootstrapInitResult = E_UNEXPECTED;
#endif

std::string ToUtf8(std::wstring const& input);
std::string ToUtf8(winrt::hstring const& input);
std::wstring FromUtf8(std::string const& input);
PushNotificationChannel GetOrCreateChannel();
void DispatchNotificationPayload(NotificationPayload&& payload);

bool IsCurrentProcessPackaged() {
  UINT32 packageNameLength = 0;
  LONG result = GetCurrentPackageFullName(&packageNameLength, nullptr);
  return result == ERROR_INSUFFICIENT_BUFFER;
}

std::string GetCurrentPackageFamilyNameUtf8() {
  UINT32 length = 0;
  LONG result = GetCurrentPackageFamilyName(&length, nullptr);
  if (result != ERROR_INSUFFICIENT_BUFFER || length == 0) {
    return "";
  }

  std::wstring familyName(length, L'\0');
  result = GetCurrentPackageFamilyName(&length, familyName.data());
  if (result != ERROR_SUCCESS) {
    return "";
  }

  if (!familyName.empty() && familyName.back() == L'\0') {
    familyName.pop_back();
  }

  return ToUtf8(familyName);
}

std::string GetCurrentPackageFullNameUtf8() {
  UINT32 length = 0;
  LONG result = GetCurrentPackageFullName(&length, nullptr);
  if (result != ERROR_INSUFFICIENT_BUFFER || length == 0) {
    return "";
  }

  std::wstring fullName(length, L'\0');
  result = GetCurrentPackageFullName(&length, fullName.data());
  if (result != ERROR_SUCCESS) {
    return "";
  }

  if (!fullName.empty() && fullName.back() == L'\0') {
    fullName.pop_back();
  }

  return ToUtf8(fullName);
}

void EnsureApartmentInitialized() {
  std::call_once(g_apartmentInitFlag, []() {
    try {
      // WinAppSDK PushNotificationManager activation is sensitive to the
      // caller apartment and can fail with E_FAIL from MTA in desktop hosts.
      winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (const winrt::hresult_error& error) {
      if (error.code() != RPC_E_CHANGED_MODE) {
        throw;
      }
    }
  });
}

#if ELECTRON_WNS_HAS_WINAPPSDK_PUSH
void EnsureWinAppSdkBootstrapInitialized() {
  std::call_once(g_winAppSdkBootstrapInitFlag, []() {
    g_winAppSdkBootstrapAttempted = true;
    g_winAppSdkBootstrapInitResult = MddBootstrapInitialize2(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        PACKAGE_VERSION{WINDOWSAPPSDK_RUNTIME_VERSION_UINT64},
        MddBootstrapInitializeOptions_OnPackageIdentity_NOOP);
  });

  if (FAILED(g_winAppSdkBootstrapInitResult)) {
    throw winrt::hresult_error(g_winAppSdkBootstrapInitResult);
  }
}

void EnsureWinAppSdkPushManagerAvailable() {
  EnsureApartmentInitialized();
  // Always bootstrap before calling Default(). For packaged apps with
  // OnPackageIdentity_NOOP this is effectively a no-op that still ensures the
  // WinAppSDK runtime is in scope. Crucially, calling Default() *before*
  // bootstrap can leave the WinRT singleton in a partial state that causes a
  // subsequent E_FAIL even after bootstrap succeeds.
  EnsureWinAppSdkBootstrapInitialized();
  // Initialize the AppLifecycle framework. PushNotificationManager::Default()
  // depends on AppInstance being available; without this Electron's activation
  // context doesn't set it up and Default() returns E_FAIL.
  (void)winrt::Microsoft::Windows::AppLifecycle::AppInstance::GetCurrent();
  (void)WinAppSdkPushNotificationManager::Default();
}
#endif

std::string ToUtf8(std::wstring const& input) {
  if (input.empty()) {
    return {};
  }

  int required = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
  std::string output(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), output.data(), required, nullptr, nullptr);
  return output;
}

std::string ToUtf8(winrt::hstring const& input) {
  return ToUtf8(std::wstring(input));
}

std::wstring FromUtf8(std::string const& input) {
  if (input.empty()) {
    return {};
  }

  int required = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), nullptr, 0);
  std::wstring output(static_cast<size_t>(required), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), output.data(), required);
  return output;
}

PushNotificationChannel GetOrCreateChannel() {
  EnsureApartmentInitialized();

  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_channel) {
    return g_channel;
  }

  auto asyncOp = PushNotificationChannelManager::CreatePushNotificationChannelForApplicationAsync();
  g_channel = asyncOp.get();
  return g_channel;
}

std::string NotificationTypeToString(PushNotificationType type) {
  switch (type) {
    case PushNotificationType::Toast:
      return "toast";
    case PushNotificationType::Tile:
      return "tile";
    case PushNotificationType::Badge:
      return "badge";
    case PushNotificationType::Raw:
      return "raw";
    default:
      return "unknown";
  }
}

void DispatchNotificationPayload(NotificationPayload&& payload) {
  Napi::ThreadSafeFunction tsfn;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_notificationTsfn) {
      return;
    }

    tsfn = g_notificationTsfn;
    if (tsfn.Acquire() != napi_ok) {
      return;
    }
  }

  auto* heapPayload = new NotificationPayload(std::move(payload));
  auto status = tsfn.BlockingCall(
      heapPayload,
      [](Napi::Env callbackEnv, Napi::Function jsCallback, NotificationPayload* data) {
        Napi::Object notification = Napi::Object::New(callbackEnv);
        notification.Set("type", data->type);
        notification.Set("content", data->content);

        Napi::Object headers = Napi::Object::New(callbackEnv);
        for (const auto& [key, value] : data->headers) {
          headers.Set(key, value);
        }
        notification.Set("headers", headers);

        jsCallback.Call({notification});
        delete data;
      });

  if (status != napi_ok) {
    delete heapPayload;
  }

  tsfn.Release();
}

Napi::Value GetChannelWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto deferred = Napi::Promise::Deferred::New(env);

  try {
    auto channel = GetOrCreateChannel();
    Napi::Object result = Napi::Object::New(env);
    result.Set("uri", ToUtf8(channel.Uri()));

    const auto expiration = channel.ExpirationTime();
    const auto expirationTicks = expiration.time_since_epoch().count();
    result.Set("expirationTicks", Napi::Number::New(env, static_cast<double>(expirationTicks)));

    deferred.Resolve(result);
  } catch (const winrt::hresult_error& error) {
    deferred.Reject(Napi::Error::New(env, ToUtf8(error.message())).Value());
  } catch (const std::exception& error) {
    deferred.Reject(Napi::Error::New(env, error.what()).Value());
  }

  return deferred.Promise();
}

Napi::Value StartForegroundNotificationsWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsFunction()) {
    throw Napi::TypeError::New(env, "startForegroundNotifications requires a callback function");
  }

  Napi::Function callback = info[0].As<Napi::Function>();

  EnsureApartmentInitialized();
  auto channel = GetOrCreateChannel();

  std::lock_guard<std::mutex> lock(g_mutex);

  if (g_isListening) {
    if (g_notificationTsfn) {
      g_notificationTsfn.Release();
    }
    channel.PushNotificationReceived(g_notificationToken);
    g_isListening = false;
  }

  g_notificationTsfn = Napi::ThreadSafeFunction::New(
      env,
      callback,
      "wns-foreground-notification-callback",
      0,
      1);

  g_notificationToken = channel.PushNotificationReceived([](PushNotificationChannel const&, PushNotificationReceivedEventArgs const& args) {
    NotificationPayload payload;

    payload.type = NotificationTypeToString(args.NotificationType());

    if (args.NotificationType() == PushNotificationType::Raw) {
      auto raw = args.RawNotification();
      payload.content = ToUtf8(raw.Content());

      auto headerMap = raw.Headers();
      for (const auto& pair : headerMap) {
        payload.headers[ToUtf8(pair.Key())] = ToUtf8(pair.Value());
      }
    } else if (args.ToastNotification()) {
      auto xmlDocument = args.ToastNotification().Content();
      payload.content = ToUtf8(xmlDocument.GetXml());
    }

    DispatchNotificationPayload(std::move(payload));
  });

  g_isListening = true;
  return env.Undefined();
}

Napi::Value StopForegroundNotificationsWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  EnsureApartmentInitialized();

  std::lock_guard<std::mutex> lock(g_mutex);

  if (!g_isListening) {
    return env.Undefined();
  }

  if (g_channel) {
    g_channel.PushNotificationReceived(g_notificationToken);
  }

  if (g_notificationTsfn) {
    g_notificationTsfn.Release();
    g_notificationTsfn = {};
  }

  g_isListening = false;
  return env.Undefined();
}

Napi::Value IsWinAppSdkPushSupportedWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

#if !ELECTRON_WNS_HAS_WINAPPSDK_PUSH
  return Napi::Boolean::New(env, false);
#else
  try {
    EnsureApartmentInitialized();
    EnsureWinAppSdkBootstrapInitialized();

    // First ask the WinAppSDK static capability probe. This does not require
    // activating Default() and can report unsupported app models cleanly.
    if (!WinAppSdkPushNotificationManager::IsSupported()) {
      return Napi::Boolean::New(env, false);
    }

    EnsureWinAppSdkPushManagerAvailable();
    return Napi::Boolean::New(env, WinAppSdkPushNotificationManager::Default().IsSupported());
  } catch (...) {
    return Napi::Boolean::New(env, false);
  }
#endif
}

Napi::Value GetWinAppSdkPushDiagnosticsWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::Object diagnostics = Napi::Object::New(env);

#if !ELECTRON_WNS_HAS_WINAPPSDK_PUSH
  diagnostics.Set("compiledWithWinAppSdkHeaders", Napi::Boolean::New(env, false));
  diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
  diagnostics.Set("processPackaged", Napi::Boolean::New(env, IsCurrentProcessPackaged()));
  diagnostics.Set("packageFamilyName", Napi::String::New(env, GetCurrentPackageFamilyNameUtf8()));
  diagnostics.Set("packageFullName", Napi::String::New(env, GetCurrentPackageFullNameUtf8()));
  diagnostics.Set("staticIsSupported", Napi::Boolean::New(env, false));
  diagnostics.Set("errorCode", Napi::Number::New(env, 0));
  diagnostics.Set("errorMessage", Napi::String::New(env, "WinAppSDK push headers were not found at build time"));
  diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, false));
  diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, 0));
  return diagnostics;
#else
  diagnostics.Set("compiledWithWinAppSdkHeaders", Napi::Boolean::New(env, true));
  diagnostics.Set("processPackaged", Napi::Boolean::New(env, IsCurrentProcessPackaged()));
  diagnostics.Set("packageFamilyName", Napi::String::New(env, GetCurrentPackageFamilyNameUtf8()));
  diagnostics.Set("packageFullName", Napi::String::New(env, GetCurrentPackageFullNameUtf8()));
  diagnostics.Set("staticIsSupported", Napi::Boolean::New(env, false));
  diagnostics.Set("errorCode", Napi::Number::New(env, 0));
  diagnostics.Set("errorMessage", Napi::String::New(env, ""));
  diagnostics.Set("failedStep", Napi::String::New(env, ""));
  diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
  diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));

  // Step 1: bootstrap
  try {
    EnsureWinAppSdkBootstrapInitialized();
  } catch (const winrt::hresult_error& error) {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
    diagnostics.Set("errorCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(error.code()))));
    diagnostics.Set("errorMessage", Napi::String::New(env, ToUtf8(error.message())));
    diagnostics.Set("failedStep", Napi::String::New(env, "bootstrap"));
    diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
    diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));
    return diagnostics;
  }

  // Step 2: static IsSupported probe
  bool staticIsSupported = false;
  try {
    staticIsSupported = WinAppSdkPushNotificationManager::IsSupported();
    diagnostics.Set("staticIsSupported", Napi::Boolean::New(env, staticIsSupported));
    if (!staticIsSupported) {
      diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
      diagnostics.Set("failedStep", Napi::String::New(env, "staticIsSupported"));
      diagnostics.Set("errorCode", Napi::Number::New(env, 0));
      diagnostics.Set("errorMessage", Napi::String::New(env, "PushNotificationManager::IsSupported() returned false"));
      return diagnostics;
    }
  } catch (const winrt::hresult_error& error) {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
    diagnostics.Set("errorCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(error.code()))));
    diagnostics.Set("errorMessage", Napi::String::New(env, ToUtf8(error.message())));
    diagnostics.Set("failedStep", Napi::String::New(env, "staticIsSupported"));
    diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
    diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));
    return diagnostics;
  }

  // Step 3: AppInstance::GetCurrent() to prime AppLifecycle
  try {
    (void)winrt::Microsoft::Windows::AppLifecycle::AppInstance::GetCurrent();
  } catch (const winrt::hresult_error& error) {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
    diagnostics.Set("errorCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(error.code()))));
    diagnostics.Set("errorMessage", Napi::String::New(env, ToUtf8(error.message())));
    diagnostics.Set("failedStep", Napi::String::New(env, "appInstance"));
    diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
    diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));
    return diagnostics;
  }

  // Step 4: Default()
  winrt::Microsoft::Windows::PushNotifications::PushNotificationManager manager{nullptr};
  try {
    manager = WinAppSdkPushNotificationManager::Default();
  } catch (const winrt::hresult_error& error) {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
    diagnostics.Set("errorCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(error.code()))));
    diagnostics.Set("errorMessage", Napi::String::New(env, ToUtf8(error.message())));
    diagnostics.Set("failedStep", Napi::String::New(env, "default"));
    diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
    diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));
    return diagnostics;
  }

  // Step 5: IsSupported()
  try {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, manager.IsSupported()));
    diagnostics.Set("failedStep", Napi::String::New(env, ""));
  } catch (const winrt::hresult_error& error) {
    diagnostics.Set("isSupported", Napi::Boolean::New(env, false));
    diagnostics.Set("errorCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(error.code()))));
    diagnostics.Set("errorMessage", Napi::String::New(env, ToUtf8(error.message())));
    diagnostics.Set("failedStep", Napi::String::New(env, "isSupported"));
  }

  diagnostics.Set("bootstrapAttempted", Napi::Boolean::New(env, g_winAppSdkBootstrapAttempted));
  diagnostics.Set("bootstrapInitCode", Napi::Number::New(env, static_cast<double>(static_cast<int32_t>(g_winAppSdkBootstrapInitResult))));
  return diagnostics;
#endif
}

Napi::Value RegisterPushWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

#if !ELECTRON_WNS_HAS_WINAPPSDK_PUSH
  throw Napi::Error::New(env, "registerPush is unavailable: WinAppSDK push headers were not found at build time");
#else
  try {
    EnsureWinAppSdkPushManagerAvailable();

    auto manager = WinAppSdkPushNotificationManager::Default();
    if (!manager.IsSupported()) {
      throw Napi::Error::New(env, "registerPush failed: PushNotificationManager is not supported in this runtime");
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_winAppSdkRegistered) {
      return env.Undefined();
    }

    g_winAppSdkNotificationToken = manager.PushReceived([](auto const&, WinAppSdkPushNotificationReceivedEventArgs const& args) {
      NotificationPayload payload;
      payload.type = "raw";

      auto bytes = args.Payload();
      payload.content.assign(bytes.begin(), bytes.end());

      DispatchNotificationPayload(std::move(payload));
    });

    manager.Register();
    g_winAppSdkRegistered = true;
    return env.Undefined();
  } catch (const winrt::hresult_error& error) {
    throw Napi::Error::New(env, ToUtf8(error.message()));
  }
#endif
}

Napi::Value UnregisterPushWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

#if !ELECTRON_WNS_HAS_WINAPPSDK_PUSH
  throw Napi::Error::New(env, "unregisterPush is unavailable: WinAppSDK push headers were not found at build time");
#else
  try {
    EnsureWinAppSdkPushManagerAvailable();
    auto manager = WinAppSdkPushNotificationManager::Default();

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_winAppSdkRegistered) {
      return env.Undefined();
    }

    manager.PushReceived(g_winAppSdkNotificationToken);
    manager.Unregister();
    g_winAppSdkRegistered = false;

    return env.Undefined();
  } catch (const winrt::hresult_error& error) {
    throw Napi::Error::New(env, ToUtf8(error.message()));
  }
#endif
}

Napi::Value GetChannelForPushManagerWrapped(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  auto deferred = Napi::Promise::Deferred::New(env);

#if !ELECTRON_WNS_HAS_WINAPPSDK_PUSH
  deferred.Reject(Napi::Error::New(env, "getChannelForPushManager is unavailable: WinAppSDK push headers were not found at build time").Value());
  return deferred.Promise();
#else
  if (info.Length() < 1 || !info[0].IsString()) {
    throw Napi::TypeError::New(env, "getChannelForPushManager requires a remoteId GUID string argument");
  }

  try {
    EnsureWinAppSdkPushManagerAvailable();

    auto manager = WinAppSdkPushNotificationManager::Default();
    if (!manager.IsSupported()) {
      throw Napi::Error::New(env, "getChannelForPushManager failed: PushNotificationManager is not supported in this runtime");
    }

    const std::string remoteIdUtf8 = info[0].As<Napi::String>().Utf8Value();
    winrt::guid remoteId{winrt::hstring(FromUtf8(remoteIdUtf8))};

    auto createChannelOperation = manager.CreateChannelAsync(remoteId);
    auto result = createChannelOperation.get();

    if (result.Status() != PushNotificationChannelStatus::CompletedSuccess) {
      throw winrt::hresult_error(result.ExtendedError());
    }

    auto channel = result.Channel();
    Napi::Object response = Napi::Object::New(env);
    response.Set("uri", ToUtf8(channel.Uri().ToString()));
    response.Set("expirationTicks", Napi::Number::New(env, 0));

    deferred.Resolve(response);
  } catch (const winrt::hresult_error& error) {
    deferred.Reject(Napi::Error::New(env, ToUtf8(error.message())).Value());
  } catch (const std::exception& error) {
    deferred.Reject(Napi::Error::New(env, error.what()).Value());
  }

  return deferred.Promise();
#endif
}

Napi::Object Initialize(Napi::Env env, Napi::Object exports) {
  try {
    EnsureApartmentInitialized();
  } catch (const winrt::hresult_error& error) {
    Napi::Error::New(env, ToUtf8(error.message())).ThrowAsJavaScriptException();
    return exports;
  } catch (const std::exception& error) {
    Napi::Error::New(env, error.what()).ThrowAsJavaScriptException();
    return exports;
  }

  exports.Set("getChannel", Napi::Function::New(env, GetChannelWrapped));
  exports.Set("startForegroundNotifications", Napi::Function::New(env, StartForegroundNotificationsWrapped));
  exports.Set("stopForegroundNotifications", Napi::Function::New(env, StopForegroundNotificationsWrapped));
  exports.Set("isWinAppSdkPushSupported", Napi::Function::New(env, IsWinAppSdkPushSupportedWrapped));
  exports.Set("getWinAppSdkPushDiagnostics", Napi::Function::New(env, GetWinAppSdkPushDiagnosticsWrapped));
  exports.Set("registerPush", Napi::Function::New(env, RegisterPushWrapped));
  exports.Set("unregisterPush", Napi::Function::New(env, UnregisterPushWrapped));
  exports.Set("getChannelForPushManager", Napi::Function::New(env, GetChannelForPushManagerWrapped));

  return exports;
}
}  // namespace

NODE_API_MODULE(electron_wns, Initialize)
