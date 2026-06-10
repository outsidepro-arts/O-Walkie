import AVFoundation
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private let platformChannelName = "ru.outsidepro_arts.owalkie.flutter/platform"
  private var pendingMicResult: FlutterResult?

  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)

    let messenger = engineBridge.applicationRegistrar.messenger()
    let channel = FlutterMethodChannel(name: platformChannelName, binaryMessenger: messenger)
    channel.setMethodCallHandler { [weak self] call, result in
      guard let self else {
        result(FlutterError(code: "unavailable", message: "AppDelegate released", details: nil))
        return
      }
      switch call.method {
      case "hasMicrophonePermission":
        result(self.hasMicrophonePermission())
      case "requestMicrophonePermission":
        self.requestMicrophonePermission(result: result)
      case "prepareAudioSession":
        self.prepareAudioSession()
        result(true)
      case "releaseAudioSession":
        self.releaseAudioSession()
        result(nil)
      default:
        result(FlutterMethodNotImplemented)
      }
    }
  }

  private func hasMicrophonePermission() -> Bool {
    AVAudioSession.sharedInstance().recordPermission == .granted
  }

  private func requestMicrophonePermission(result: @escaping FlutterResult) {
    if hasMicrophonePermission() {
      result(true)
      return
    }
    if pendingMicResult != nil {
      result(FlutterError(code: "busy", message: "Microphone permission request in progress", details: nil))
      return
    }
    pendingMicResult = result
    AVAudioSession.sharedInstance().requestRecordPermission { [weak self] granted in
      DispatchQueue.main.async {
        self?.pendingMicResult?(granted)
        self?.pendingMicResult = nil
      }
    }
  }

  private func prepareAudioSession() {
    let session = AVAudioSession.sharedInstance()
    try? session.setCategory(.playAndRecord, mode: .voiceChat, options: [.defaultToSpeaker, .allowBluetooth])
    try? session.setActive(true)
  }

  private func releaseAudioSession() {
    try? AVAudioSession.sharedInstance().setActive(false, options: [.notifyOthersOnDeactivation])
  }
}
