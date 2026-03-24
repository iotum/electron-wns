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

namespace {
using winrt::Windows::Networking::PushNotifications::PushNotificationChannel;
using winrt::Windows::Networking::PushNotifications::PushNotificationChannelManager;
using winrt::Windows::Networking::PushNotifications::PushNotificationReceivedEventArgs;
using winrt::Windows::Networking::PushNotifications::PushNotificationType;

std::mutex g_mutex;
std::once_flag g_apartmentInitFlag;
PushNotificationChannel g_channel{nullptr};
winrt::event_token g_notificationToken{};
bool g_isListening = false;
Napi::ThreadSafeFunction g_notificationTsfn;

std::string ToUtf8(std::wstring const& input);
std::string ToUtf8(winrt::hstring const& input);
PushNotificationChannel GetOrCreateChannel();

void EnsureApartmentInitialized() {
  std::call_once(g_apartmentInitFlag, []() {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& error) {
      if (error.code() != RPC_E_CHANGED_MODE) {
        throw;
      }
    }
  });
}

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
    std::lock_guard<std::mutex> callbackLock(g_mutex);
    if (!g_notificationTsfn) {
      return;
    }

    struct NotificationPayload {
      std::string type;
      std::string content;
      std::unordered_map<std::string, std::string> headers;
    } payload;

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

    auto status = g_notificationTsfn.BlockingCall(
        new NotificationPayload(std::move(payload)),
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
      return;
    }
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

  return exports;
}
}  // namespace

NODE_API_MODULE(electron_wns, Initialize)
