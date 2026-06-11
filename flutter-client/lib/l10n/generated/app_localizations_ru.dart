// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Russian (`ru`).
class AppLocalizationsRu extends AppLocalizations {
  AppLocalizationsRu([String locale = 'ru']) : super(locale);

  @override
  String get appName => 'O-Walkie';

  @override
  String get menuMore => 'Ещё';

  @override
  String get menuRepeaterMode => 'Режим репитера';

  @override
  String get menuSettings => 'Настройки';

  @override
  String get connectionStateDisconnected => 'Отключен';

  @override
  String get connectionStateConnecting => 'Подключение...';

  @override
  String get connectionStateReconnecting => 'Переподключение...';

  @override
  String get connectionStatePausedPhoneCall => 'Пауза (телефонный звонок)';

  @override
  String get connectionStateConnected => 'Подключен';

  @override
  String get connectionStatePartial => 'Подключен (иниц. UDP)';

  @override
  String get connectionStateProtocolIncompatible =>
      'Несовместимая версия протокола';

  @override
  String get connectionStateTransmitting => 'Передача';

  @override
  String get connectionStateReceiving => 'Прием';

  @override
  String get connectionStateScanning => 'Сканирование...';

  @override
  String get connectionStateParallelTx => 'Параллельная передача!';

  @override
  String get connectionStateCalling => 'Вызов';

  @override
  String get connectionStateUnsupported => 'Сессия недоступна';

  @override
  String get signalQualityDefault => 'Сигнал: --';

  @override
  String signalQualityPercent(int percent) {
    return 'Сигнал: $percent%';
  }

  @override
  String get signalRxActive => 'Приём активен';

  @override
  String get signalRxBusy => 'Приём занят';

  @override
  String get serverProfiles => 'Список подключений';

  @override
  String get collapseConnectionDetails => 'Свернуть данные подключения';

  @override
  String get expandConnectionDetails => 'Развернуть данные подключения';

  @override
  String get previousServer => 'Назад';

  @override
  String get nextServer => 'Вперед';

  @override
  String get connectServer => 'Подключиться';

  @override
  String get disconnectServer => 'Отключиться';

  @override
  String get scanToggle => 'Сканирование';

  @override
  String get scanning => 'Сканирование...';

  @override
  String get serverNameLabel => 'Название сервера';

  @override
  String get serverNameHint => 'Название сервера (пример: Team Alpha)';

  @override
  String get serverHostLabel => 'Хост сервера';

  @override
  String get serverHostHint => 'Хост/IP сервера';

  @override
  String get portLabel => 'Порт';

  @override
  String get portHint => 'Порт';

  @override
  String get channelLabel => 'Канал';

  @override
  String get channelHint => 'Название канала';

  @override
  String get saveServer => 'Сохранить сервер';

  @override
  String get deleteServer => 'Удалить сервер';

  @override
  String get moveServerUp => 'Выше';

  @override
  String get moveServerDown => 'Ниже';

  @override
  String get shareConnection => 'Поделиться подключением';

  @override
  String get importConnection => 'Импорт подключения из буфера';

  @override
  String get connectionLinkCopied => 'Ссылка подключения скопирована';

  @override
  String get connectionLinkIncludeNamePrompt =>
      'Добавить имя вашего подключения в ссылку?';

  @override
  String get connectionLinkInvalid =>
      'В буфере нет корректной ссылки подключения O-Walkie';

  @override
  String get connectionLinkImported => 'Подключение импортировано из ссылки';

  @override
  String get scanModeTitle => 'Режим сканирования';

  @override
  String get scanModeOneShot => 'Одноразовое';

  @override
  String get scanModeContinuous => 'Постоянное';

  @override
  String get scanStartedAnnouncement => 'Сканирование запущено';

  @override
  String get scanStoppedAnnouncement => 'Сканирование остановлено';

  @override
  String scanFoundServerAnnouncement(String name) {
    return 'Обнаружена активность на $name, выполняется подключение';
  }

  @override
  String scanFoundActivityToast(String name) {
    return 'Обнаружена активность на «$name»';
  }

  @override
  String get rxVolumeLabel => 'Громкость входящего звука';

  @override
  String get rxVolumeValueDefault => '100%';

  @override
  String rxVolumePercent(int percent) {
    return '$percent%';
  }

  @override
  String rxVolumePercentAccessibility(int percent) {
    return '$percent процентов';
  }

  @override
  String get pttHold => 'Удерживайте для разговора';

  @override
  String get pttActive => 'Передача…';

  @override
  String get pttUnavailable => 'PTT недоступна: нет соединения';

  @override
  String pttLockedCountdown(int sec) {
    return 'Заблокировано $sec с';
  }

  @override
  String pttTxCountdown(int sec) {
    return 'Подождите $sec с';
  }

  @override
  String get callSignal => 'Вызов';

  @override
  String get coreVersionFooter => 'Ядро';

  @override
  String get protocolLabel => 'протокол';

  @override
  String get settingsTitle => 'Настройки';

  @override
  String get settingsAudio => 'Аудио';

  @override
  String get settingsPauseDuringPhoneCall =>
      'Приостанавливать приём при активном звонке';

  @override
  String get settingsUseBluetoothHeadset => 'Использовать Bluetooth-гарнитуру';

  @override
  String get settingsExternalControl =>
      'Использовать внешнее управление (Tasker)';

  @override
  String get settingsWindows => 'Windows';

  @override
  String get settingsGlobalPttHotkey => 'Глобальная клавиша PTT';

  @override
  String get settingsGlobalPttHotkeyUnassigned => 'Не задана';

  @override
  String get settingsGlobalPttHotkeyAssign => 'Назначить';

