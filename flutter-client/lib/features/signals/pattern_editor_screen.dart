import 'dart:async';
import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:go_router/go_router.dart';

import '../../features/home/home_screen_controller.dart';
import '../../data/signal_pattern_store.dart';
import '../../domain/signal_pattern.dart';
import '../../l10n/app_strings.dart';

enum SignalEditorKind { roger, calling }

/// Fixed viewport so action buttons stay put as segments are added or removed.
const _segmentListHeight = 288.0;

class PatternEditorScreen extends ConsumerStatefulWidget {
  const PatternEditorScreen({
    super.key,
    required this.kind,
    this.editId,
  });

  final SignalEditorKind kind;
  final String? editId;

  @override
  ConsumerState<PatternEditorScreen> createState() => _PatternEditorScreenState();
}

class _PatternEditorScreenState extends ConsumerState<PatternEditorScreen> {
  final _nameCtrl = TextEditingController();
  final _repeatCtrl = TextEditingController(text: '1');
  final List<SignalPoint> _points = [];

  @override
  void initState() {
    super.initState();
    _loadExisting();
  }

  void _loadExisting() {
    final id = widget.editId;
    if (id == null) {
      return;
    }
    final patterns = widget.kind == SignalEditorKind.roger
        ? ref.read(rogerPatternStoreProvider).getAllPatterns()
        : ref.read(callingPatternStoreProvider).getAllPatterns();
    SignalPattern? found;
    for (final p in patterns) {
      if (p.id == id) {
        found = p;
        break;
      }
    }
    if (found == null) {
      return;
    }
    _nameCtrl.text = found.name;
    _points.addAll(found.points);
    if (found.repeatCount != null) {
      _repeatCtrl.text = '${found.repeatCount}';
    }
  }

  @override
  void dispose() {
    _nameCtrl.dispose();
    _repeatCtrl.dispose();
    super.dispose();
  }

  int get _maxDurationMs =>
      widget.kind == SignalEditorKind.calling ? 5000 : 2000;

  int _totalDurationMs() {
    final base = _points.fold<int>(0, (s, p) => s + p.durationMs);
    if (widget.kind == SignalEditorKind.calling) {
      final rep = int.tryParse(_repeatCtrl.text) ?? 1;
      return base * rep.clamp(1, 500);
    }
    return base + 40;
  }

  Future<void> _addOrEditSegment([int? index]) async {
    final result = await showDialog<SignalPoint>(
      context: context,
      builder: (ctx) => _SegmentEditDialog(
        title: index == null
            ? AppStrings.rogerNewSegment
            : AppStrings.rogerEditSegment,
        initialFreqHz: index != null ? _points[index].freqHz : 1000,
        initialDurationMs: index != null ? _points[index].durationMs : 50,
      ),
    );
    if (result == null) {
      return;
    }
    setState(() {
      if (index == null) {
        _points.add(result);
      } else {
        _points[index] = result;
      }
    });
  }

  Future<void> _save() async {
    final name = _nameCtrl.text.trim();
    if (name.isEmpty) {
      _showSnack(AppStrings.rogerNameRequired);
      return;
    }
    if (_points.isEmpty) {
      _showSnack(AppStrings.rogerPointsRequired);
      return;
    }
    if (_totalDurationMs() > _maxDurationMs) {
      _showSnack(AppStrings.rogerPointsTooLong);
      return;
    }
    if (widget.kind == SignalEditorKind.roger) {
      final store = ref.read(rogerPatternStoreProvider);
      if (widget.editId != null) {
        await store.updateCustomPattern(widget.editId!, name, _points);
      } else {
        await store.saveCustomPattern(name, _points);
      }
    } else {
      final rep = int.tryParse(_repeatCtrl.text) ?? 1;
      final store = ref.read(callingPatternStoreProvider);
      if (widget.editId != null) {
        await store.updateCustomPattern(widget.editId!, name, _points, rep);
      } else {
        await store.saveCustomPattern(name, _points, rep);
      }
    }
    if (mounted) {
      context.pop();
    }
  }

  Future<void> _copyToClipboard() async {
    final payload = jsonEncode({
      'name': _nameCtrl.text.trim(),
      'points': [for (final p in _points) p.toJson()],
      if (widget.kind == SignalEditorKind.calling)
        'repeatCount': int.tryParse(_repeatCtrl.text) ?? 1,
    });
    await Clipboard.setData(ClipboardData(text: payload));
    _showSnack(AppStrings.patternCopied);
  }

  Future<void> _pasteFromClipboard() async {
    final data = await Clipboard.getData('text/plain');
    final raw = data?.text;
    if (raw == null || raw.isEmpty) {
      return;
    }
    try {
      final map = jsonDecode(raw) as Map<String, dynamic>;
      final pts = map['points'] as List<dynamic>? ?? [];
      setState(() {
        _nameCtrl.text = map['name'] as String? ?? _nameCtrl.text;
        _points
          ..clear()
          ..addAll(
            pts.map(
              (e) => SignalPoint.fromJson(e as Map<String, dynamic>),
            ),
          );
        if (widget.kind == SignalEditorKind.calling) {
          _repeatCtrl.text = '${map['repeatCount'] ?? 1}';
        }
      });
    } catch (_) {
      _showSnack(AppStrings.patternPasteFailed);
    }
  }

