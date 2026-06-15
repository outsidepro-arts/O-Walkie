import AVFoundation

/// iOS audio output profile registry.
/// Maps profile IDs to AVAudioSession category/mode/options.
/// IDs match Android [AudioOutputProfileRegistry] for shared [AudioOutputProfileStore] keys.
enum AudioOutputProfileRegistry {
  struct Option {
    let id: String
    let title: String
    let mode: AVAudioSession.Mode
    let defaultToSpeaker: Bool
    let allowBluetoothA2dp: Bool
    let duckOthers: Bool
  }

  static let options: [Option] = [
    Option(
      id: ID_MEDIA,
      title: ID_MEDIA,
      mode: .default,
      defaultToSpeaker: true,
      allowBluetoothA2dp: false,
      duckOthers: false
    ),
    Option(
      id: ID_VOICE_CALL,
      title: ID_VOICE_CALL,
      mode: .voiceChat,
      defaultToSpeaker: false,
      allowBluetoothA2dp: false,
      duckOthers: false
    ),
    Option(
      id: ID_VOICE_CALL_BT,
      title: ID_VOICE_CALL_BT,
      mode: .voiceChat,
      defaultToSpeaker: false,
      allowBluetoothA2dp: true,
      duckOthers: false
    ),
    Option(
      id: ID_GAME,
      title: ID_GAME,
      mode: .gameChat,
      defaultToSpeaker: true,
      allowBluetoothA2dp: false,
      duckOthers: false
    ),
    Option(
      id: ID_RAW,
      title: ID_RAW,
      mode: .measurement,
      defaultToSpeaker: true,
      allowBluetoothA2dp: false,
      duckOthers: false
    ),
    Option(
      id: ID_NOTIFICATION,
      title: ID_NOTIFICATION,
      mode: .voiceChat,
      defaultToSpeaker: true,
      allowBluetoothA2dp: false,
      duckOthers: true
    ),
  ]

  static func option(for id: String) -> Option {
    options.first { $0.id == id }
      ?? options.first { $0.id == ID_MEDIA }
      ?? options[0]
  }

  static func applySession(profileId: String) throws {
    let profile = option(for: profileId)
    var options: AVAudioSession.CategoryOptions = [.allowBluetooth]
    if profile.defaultToSpeaker {
      options.insert(.defaultToSpeaker)
    }
    if profile.allowBluetoothA2dp {
      options.insert(.allowBluetoothA2DP)
    }
    if profile.duckOthers {
      options.insert(.duckOthers)
    }
    let session = AVAudioSession.sharedInstance()
    try session.setCategory(.playAndRecord, mode: profile.mode, options: options)
    try session.setActive(true)
  }

  static let ID_MEDIA = "media"
  static let ID_VOICE_CALL = "voice_call"
  static let ID_VOICE_CALL_BT = "voice_call_bt"
  static let ID_GAME = "game"
  static let ID_RAW = "raw"
  static let ID_NOTIFICATION = "notification"
}