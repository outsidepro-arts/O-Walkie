#include "global_ptt_plugin.h"

#include <flutter/event_channel.h>
#include <flutter/event_sink.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <memory>
#include <mutex>
#include <optional>

#include "global_ptt_hook.h"

namespace {

constexpr char kChannel[] =
    "ru.outsidepro_arts.owalkie.flutter/windows_global_ptt";
constexpr char kEventsChannel[] =
    "ru.outsidepro_arts.owalkie.flutter/windows_global_ptt_events";

constexpr char kEventDown[] = "global_ptt_down";
constexpr char kEventUp[] = "global_ptt_up";
constexpr char kEventCaptured[] = "global_ptt_captured";

std::unique_ptr<flutter::MethodChannel<>> g_method_channel;
std::unique_ptr<flutter::EventChannel<>> g_event_channel;
std::unique_ptr<flutter::EventSink<flutter::EncodableValue>> g_event_sink;
std::mutex g_event_sink_mutex;

std::optional<int> ReadInt(const flutter::EncodableValue& value) {
  if (const auto* i = std::get_if<int32_t>(&value)) {
    return *i;
  }
  if (const auto* l = std::get_if<int64_t>(&value)) {
    return static_cast<int>(*l);
  }
  return std::nullopt;
}

flutter::EncodableMap BindingToMap(int vkey, int mods) {
  return flutter::EncodableMap{
      {flutter::EncodableValue("vkey"), flutter::EncodableValue(vkey)},
      {flutter::EncodableValue("mods"), flutter::EncodableValue(mods)},
      {flutter::EncodableValue("displayName"),
       flutter::EncodableValue(PttComboToDisplayName(vkey, mods))},
  };
}

}  // namespace

GlobalPttPlugin& GlobalPttPlugin::Instance() {
  static GlobalPttPlugin instance;
  return instance;
}

void GlobalPttPlugin::Register(flutter::FlutterEngine* engine, HWND hwnd) {
  engine_ = engine;
  hwnd_ = hwnd;
  GlobalPttHook::Instance().SetWindow(hwnd);

  g_method_channel = std::make_unique<flutter::MethodChannel<>>(
      engine->messenger(), kChannel,
      &flutter::StandardMethodCodec::GetInstance());

  g_method_channel->SetMethodCallHandler(
      [](const flutter::MethodCall<>& call,
         std::unique_ptr<flutter::MethodResult<>> result) {
        auto& hook = GlobalPttHook::Instance();
        const std::string& method = call.method_name();

        if (method == "installHook") {
          result->Success(flutter::EncodableValue(hook.Install()));
          return;
        }
        if (method == "uninstallHook") {
          hook.Uninstall();
          result->Success();
          return;
        }
        if (method == "setBinding") {
          const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
          if (args == nullptr) {
            result->Error("invalid_args", "Expected map");
            return;
          }
          int vkey = 0;
          int mods = 0;
          for (const auto& entry : *args) {
            const auto* key = std::get_if<std::string>(&entry.first);
            if (key == nullptr) {
              continue;
            }
            const auto parsed = ReadInt(entry.second);
            if (!parsed.has_value()) {
              continue;
            }
            if (*key == "vkey") {
              vkey = *parsed;
            } else if (*key == "mods") {
              mods = *parsed;
            }
          }
          hook.SetBinding(vkey, mods);
          if (vkey > 0) {
            hook.Install();
          }
          result->Success();
          return;
        }
        if (method == "clearBinding") {
          hook.ClearBinding();
          result->Success();
          return;
        }
        if (method == "getBinding") {
          if (!hook.HasBinding()) {
            result->Success(flutter::EncodableValue());
            return;
          }
          result->Success(flutter::EncodableValue(
              BindingToMap(hook.vkey(), hook.mods())));
          return;
        }
        if (method == "startCapture") {
          result->Success(flutter::EncodableValue(hook.StartCapture()));
          return;
        }
        if (method == "cancelCapture") {
          hook.CancelCapture();
          result->Success();
          return;
        }
        if (method == "takeCaptureResult") {
          const auto captured = hook.TakeCaptureResult();
          if (captured.vkey <= 0) {
            result->Success(flutter::EncodableValue());
            return;
          }
          result->Success(flutter::EncodableValue(
              BindingToMap(captured.vkey, captured.mods)));
          return;
        }

        result->NotImplemented();
      });

  g_event_channel = std::make_unique<flutter::EventChannel<>>(
      engine->messenger(), kEventsChannel,
      &flutter::StandardMethodCodec::GetInstance());

  auto handler = std::make_unique<flutter::StreamHandlerFunctions<>>(
      [](const flutter::EncodableValue* arguments,
         std::unique_ptr<flutter::EventSink<>>&& events)
          -> std::unique_ptr<flutter::StreamHandlerError<>> {
        std::lock_guard<std::mutex> lock(g_event_sink_mutex);
        g_event_sink = std::move(events);
        return nullptr;
      },
      [](const flutter::EncodableValue* arguments)
          -> std::unique_ptr<flutter::StreamHandlerError<>> {
        std::lock_guard<std::mutex> lock(g_event_sink_mutex);
        g_event_sink.reset();
        return nullptr;
      });

  g_event_channel->SetStreamHandler(std::move(handler));
}

void GlobalPttPlugin::Unregister() {
  GlobalPttHook::Instance().Uninstall();
  std::lock_guard<std::mutex> lock(g_event_sink_mutex);
  g_event_sink.reset();
  g_method_channel.reset();
  g_event_channel.reset();
  engine_ = nullptr;
  hwnd_ = nullptr;
}

void GlobalPttPlugin::EmitEvent(const char* event) {
  std::lock_guard<std::mutex> lock(g_event_sink_mutex);
  if (g_event_sink != nullptr) {
    g_event_sink->Success(flutter::EncodableValue(event));
  }
}

void GlobalPttPlugin::OnPttDown() {
  EmitEvent(kEventDown);
}

void GlobalPttPlugin::OnPttUp() {
  EmitEvent(kEventUp);
}

void GlobalPttPlugin::OnCaptured() {
  EmitEvent(kEventCaptured);
}

void RegisterGlobalPttPlugin(flutter::FlutterEngine* engine, HWND hwnd) {
  GlobalPttPlugin::Instance().Register(engine, hwnd);
}