  void _showSnack(String text) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(text)));
  }

  List<SignalPoint> _pointsForPlayback() {
    if (widget.kind == SignalEditorKind.calling) {
      final rep = int.tryParse(_repeatCtrl.text);
      if (rep == null || rep < 1 || rep > 500) {
        _showSnack(AppStrings.rogerPointInvalid);
        return List<SignalPoint>.from(_points);
      }
      return expandSignalPoints(_points, repeatCount: rep);
    }
    return List<SignalPoint>.from(_points);
  }

  void _playPattern() {
    ref
        .read(homeScreenControllerProvider.notifier)
        .previewSignalPattern(_pointsForPlayback());
  }

  @override
  Widget build(BuildContext context) {
    final title = widget.kind == SignalEditorKind.calling
        ? AppStrings.callCustomTitle
        : AppStrings.rogerCustomTitle;

    return Scaffold(
      resizeToAvoidBottomInset: true,
      appBar: AppBar(
        title: Text(title),
        leading: IconButton(
          icon: const Icon(Icons.arrow_back),
          onPressed: () => context.pop(),
        ),
        actions: [
          IconButton(
            tooltip: AppStrings.patternCopy,
            onPressed: _copyToClipboard,
            icon: const Icon(Icons.copy),
          ),
          IconButton(
            tooltip: AppStrings.patternPaste,
            onPressed: _pasteFromClipboard,
            icon: const Icon(Icons.paste),
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          TextField(
            controller: _nameCtrl,
            decoration: InputDecoration(labelText: AppStrings.rogerNameHint),
          ),
          if (widget.kind == SignalEditorKind.calling) ...[
            const SizedBox(height: 8),
            TextField(
              controller: _repeatCtrl,
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                labelText: AppStrings.callRepeatLabel,
              ),
            ),
          ],
          const SizedBox(height: 16),
          Text(AppStrings.rogerPointsLabel, style: Theme.of(context).textTheme.titleSmall),
          const SizedBox(height: 8),
          SizedBox(
            height: _segmentListHeight,
            child: _points.isEmpty
                ? Align(
                    alignment: Alignment.topLeft,
                    child: Text(AppStrings.rogerPointsEmpty),
                  )
                : ListView.separated(
                    itemCount: _points.length,
                    separatorBuilder: (context, index) =>
                        const Divider(height: 1),
                    itemBuilder: (context, i) => ListTile(
                      title: Text(
                        _points[i].freqHz <= 0
                            ? AppStrings.rogerPointPause
                            : AppStrings.rogerPointHz(_points[i].freqHz.toInt()),
                      ),
                      subtitle: Text(
                        AppStrings.rogerPointDurationMs(_points[i].durationMs),
                      ),
                      trailing: IconButton(
                        icon: const Icon(Icons.edit),
                        onPressed: () => _addOrEditSegment(i),
                      ),
                      onTap: () => _addOrEditSegment(i),
                    ),
                  ),
          ),
          OutlinedButton(
            onPressed: () => _addOrEditSegment(),
            child: Text(AppStrings.rogerNewSegment),
          ),
          const SizedBox(height: 12),
          OutlinedButton(
            onPressed: _playPattern,
            child: Text(AppStrings.playSignalButton),
          ),
          const SizedBox(height: 24),
          FilledButton(
            onPressed: _save,
            child: Text(AppStrings.rogerSave),
          ),
        ],
      ),
    );
  }
}

class _SegmentEditDialog extends StatefulWidget {
  const _SegmentEditDialog({
    required this.title,
    required this.initialFreqHz,
    required this.initialDurationMs,
  });

  final String title;
  final double initialFreqHz;
  final int initialDurationMs;

  @override
  State<_SegmentEditDialog> createState() => _SegmentEditDialogState();
}

class _SegmentEditDialogState extends State<_SegmentEditDialog> {
  late final TextEditingController _freqCtrl;
  late final TextEditingController _durCtrl;

  @override
  void initState() {
    super.initState();
    _freqCtrl = TextEditingController(
      text: '${widget.initialFreqHz.toInt()}',
    );
    _durCtrl = TextEditingController(
      text: '${widget.initialDurationMs}',
    );
  }

  @override
  void dispose() {
    _freqCtrl.dispose();
    _durCtrl.dispose();
    super.dispose();
  }

  Future<void> _close([SignalPoint? result]) async {
    FocusManager.instance.primaryFocus?.unfocus();
    await SystemChannels.textInput.invokeMethod<void>('TextInput.hide');
    if (!mounted) {
      return;
    }
    Navigator.of(context).pop(result);
  }

  void _submit() {
    final freq = double.tryParse(_freqCtrl.text);
    final dur = int.tryParse(_durCtrl.text);
    if (freq == null || dur == null || freq < 0 || dur <= 0) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(AppStrings.rogerPointInvalid)),
      );
      return;
    }
    unawaited(_close(SignalPoint(freqHz: freq, durationMs: dur)));
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text(widget.title),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: _freqCtrl,
              keyboardType: TextInputType.number,
              textInputAction: TextInputAction.next,
              decoration: InputDecoration(
                labelText: AppStrings.rogerFrequencyHint,
              ),
            ),
            TextField(
              controller: _durCtrl,
              keyboardType: TextInputType.number,
              textInputAction: TextInputAction.done,
              onSubmitted: (_) => _submit(),
              decoration: InputDecoration(
                labelText: AppStrings.rogerDurationHint,
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => unawaited(_close()),
          child: Text(AppStrings.rogerCancel),
        ),
        FilledButton(
          onPressed: _submit,
          child: Text(AppStrings.rogerSave),
        ),
      ],
    );
  }
}
