# Сопоставление MPC Neural Renderer и EchoStorm

Перенос в ветке `codex/echostorm-motion-repair`, 29 октября 2026 года. Исходник: [EchoStorm 8d3eac3](https://github.com/Echo-Storm/ls-addon-manager/tree/8d3eac351d42480326290b0f8965127a630b44f2). Это экспериментальная ветка; опубликованный `v0.1.3` её не содержит.

## Что изменилось

Ранее были адаптированы оценка движения, FlowFix и SR-маски, а NR, SR и FG выполнялись одним последовательным пакетом. Теперь добавлены собственный генератор EchoFG и независимый NR-движок с композицией добавки на свежие показываемые кадры. Исходные модули с лицензией MIT находятся в `Source/EchoStorm`; адаптеры MPC — в `Source/FrameGeneration` и `Source/NeuralRendering/EchoNr.cpp`.

| Этап | Перенесено / адаптировано | Отличие хоста MPC |
|---|---|---|
| EchoFG | Исходные D3D12 `FlowEstimator → FlowFix → OwnSynth`: пирамида, глобальное движение, субпиксельный поиск, две гипотезы, заполнение дыр, HUD, grain matching | Отдельный провайдер. Интерфейс MPC ограничен 4x; для 4K анализ половинного размера |
| Другие генераторы | DLSS MFG и FSR FG сохранены; post-FG guard перенесён из `fg_engine.cpp` | DLSS FG при выборе EchoStorm получает исходное D3D12-поле по готовой паре после NR/SR в выходном разрешении. Guard учитывает долю каждого MFG-кадра. Скрытые промежуточные кадры XeSS этим проходом недоступны |
| NR | Исходные `NrEngine`, `Bridge`, `Compose11`, `AutoQuality` и отдельный forwarder DLL | Собственная D3D12-очередь на том же адаптере. Базовые кадры не ждут завершения модели |
| Размер NR | Уменьшенный proxy и автоматический бюджет качества | Потолок ширины 1920, бюджет 5 мс, минимальный scale 0.25. Ограничение по измеренной загрузке GPU не включено: MPC не передаёт этот счётчик |
| Композиция NR | Добавка model − proxy, перенос по движению, временная обработка исходного движка, dark/highlight/max-delta guards | На каждом доступном реальном/сгенерированном кадре перед субтитрами. Поворот/отражение и BGRA swapchain адаптированы к MPC |
| Поля движения NR | Собственное поле модели из исходного движка | Двунаправленное поле LSFG не поступает: используется предусмотренный upstream fallback `motion`. LSFG ghost guard, требующий двух направлений, без них не включается |
| Общая оценка NR/SR | Исходные D3D12 FlowEstimator и FlowFix; оценка и экспорт векторов/масок выполняются в одной очереди с NR/SR | Сохранены DLSS bias-current-colour, FSR reactive, XeSS responsive и post-SR lean. Векторы NR после SR масштабируются на GPU. Старый D3D11-оценщик EchoStorm больше не вызывается рабочим конвейером; Fast остаётся отдельным режимом |
| Повторы | Точное GPU-сравнение всего RGB в исходной области кадра, включая один изменённый пиксель | Повторный готовый пакет не публикуется. Уже отправленные проходы SDK завершаются; это не пропуск всей работы до отправки. Следующий интервал считается от последнего различного изображения |
| Пейсинг | Сглаживание интервала и согласование со стабильным измеренным refresh по принципу `SampleDisplay / AlignFits / SnapToRefresh` | Реализация расписания адаптирована к DirectShow и ограниченной очереди MPC. Множитель не уменьшается; отдельный monitor cap остаётся опциональным и выключенным по умолчанию |
| HDR | SDR-view и возврат добавки в исходное HDR-кодирование из Compose11; HDR-aware EchoFG и FG guard | MPC передаёт HDR10/PQ, SDR white 203 нит. scRGB-вывод MPC этим путём не вводится |
| Переключатели | Независимый NR включён по умолчанию; EchoFG — новый выбор по умолчанию | Сохранённый выбор DLSS/FSR/XeSS не перезаписывается. Старый последовательный NR доступен при выключении `EchoStorm independent NR / compose at presentation` |

«Весь конвейер» здесь означает согласованный рабочий путь FG + guards + повторы + пейсинг + независимый NR. Внутренние хуки Lossless Scaling, его swapchain и все необязательные интерфейсные настройки не копируются в DirectShow.

По результату проверки пользователя EchoFG без independent compose работает хорошо. Артефакты DLSS FG сохраняются и без independent compose. Следующий фикс проверяется раздельно: нативное поле движения для DLSS и проверка NR-проекции по яркости proxy, возрасту и границам изображения. Проверка яркости уменьшает риск ошибочного переноса, но не заменяет двунаправленные поля LSFG и не доказывает правильность каждого пикселя.

## Проверки и пределы вывода

`tools/Test.cmd` проверяет точные повторы, один изменённый канал, ROI, NaN и сброс истории; RGBA/BGRA-композицию NR на D3D11 WARP; расписание и стабильную фазу refresh; полный синтез трёх EchoFG-кадров и guard каждой временной доли на D3D12 WARP. Release x64 собирается в GitHub Actions.

WARP не выполняет модель NVIDIA и не доказывает устранение игровых артефактов. Реальную работу нового NR, нагрузку на RTX и повороты камеры нужно проверить на пользовательской системе. Сообщение в README о проверенном `v0.1.3` относится только к тому релизу.

Для сравнения в плеере: `Frame generation = EchoFG`, `Multiplier = 4x`, `EchoStorm independent NR / compose at presentation = on`, monitor cap — по желанию. Проверить один поворот камеры с NR on/off; затем сравнить с DLSS MFG. Смотреть Processing, Output, DXGI shown, счётчик повторов и состояние Echo NR.

## Исходники

- [EchoFG / FSR FG и guard](https://github.com/Echo-Storm/ls-addon-manager/blob/8d3eac351d42480326290b0f8965127a630b44f2/addons/DLSS5NR01/src/engine/fg_engine.cpp)
- [OwnSynth](../Source/EchoStorm/engine/own_synth.h), [NR engine](../Source/EchoStorm/engine/nr_engine.h), [Bridge](../Source/EchoStorm/addon/bridge.h), [Compose11](../Source/EchoStorm/addon/compose11.h)
- [EchoFG adapter](../Source/FrameGeneration/EchoFrameGeneration.cpp), [NR adapter](../Source/NeuralRendering/EchoNr.cpp), [FG guard](../Source/FrameGeneration/EchoGuard.cpp), [точные повторы](../Source/FrameGeneration/ExactInputRepeat.h), [часы презентации](../Source/FrameGeneration/EchoPresentationClock.h)
- [MIT license EchoStorm](../Source/EchoStorm/EchoStorm-LICENSE.txt)

DLSS FG follow-up: the capture fallback now uses an internally consistent reversed-depth orthographic projection/inverse and a finite constant plane at depth 0.5, instead of identity projection matrices plus zero depth. This is not game geometry and does not establish the cause of the reported artifacts. FSR, XeSS and EchoFG are unchanged by this adjustment. Statistics enumerate loaded DLSS-G module paths and file versions; an unidentified NGX alias is reported explicitly.

DLSS post-FG isolation: `DLSS FG: protect slow foreground (off = SDK output)` defaults on. Off skips writes to generated surfaces while keeping the guard endpoint history current. Applying only this toggle preserves the DLSS feature and temporal history. Other providers and guards are unchanged. This permits direct comparison of SDK results before/after this specific adaptation. The user clarified that EchoDLSS does not use NVIDIA FG, so its clean result is not a working NVIDIA-FG reference implementation.
