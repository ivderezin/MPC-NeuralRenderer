# MPC Neural Renderer

[Русский](#русский) · [English](#english)

## Русский

Нейрорендеринг видео, апскейл DLSS / FSR / XeSS и генерация кадров внутри Media Player Classic. Основа — форк [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer), GPL-3.0.

### Подключение

1. Скачайте и распакуйте Windows x64 сборку из **Actions → Artifacts** или **Releases**. Оставьте DLL AMD и Intel рядом с `.ax`.
2. Для функций NVIDIA скопируйте туда же нужные библиотеки: `nvngx_dlssnr.dll` — NR, `nvngx_dlss.dll` — DLSS SR, `nvngx_dlssg.dll` — DLSS MFG. Они не входят в архив.
3. Закройте плеер и запустите `Install_MPCVR_64.cmd` от администратора.
4. В MPC-HC x64 откройте **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. Если фильтра нет в списке, нажмите **Browse** и укажите `nvngx.dll_MpcNeuralRenderer64.ax`.
5. Откройте видео или устройство захвата. Через **Play → Filters → MPC Neural Renderer** откройте настройки, включите **Use Direct3D 11** и выберите обработку:
   - **Enable neural rendering**, **Intensity** — NR, интенсивность 0–200%.
   - **Upscaling** — Off / DLSS SR / FSR 3.1 / XeSS.
   - **Frame generation** — DLSS MFG / FSR FG / XeSS FG; **Multiplier** — Off / 2x / 3x / 4x.
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

### Сборка и тесты

1. Установите Visual Studio 2022 с **Desktop development with C++**, MFC, ATL и Windows SDK.
2. Выполните `powershell -ExecutionPolicy Bypass -File tools/Build.ps1`. Скрипт загрузит закреплённые SDK, проверит SHA-256, соберёт рендерер и скопирует DLL AMD / Intel с лицензиями.
3. Готовые файлы: `_bin/Filter_x64/`. Для сборки через Visual Studio предварительно выполните `tools/Fetch-NgxSdk.ps1` и `tools/Fetch-VendorSdks.ps1`, затем выберите **Release | x64**.
4. `tools\Test.cmd` проверяет историю кадров, настройки и миграцию, условия апскейла, расписание генерации, таймер показа, оценку движения через D3D11 WARP и преобразование цвета через D3D12 WARP. Эти же проверки и сборка выполняются в GitHub Actions.

`Source/Upscaling` — апскейлеры; `Source/FrameGeneration` — генераторы и вывод; `Source/Processing` — провайдеры, загрузка библиотек и цвет; `Source/NeuralRendering` — NR, история и координация обработки. Всё выполняется в процессе плеера.

Происхождение исходников и лицензии: [docs/upstream.md](docs/upstream.md). Исходная документация: [docs/upstream-readme.md](docs/upstream-readme.md).

## English

Video neural rendering, DLSS / FSR / XeSS upscaling, and frame generation inside Media Player Classic. Based on a fork of [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer), GPL-3.0.

### Setup

1. Download and extract the Windows x64 build from **Actions → Artifacts** or **Releases**. Keep the AMD and Intel DLLs next to the `.ax` file.
2. For NVIDIA features, copy the required libraries into the same folder: `nvngx_dlssnr.dll` for NR, `nvngx_dlss.dll` for DLSS SR, and `nvngx_dlssg.dll` for DLSS MFG. These libraries are not included in the archive.
3. Close the player and run `Install_MPCVR_64.cmd` as administrator.
4. In MPC-HC x64, open **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. If the filter is missing, click **Browse** and select `nvngx.dll_MpcNeuralRenderer64.ax`.
5. Open a video or capture device. Open the renderer settings through **Play → Filters → MPC Neural Renderer**, enable **Use Direct3D 11**, and choose the processing options:
   - **Enable neural rendering**, **Intensity** — NR with 0–200% intensity.
   - **Upscaling** — Off / DLSS SR / FSR 3.1 / XeSS.
   - **Frame generation** — DLSS MFG / FSR FG / XeSS FG; **Multiplier** — Off / 2x / 3x / 4x.
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

### Building and testing

1. Install Visual Studio 2022 with **Desktop development with C++**, MFC, ATL, and the Windows SDK.
2. Run `powershell -ExecutionPolicy Bypass -File tools/Build.ps1`. The script downloads pinned SDK files, verifies SHA-256 hashes, builds the renderer, and copies the AMD / Intel DLLs with their licenses.
3. Build output: `_bin/Filter_x64/`. To build in Visual Studio, first run `tools/Fetch-NgxSdk.ps1` and `tools/Fetch-VendorSdks.ps1`, then select **Release | x64**.
4. `tools\Test.cmd` checks frame history, settings and migration, upscaling conditions, frame-generation scheduling, the presentation timer, motion estimation through D3D11 WARP, and color conversion through D3D12 WARP. GitHub Actions runs the same checks and build.

`Source/Upscaling` contains upscalers; `Source/FrameGeneration` contains generators and presentation; `Source/Processing` contains provider selection, library loading, and color conversion; `Source/NeuralRendering` contains NR, history, and processing coordination. Everything runs inside the player process.

Source provenance and licenses: [docs/upstream.md](docs/upstream.md). Original documentation: [docs/upstream-readme.md](docs/upstream-readme.md).
