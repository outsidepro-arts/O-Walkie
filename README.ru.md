# O-Walkie

[![CI](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml/badge.svg)](https://github.com/outsidepro-arts/O-Walkie/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

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

- `backend/` - relay-сервер на Go (`WebSocket` control plane + `UDP` audio plane).
- `android/` - Android-клиент (релизный таргет).
- `windows-client/` - Windows WPF-клиент (рабочий прототип для тестирования, пока не релизный таргет).

## Что публикуем

В публичные релизы сейчас входят:

- сборка relay-сервера (`backend`);
- сборка Android-клиента (`android`).

Windows-клиент доступен в репозитории как прототип и тестовый клиент.

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
