import AVFoundation

/// iOS microphone capture profiles (AVAudioSession category + mode).
/// IDs match Android [MicrophoneSourceRegistry] for shared [MicrophoneSourceStore] keys.
enum MicrophoneSourceRegistry {
  struct Option {
    let id: String
    let inputPreset: Int
    let category: AVAudioSession.Category
    let mode: AVAudioSession.Mode
  }

  static let options: [Option] = [
    Option(id: ID_MIC, inputPreset: 1, category: .playAndRecord, mode: .default),
    Option(id: ID_DEFAULT, inputPreset: 0, category: .playAndRecord, mode: .default),
    Option(id: ID_CAMCORDER, inputPreset: 2, category: .playAndRecord, mode: .videoRecording),
    Option(
      id: ID_VOICE_RECOGNITION,
      inputPreset: 3,
      category: .playAndRecord,
      mode: .measurement
    ),
    Option(
      id: ID_VOICE_COMMUNICATION,
      inputPreset: 4,
      category: .playAndRecord,
      mode: .voiceChat
    ),
    Option(id: ID_UNPROCESSED, inputPreset: 5, category: .playAndRecord, mode: .measurement),
    Option(id: ID_VOICE_PERFORMANCE, inputPreset: 6, category: .playAndRecord, mode: .gameChat),
  ]

  static func option(for id: String) -> Option {
    options.first { $0.id == id }
      ?? options.first { $0.id == ID_VOICE_COMMUNICATION }
      ?? options[0]
  }

  static func applySession(profileId: String, bluetoothHeadset: Bool) throws {
    let profile = option(for: profileId)
    var options: AVAudioSession.CategoryOptions = [.defaultToSpeaker, .allowBluetooth]
    if bluetoothHeadset {
      options.insert(.allowBluetoothA2DP)
    }
    let session = AVAudioSession.sharedInstance()
    try session.setCategory(profile.category, mode: profile.mode, options: options)
    try session.setActive(true)
  }

  static let ID_MIC = "mic"
  static let ID_DEFAULT = "default"
  static let ID_CAMCORDER = "camcorder"
  static let ID_VOICE_RECOGNITION = "voice_recognition"
  static let ID_VOICE_COMMUNICATION = "voice_communication"
  static let ID_UNPROCESSED = "unprocessed"
  static let ID_VOICE_PERFORMANCE = "voice_performance"
}
