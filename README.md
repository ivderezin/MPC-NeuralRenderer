# MPC Neural Renderer

[Русский](#русский) · [English](#english)

## Русский

Нейрорендеринг видео, апскейл DLSS / FSR / XeSS и генерация кадров внутри Media Player Classic. Основа — форк [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer), GPL-3.0.

### Примеры

Идея проекта — попробовать нейрорендеринг в играх Nintendo Switch 2: консоль подключается через карту захвата, а MPC-HC с этим рендерером обрабатывает картинку на ПК.

**Игры Nintendo Switch 2** — пары DLSS NR OFF / ON:

![Игра Nintendo Switch 2: DLSS NR выключен и включён](docs/images/switch2-1.jpg)

![Игра Nintendo Switch 2: DLSS NR выключен и включён](docs/images/switch2-2.jpg)

![Игра Nintendo Switch 2: DLSS NR выключен и включён](docs/images/switch2-3.jpg)

**Фильмы:**

| NR выключен | NR включён |
|---|---|
| ![Кадр фильма без NR](docs/images/film-1-off.jpg) | ![Кадр фильма с NR](docs/images/film-1-on.jpg) |
| ![Кадр фильма без NR](docs/images/film-2-off.jpg) | ![Кадр фильма с NR](docs/images/film-2-on.jpg) |

### Подключение

1. Скачайте и распакуйте Windows x64 сборку из **Actions → Artifacts** или **Releases**. Оставьте DLL AMD и Intel рядом с `.ax`.
2. Для функций NVIDIA скопируйте туда же нужные библиотеки: `nvngx_dlssnr.dll` — NR, `nvngx_dlss.dll` — DLSS SR, `nvngx_dlssg.dll` — DLSS MFG. Они не входят в архив.
3. Закройте плеер и запустите `Install_MPCVR_64.cmd` от администратора.
4. В MPC-HC x64 откройте **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. Если фильтра нет в списке, нажмите **Browse** и укажите `nvngx.dll_MpcNeuralRenderer64.ax`.
5. Откройте видео или устройство захвата. Через **Play → Filters → MPC Neural Renderer** откройте настройки, включите **Use Direct3D 11** и выберите обработку:
   - **Enable neural rendering**, **Intensity** — NR, интенсивность 0–200%.
   - **NR stage** — где выполняется NR при апскейле: **Before upscaling** (на исходном кадре, быстрее всего), **After upscaling** (на увеличенном кадре) или **Before and after** (два прохода). Без апскейла NR всегда выполняется один раз на исходном кадре.
   - **Upscaling** — Off / DLSS SR / FSR 3.1 / XeSS.
   - **Frame generation** — DLSS MFG / FSR FG / XeSS FG; **Multiplier** — Off / 2x / 3x / 4x.
   - **Present DLSS / FSR generated frames from a dedicated thread** — показ сгенерированных кадров из отдельного потока с очередью пакетов вместо таймера в UI-потоке плеера. Меньше опозданий и потерянных реальных кадров; задержка — до одного интервала между кадрами.
6. Нажмите **Apply**. Включите **Show statistics**, чтобы увидеть выбранные режимы, состояние обработки и причину отказа, если функция недоступна.

Апскейлер и генератор выбираются независимо: например, FSR + XeSS FG или XeSS + DLSS MFG. NR также включается отдельно. Это дополнительный DirectShow-рендерер: регистрация штатного MPC Video Renderer и настройки его вывода сохраняются. Для удаления закройте плеер и запустите `Uninstall_MPCVR_64.cmd` от администратора.

### Апскейл и генерация

Апскейл включается, только когда исходный кадр меньше разрешения монитора и области вывода: 1080p → 4K увеличивается, 4K → 4K — нет. NR получает полное исходное изображение. При выключенном или недоступном апскейлере кадр остаётся в нативном размере по центру; уменьшение для размещения в окне сохраняется.

FSR 3.1 FG поддерживает **2x**. DLSS и XeSS используют доступный GPU / драйверу множитель в пределах выбранного. Частота монитора дополнительно ограничивает генерацию: для 60 FPS на 160 Гц максимум 2x. Для DLSS MFG нужны совместимая RTX, драйвер NVIDIA и включённое аппаратное планирование GPU (HAGS). Для XeSS FG используется D3D12 swap chain и XeLL; включайте его в оконном или безрамочном режиме с 8-битным SDR-выводом. Поддержку конкретного GPU проверяет SDK выбранного провайдера.

Генерация работает без апскейла. При перемотке, паузе и смене размера история сбрасывается. Обработка не накапливает очередь: пока GPU занят, используется последний готовый кадр. Ошибка провайдера отображается в статистике, без автоматического переключения на другой алгоритм.

### Данные видео

- Обработка доступна для Windows x64 и SDR. Для NR и апскейла нужен 8-битный внутренний формат; HDR и стереопреобразование обходят эти стадии.
- Векторы движения оцениваются по изображению на GPU, поиск ограничен ±16 пикселями за кадр. Данных о глубине нет — используется нулевая текстура. Векторы и глубина игрового движка через видеозахват не передаются.
- NR выполняется в исходном разрешении, затем выбранный апскейлер увеличивает результат. FSR и XeSS SR получают линейный цвет. DLSS / FSR FG показывают готовые кадры по расписанию рендерера, XeSS FG управляет показом через свой swap chain.
- NR использует NGX Feature 18. Имя `.ax` содержит `nvngx.dll` для совместимости с проверкой вызывающего модуля в модели 310.8.0.0; патчей памяти и перехватов функций нет.

### Горячие клавиши и AMDLSS5

В активном окне плеера (D3D11, Windows x64):

| Клавиши | Действие |
|---|---|
| Ctrl+Alt+U | Включить / выключить выбранный апскейлер |
| Ctrl+Alt+N | Включить / выключить встроенный NR |
| Ctrl+Alt+G | Включить / выключить генерацию кадров |
| Ctrl+Alt+R | Приостановить / возобновить всю нейрообработку |

При повторном включении восстанавливаются последний апскейлер и множитель (по умолчанию DLSS и 2x). Первые три переключения сохраняются в настройках; общая приостановка действует до закрытия рендерера. Видео продолжает заполнять область просмотра с сохранением выбранных плеером пропорций, даже когда апскейлер выключен. Короткое сообщение показывает выбранный режим; фактическую доступность функций смотри в статистике. Сочетания освобождаются, когда окно плеера теряет фокус; занятое другим приложением сочетание может быть недоступно.

**Экспериментальные входы для [AMDLSS5](https://github.com/Wehaveall/AMDLSS5):** установи мод отдельно для `mpc-hc64.exe` через его установщик, выбери **FSR 3.1**, включи **AMDLSS5 bridge: FSR native AA (experimental)** и выключи встроенный NVIDIA NR. Рендерер будет вызывать FSR и при 1:1 (например, 4K → 4K). В этом режиме `amd_fidelityfx_upscaler_dx12.dll` из папки плеера имеет приоритет; если её там нет, используется DLL рядом с фильтром. Внешний NR управляется самим модом: **F10**, меню — **End**; Ctrl+Alt+N управляет только встроенным NR. Ctrl+Alt+U / R прекращают подачу кадров в FSR и внешний NR.

Это подготовка входов для внешнего мода, совместимость с MPC-HC пока не подтверждена. AMDLSS5 заявляет RX 9070 / 9070 XT и D3D12; наш вывод использует D3D11, поэтому перехват FSR и работа оверлея требуют проверки на Radeon. Файлы и код мода не входят в поставку рендерера.

### Сборка и тесты

1. Установите Visual Studio 2022 с **Desktop development with C++**, MFC, ATL и Windows SDK.
2. Выполните `powershell -ExecutionPolicy Bypass -File tools/Build.ps1`. Скрипт загрузит закреплённые SDK, проверит SHA-256, соберёт рендерер и скопирует DLL AMD / Intel с лицензиями.
3. Готовые файлы: `_bin/Filter_x64/`. Для сборки через Visual Studio предварительно выполните `tools/Fetch-NgxSdk.ps1` и `tools/Fetch-VendorSdks.ps1`, затем выберите **Release | x64**.
4. `tools\Test.cmd` проверяет историю кадров, настройки и миграцию, условия апскейла, расписание генерации, таймер показа, оценку движения через D3D11 WARP и преобразование цвета через D3D12 WARP. Эти же проверки и сборка выполняются в GitHub Actions.

`Source/Upscaling` — апскейлеры; `Source/FrameGeneration` — генераторы и вывод; `Source/Processing` — провайдеры, загрузка библиотек и цвет; `Source/NeuralRendering` — NR, история и координация обработки. Всё выполняется в процессе плеера.

Происхождение исходников и лицензии: [docs/upstream.md](docs/upstream.md). Исходная документация: [docs/upstream-readme.md](docs/upstream-readme.md).

## English

Video neural rendering, DLSS / FSR / XeSS upscaling, and frame generation inside Media Player Classic. Based on a fork of [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer), GPL-3.0.

### Examples

The project started from wanting to try neural rendering in Nintendo Switch 2 games: the console connects through a capture card, and MPC-HC with this renderer processes the image on the PC.

**Nintendo Switch 2 games** — DLSS NR OFF / ON pairs:

![Nintendo Switch 2 game with DLSS NR off and on](docs/images/switch2-1.jpg)

![Nintendo Switch 2 game with DLSS NR off and on](docs/images/switch2-2.jpg)

![Nintendo Switch 2 game with DLSS NR off and on](docs/images/switch2-3.jpg)

**Films:**

| NR off | NR on |
|---|---|
| ![Film frame without NR](docs/images/film-1-off.jpg) | ![Film frame with NR](docs/images/film-1-on.jpg) |
| ![Film frame without NR](docs/images/film-2-off.jpg) | ![Film frame with NR](docs/images/film-2-on.jpg) |

### Setup

1. Download and extract the Windows x64 build from **Actions → Artifacts** or **Releases**. Keep the AMD and Intel DLLs next to the `.ax` file.
2. For NVIDIA features, copy the required libraries into the same folder: `nvngx_dlssnr.dll` for NR, `nvngx_dlss.dll` for DLSS SR, and `nvngx_dlssg.dll` for DLSS MFG. These libraries are not included in the archive.
3. Close the player and run `Install_MPCVR_64.cmd` as administrator.
4. In MPC-HC x64, open **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. If the filter is missing, click **Browse** and select `nvngx.dll_MpcNeuralRenderer64.ax`.
5. Open a video or capture device. Open the renderer settings through **Play → Filters → MPC Neural Renderer**, enable **Use Direct3D 11**, and choose the processing options:
   - **Enable neural rendering**, **Intensity** — NR with 0–200% intensity.
   - **NR stage** — where NR runs when upscaling: **Before upscaling** (on the source frame, fastest), **After upscaling** (on the upscaled frame), or **Before and after** (two passes). Without upscaling, NR always runs once on the source frame.
   - **Upscaling** — Off / DLSS SR / FSR 3.1 / XeSS.
   - **Frame generation** — DLSS MFG / FSR FG / XeSS FG; **Multiplier** — Off / 2x / 3x / 4x.
   - **Present DLSS / FSR generated frames from a dedicated thread** — presents generated frames from a dedicated thread with a queued schedule instead of a timer on the player's UI thread. Fewer late and dropped real frames, at up to one frame interval of extra latency.
6. Click **Apply**. Enable **Show statistics** to see the selected modes, processing status, and the reason if a feature is unavailable.

Select the upscaler and frame generator independently: for example, FSR + XeSS FG or XeSS + DLSS MFG. NR is also enabled separately. This is an additional DirectShow renderer: the stock MPC Video Renderer registration and output settings are preserved. To uninstall, close the player and run `Uninstall_MPCVR_64.cmd` as administrator.

### Upscaling and frame generation

Upscaling runs only when the source frame is smaller than both the monitor resolution and the output area: 1080p → 4K is upscaled; 4K → 4K is not. NR receives the full source image. With upscaling disabled or unavailable, the image stays centered at its native size; downscaling to fit the window remains available.

FSR 3.1 FG supports **2x**. DLSS and XeSS use the GPU / driver-supported multiplier, up to the selected value. The monitor refresh rate further limits generation: 60 FPS on a 160 Hz display allows up to 2x. DLSS MFG requires a compatible RTX GPU, an NVIDIA driver, and Hardware-Accelerated GPU Scheduling (HAGS). XeSS FG uses a D3D12 swap chain and XeLL; use windowed or borderless mode with 8-bit SDR output. The selected provider's SDK checks GPU support.

Frame generation works without upscaling. Seeking, pausing, and resizing reset the history. Processing does not build up a queue: while the GPU is busy, the latest completed frame is used. Provider errors appear in the statistics without automatically switching to another algorithm.

### Video inputs and processing

- Processing is available on Windows x64 for SDR. NR and upscaling require an 8-bit internal format; HDR and stereo conversion bypass these stages.
- Motion vectors are estimated from the image on the GPU, with a search range of ±16 pixels per frame. No depth data is available, so a zero-filled texture is used. Video capture does not carry the game engine's motion vectors or depth.
- NR runs at the source resolution, followed by the selected upscaler. FSR and XeSS SR receive linear color. DLSS / FSR FG use the renderer's presentation schedule; XeSS FG manages presentation through its own swap chain.
- NR uses NGX Feature 18. The `.ax` filename contains `nvngx.dll` for compatibility with the calling-module check in model 310.8.0.0; no memory patches or function hooks are used.

### Hotkeys and AMDLSS5

With the player window active (D3D11, Windows x64):

| Shortcut | Action |
|---|---|
| Ctrl+Alt+U | Toggle the selected upscaler |
| Ctrl+Alt+N | Toggle built-in NR |
| Ctrl+Alt+G | Toggle frame generation |
| Ctrl+Alt+R | Suspend / resume all neural processing |

Re-enabling restores the last upscaler and multiplier (initially DLSS and 2x). The first three toggles save settings; suspension lasts until the renderer closes. Video keeps filling the viewport according to the player's aspect ratio setting with upscaling off. A brief message reports the selection; statistics show actual availability. Shortcuts are released when the player loses focus; a combination owned by another application may be unavailable.

**Experimental inputs for [AMDLSS5](https://github.com/Wehaveall/AMDLSS5):** install the mod separately for `mpc-hc64.exe` using its installer, select **FSR 3.1**, enable **AMDLSS5 bridge: FSR native AA (experimental)**, and disable built-in NVIDIA NR. FSR will also run at 1:1 (e.g. 4K → 4K). In this mode, `amd_fidelityfx_upscaler_dx12.dll` in the player's folder takes precedence over the DLL beside the filter. External NR uses the mod's **F10** toggle and **End** menu; Ctrl+Alt+N controls built-in NR only. Ctrl+Alt+U / R stop feeding FSR and external NR.

This prepares inputs for an external mod; MPC-HC compatibility is unverified. AMDLSS5 lists RX 9070 / 9070 XT and D3D12; our presentation uses D3D11, so FSR interception and overlay support need Radeon testing. The renderer does not bundle the mod's code or files.

### Building and testing

1. Install Visual Studio 2022 with **Desktop development with C++**, MFC, ATL, and the Windows SDK.
2. Run `powershell -ExecutionPolicy Bypass -File tools/Build.ps1`. The script downloads pinned SDK files, verifies SHA-256 hashes, builds the renderer, and copies the AMD / Intel DLLs with their licenses.
3. Build output: `_bin/Filter_x64/`. To build in Visual Studio, first run `tools/Fetch-NgxSdk.ps1` and `tools/Fetch-VendorSdks.ps1`, then select **Release | x64**.
4. `tools\Test.cmd` checks frame history, settings and migration, upscaling conditions, frame-generation scheduling, the presentation timer, motion estimation through D3D11 WARP, and color conversion through D3D12 WARP. GitHub Actions runs the same checks and build.

`Source/Upscaling` contains upscalers; `Source/FrameGeneration` contains generators and presentation; `Source/Processing` contains provider selection, library loading, and color conversion; `Source/NeuralRendering` contains NR, history, and processing coordination. Everything runs inside the player process.

Source provenance and licenses: [docs/upstream.md](docs/upstream.md). Original documentation: [docs/upstream-readme.md](docs/upstream-readme.md).
