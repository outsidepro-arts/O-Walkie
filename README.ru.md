# O-Walkie

[![CI](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml/badge.svg)](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Этот проект создан в полной коллаборации с LLM-моделями и является вайб-кодинг-проектом.**

O-Walkie - это децентрализованная lo-fi walkie-talkie платформа.  
Проект сфокусирован на низкой задержке, практичном PTT-потоке и атмосферных радио-артефактах вместо hi-fi качества.

English version: [README.md](README.md)

## Статус первого релиза

Это первый публичный релиз проекта.

- внешние pull request приветствуются;
- issues и обсуждения приветствуются;
- протокол и UX пока активно развиваются.

## Почему O-Walkie?

O-Walkie намеренно моделирует поведение "живой рации":

- шумовой фон и деградацию сигнала;
- поведение squelch и tail;
- TX-клики и искажения;
- настраиваемую DSP-цепочку на стороне relay.

Такой подход дает более "живое" радио-ощущение в коммуникации.

## Компоненты проекта

- `backend/` — relay-сервер на Go (`WebSocket` control plane + `UDP` audio plane).
- `owalkie-core/` — общая C/C++ библиотека relay (managed-сессии, Opus, probe активности, Roger/Call PCM).
- `android/` — Android-клиент (релизный таргет); нативный relay через JNI → `owalkie-core`.
- `windows-client-cpp/` — Windows desktop-клиент (`wxWidgets` + miniaudio); рабочий прототип, пока не релизный таргет.

### Возможности клиентов

| Область | Android | Windows (`windows-client-cpp`) |
|---------|---------|--------------------------------|
| Транспорт | JNI `owalkie-core` (всегда) | managed-сессия `owalkie-core` |
| Сканирование каналов | Переключатель с режимами **одноразовое** / **постоянное**; нативный `has_activity`; текущий профиль пропускается при соединении; переключение только в простое (без RX/TX), иначе toast + вибрация | — |
| Busy mode PTT | `ptt_lock` / `ptt_unlock` с сервера; декоративный отсчёт; TalkBack: отсчёт при фокусе, «Заблокировано»/«Разблокировано» при фокусе на PTT | Блокировка с сервера; отсчёт на PTT; разблокировка по состоянию core |
| Фон | Foreground service; опция **держать микрофон готовым** для быстрого PTT | Опция **сворачивать в системный трей** при закрытии (настройки); меню трея Показать / Выход |
| Accessibility | PTT и переключатель сканирования: стабильные имена и состояние Вкл/Выкл для TalkBack | Порядок Tab и имена для скринридеров на основных элементах |

## Что публикуем

В публичные релизы сейчас входят:

- сборка relay-сервера (`backend`);
- сборка Android-клиента (`android`).

Windows-клиент (`windows-client-cpp`) доступен в репозитории как прототип и тестовый клиент.

## Быстрый старт

### Relay-сервер

Полная инструкция в `backend/README.md`.

### Android-клиент

Из каталога `android/`:

- `./gradlew :app:assembleDebug`
- `./gradlew :app:assembleRelease`

## Contribution

Перед отправкой pull request прочитайте [CONTRIBUTING.md](CONTRIBUTING.md).

## Контакты

- E-mail: `denis.outsidepro@gmail.com`
- Telegram: [@outsidepro](https://t.me/outsidepro)

## Лицензия

Лицензия проекта: [MIT](LICENSE).

## Сторонние лицензии

Проект использует внешние библиотеки со своими лицензиями.  
При распространении бинарных сборок учитывайте лицензионные требования зависимостей.