  @override
  String get settingsGlobalPttHotkeyClear => 'Сбросить';

  @override
  String get settingsGlobalPttHotkeyDialogTitle =>
      'Нажмите глобальную клавишу PTT';

  @override
  String get settingsGlobalPttHotkeyDialogHint =>
      'Удерживайте для передачи; отпустите, чтобы остановить (работает в фоне).';

  @override
  String get settingsGlobalPttHotkeyDialogWaiting =>
      'Нажмите клавишу или сочетание (Ctrl/Alt/Shift).';

  @override
  String get settingsMinimizeToTray =>
      'Сворачивать в системный трей при закрытии окна';

  @override
  String get trayMenuShow => 'Показать O-Walkie';

  @override
  String get trayMenuExit => 'Выход';

  @override
  String get settingsMediaButtonPtt =>
      'Кнопка воспроизведения/паузы на гарнитуре переключает передачу';

  @override
  String get settingsHardwarePttKey => 'Аппаратная кнопка PTT';

  @override
  String get settingsHardwarePttUnassigned => 'Не задана';

  @override
  String get settingsHardwarePttAssign => 'Назначить кнопку';

  @override
  String get settingsHardwarePttDialogTitle => 'Нажмите аппаратную кнопку PTT';

  @override
  String get settingsHardwarePttDialogWaiting => 'Ожидание нажатия кнопки…';

  @override
  String get settingsHardwarePttReset => 'Сбросить';

  @override
  String get settingsDisplay => 'Экран';

  @override
  String get settingsOrientation => 'Ориентация экрана';

  @override
  String get settingsAbout => 'О приложении';

  @override
  String get settingsAppVersion => 'Версия приложения';

  @override
  String get settingsProtocolVersion => 'Версия протокола';

  @override
  String get settingsGitHub => 'Репозиторий GitHub';

  @override
  String get settingsGitHubOpenFailed => 'Не удалось открыть GitHub';

  @override
  String get orientationFollowSystem =>
      'По устройству (учитывать автоповорот системы)';

  @override
  String get orientationPortrait => 'Вертикальная';

  @override
  String get orientationLandscape => 'Горизонтальная';

  @override
  String get cannotSwitchProfileConnected =>
      'Отключитесь перед сменой профиля подключения.';

  @override
  String get cannotDeleteLastProfile => 'Нужен хотя бы один профиль.';

  @override
  String get profileSaved => 'Профиль подключения сохранён.';

  @override
  String get rogerSignalLabel => 'Сигнал Роджер';

  @override
  String get callSignalLabel => 'Сигнал вызова';

  @override
  String get rogerCustomButton => 'Свой';

  @override
  String get rogerCustomTitle => 'Свой сигнал';

  @override
  String get callCustomTitle => 'Свой сигнал вызова';

  @override
  String get rogerNameHint => 'Название сигнала';

  @override
  String get rogerPointsLabel => 'Сегменты';

  @override
  String get rogerNewSegment => 'Новый сегмент';

  @override
  String get rogerEditSegment => 'Редактировать сегмент';

  @override
  String get rogerFrequencyHint => 'Частота (Гц)';

  @override
  String get rogerDurationHint => 'Длительность (мс)';

  @override
  String get rogerPointInvalid =>
      'Введите корректные значения сегмента (частота >= 0, длительность > 0)';

  @override
  String get rogerPointsTooLong =>
      'Суммарная длина сигнала слишком большая для выбранного режима';

  @override
  String get rogerPointPause => 'Пауза';

  @override
  String rogerPointHz(int hz) {
    return '$hz Гц';
  }

  @override
  String rogerPointDurationMs(int ms) {
    return '$ms мс';
  }

  @override
  String get rogerSave => 'Сохранить';

  @override
  String get rogerCancel => 'Отмена';

  @override
  String get rogerPointsEmpty => 'Сегменты ещё не добавлены';

  @override
  String get rogerNameRequired => 'Введите название сигнала';

  @override
  String get rogerPointsRequired => 'Добавьте хотя бы один сегмент';

  @override
  String get callRepeatLabel => 'Количество повторов';

  @override
  String get patternCopy => 'Копировать сигнал';

  @override
  String get patternPaste => 'Вставить сигнал';

  @override
  String get patternCopied => 'Последовательность сигнала скопирована';

  @override
  String get patternPasteFailed =>
      'В буфере нет корректных данных сигнала O-Walkie';

  @override
  String get commonYes => 'Да';

  @override
  String get commonNo => 'Нет';

  @override
  String get commonOk => 'Ок';

  @override
  String profileNumberFallback(int number) {
    return 'Профиль $number';
  }

  @override
  String get a11yScanStateOn => 'Включено';

  @override
  String get a11yScanStateOff => 'Выключено';

  @override
  String get a11yPttHoldHint => 'Нажмите и удерживайте для передачи.';

  @override
  String get a11yPttToggleHint =>
      'Дважды нажмите, чтобы начать или остановить передачу.';

  @override
  String get a11yPttUnavailable => 'PTT недоступна: нет соединения.';

  @override
  String get a11yPttLocked => 'PTT заблокирован, пока канал занят.';

  @override
  String a11yPttCountdown(int sec) {
    return 'PTT разблокируется через $sec секунд';
  }

  @override
  String get a11yConnectUnavailableHint => 'Сессия недоступна.';

  @override
  String get a11yNotAvailableYet => 'Недоступно';

  @override
  String get a11yPttStartAction => 'Начать передачу';

  @override
  String get a11yPttStopAction => 'Остановить передачу';

  @override
  String get a11yPttLockedAnnouncement => 'Заблокировано';

  @override
  String get a11yPttUnlockedAnnouncement => 'Разблокировано';
}
