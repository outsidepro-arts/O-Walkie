/// Roger / call tone pattern (mirrors Kotlin [RogerPattern]).
class SignalPoint {
  const SignalPoint({required this.freqHz, required this.durationMs});

  final double freqHz;
  final int durationMs;

  Map<String, dynamic> toJson() => {
        'freqHz': freqHz,
        'durationMs': durationMs,
      };

  factory SignalPoint.fromJson(Map<String, dynamic> json) {
    return SignalPoint(
      freqHz: (json['freqHz'] as num).toDouble(),
      durationMs: json['durationMs'] as int,
    );
  }
}

class SignalPattern {
  const SignalPattern({
    required this.id,
    required this.name,
    required this.points,
    this.builtIn = false,
    this.repeatCount,
  });

  final String id;
  final String name;
  final List<SignalPoint> points;
  final bool builtIn;
  final int? repeatCount;

  bool get isEmpty => points.isEmpty;

  List<SignalPoint> expandedPoints() {
    final r = repeatCount?.clamp(1, 500) ?? 1;
    if (r <= 1) {
      return points;
    }
    return [for (var i = 0; i < r; i++) ...points];
  }

  SignalPattern copyWith({
    String? id,
    String? name,
    List<SignalPoint>? points,
    bool? builtIn,
    int? repeatCount,
  }) {
    return SignalPattern(
      id: id ?? this.id,
      name: name ?? this.name,
      points: points ?? this.points,
      builtIn: builtIn ?? this.builtIn,
      repeatCount: repeatCount ?? this.repeatCount,
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'builtIn': builtIn,
        'points': [for (final p in points) p.toJson()],
        if (repeatCount != null) 'repeatCount': repeatCount,
      };

  factory SignalPattern.fromJson(Map<String, dynamic> json) {
    final ptsJson = json['points'] as List<dynamic>? ?? [];
    return SignalPattern(
      id: json['id'] as String,
      name: json['name'] as String,
      builtIn: json['builtIn'] as bool? ?? false,
      points: [
        for (final p in ptsJson)
          SignalPoint.fromJson(p as Map<String, dynamic>),
      ],
      repeatCount: json['repeatCount'] as int?,
    );
  }
}

abstract final class BuiltInRogerPatterns {
  static const none = SignalPattern(
    id: 'none',
    name: 'No signal',
    points: [],
    builtIn: true,
  );

  static const variant1 = SignalPattern(
    id: 'variant_1',
    name: 'Variant 1',
    builtIn: true,
    points: [
      SignalPoint(freqHz: 890, durationMs: 20),
      SignalPoint(freqHz: 670, durationMs: 20),
      SignalPoint(freqHz: 890, durationMs: 45),
      SignalPoint(freqHz: 1000, durationMs: 28),
    ],
  );

  static const variant2 = SignalPattern(
    id: 'variant_2',
    name: 'Variant 2',
    builtIn: true,
    points: [
      SignalPoint(freqHz: 1000, durationMs: 88),
      SignalPoint(freqHz: 800, durationMs: 64),
    ],
  );

  static const variant3 = SignalPattern(
    id: 'variant_3',
    name: 'Variant 3',
    builtIn: true,
    points: [
      SignalPoint(freqHz: 1330, durationMs: 68),
      SignalPoint(freqHz: 1600, durationMs: 56),
    ],
  );

  static List<SignalPattern> get all => [none, variant1, variant2, variant3];
}

abstract final class BuiltInCallPatterns {
  static const _cycle1 = [
    SignalPoint(freqHz: 2300, durationMs: 70),
    SignalPoint(freqHz: 1850, durationMs: 70),
    SignalPoint(freqHz: 1450, durationMs: 70),
  ];

  static const _cycle2 = [
    SignalPoint(freqHz: 1150, durationMs: 35),
    SignalPoint(freqHz: 1350, durationMs: 35),
    SignalPoint(freqHz: 1550, durationMs: 35),
    SignalPoint(freqHz: 1750, durationMs: 35),
    SignalPoint(freqHz: 1550, durationMs: 35),
    SignalPoint(freqHz: 1350, durationMs: 35),
  ];

  static const _cycle3 = [
    SignalPoint(freqHz: 2000, durationMs: 60),
    SignalPoint(freqHz: 1000, durationMs: 60),
  ];

  static const variant1 = SignalPattern(
    id: 'call_variant_1',
    name: 'Variant 1',
    builtIn: true,
    points: _cycle1,
    repeatCount: 9,
  );

  static const variant2 = SignalPattern(
    id: 'call_variant_2',
    name: 'Variant 2',
    builtIn: true,
    points: _cycle2,
    repeatCount: 14,
  );

  static const variant3 = SignalPattern(
    id: 'call_variant_3',
    name: 'Variant 3',
    builtIn: true,
    points: _cycle3,
    repeatCount: 32,
  );

  static List<SignalPattern> get all => [variant1, variant2, variant3];
}
