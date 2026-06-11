import AVFoundation
import Flutter
import UIKit

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private let platformChannelName = "ru.outsidepro_arts.owalkie.flutter/platform"
  private var pendingMicResult: FlutterResult?
  private var microphoneProfileId = MicrophoneSourceRegistry.ID_MIC

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
      case "listMicrophoneSources":
        result(
          MicrophoneSourceRegistry.options.map { option in
            [
              "id": option.id,
              "title": option.id,
              "inputPreset": option.inputPreset,
            ]
          }
        )
      case "applyMicrophoneProfile":
        let args = call.arguments as? [String: Any]
        let profileId = args?["profileId"] as? String ?? self.microphoneProfileId
        let bluetooth = args?["bluetoothHeadset"] as? Bool ?? false
        self.applyMicrophoneSession(profileId: profileId, bluetoothHeadset: bluetooth)
        result(true)
      case "prepareAudioSession":
        let args = call.arguments as? [String: Any]
        let bluetooth = args?["bluetoothHeadset"] as? Bool ?? false
        let profileId = args?["microphoneProfileId"] as? String ?? self.microphoneProfileId
        self.applyMicrophoneSession(profileId: profileId, bluetoothHeadset: bluetooth)
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

  private func applyMicrophoneSession(profileId: String, bluetoothHeadset: Bool) {
    microphoneProfileId = profileId
    do {
      try MicrophoneSourceRegistry.applySession(
        profileId: profileId,
        bluetoothHeadset: bluetoothHeadset
      )
    } catch {
      NSLog("owalkie_ios: AVAudioSession apply failed for profile %@: %@", profileId, error.localizedDescription)
    }
  }

  private func releaseAudioSession() {
    try? AVAudioSession.sharedInstance().setActive(false, options: [.notifyOthersOnDeactivation])
  }
}
