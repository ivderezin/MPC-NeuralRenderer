# MPC Neural Renderer

[Русский](#русский) · [English](#english) · [Download](https://github.com/ivderezin/MPC-NeuralRenderer/releases/latest)

## Русский

Рендерер для MPC-HC: нейрорендеринг видео, апскейл и генерация кадров. Основан на [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer).

### Возможности

- **Нейрорендеринг NVIDIA:** интенсивность 0–200%, независимая обработка с наложением результата при показе или обработка до/после апскейла.
- **AMD NR:** опциональный экспериментальный HIP/ZLUDA-бэкенд перед апскейлом. [Установка runtime](docs/amd-nr.md).
- **Апскейл:** DLSS SR, FSR 3.1, XeSS. При недоступном DLSS автоматически используется FSR.
- **Генерация кадров:** EchoFG — по умолчанию; также доступны DLSS MFG, FSR FG и XeSS FG. EchoFG поддерживает 2x / 3x / 4x, FSR FG — 2x; возможности DLSS/XeSS зависят от GPU и драйвера.
- **Оценка движения:** EchoStorm coarse-to-fine — по умолчанию; Fast block matching — облегчённый вариант.
- **SDR и HDR:** 8/10-битный SDR, HDR10, HLG, Dolby Vision и RTX Video HDR; защита отдельных артефактов DLSS FG в SDR/HDR10.
- **Настройки вывода:** независимый выбор NR, апскейлера и генератора; отдельный поток показа; опциональное ограничение FG частотой монитора.
- **HLSL-шейдеры:** цепочки до и после масштабирования, применяемые из свойств рендерера.
- **AMDLSS5 bridge:** экспериментальный режим FSR native AA, включая обработку без увеличения разрешения.

### Требования и установка

**Windows 10/11 x64, MPC-HC x64, Direct3D 11 и совместимый GPU/драйвер.** Для штатного NR и DLSS нужна NVIDIA; AMD NR требует отдельного runtime и HIP SDK. Поддержку отдельных функций определяет соответствующий SDK.

1. Скачайте архив из [Releases](https://github.com/ivderezin/MPC-NeuralRenderer/releases/latest) и распакуйте в постоянную папку. DLL AMD/Intel включены.
2. Для функций NVIDIA добавьте рядом с рендерером: `nvngx_dlssnr.dll` — NR, `nvngx_dlss.dll` — DLSS SR, `nvngx_dlssg.dll` — DLSS FG. Эти библиотеки устанавливаются отдельно.
3. Закройте плеер и запустите `Install_MPCVR_64.cmd` от администратора.
4. В MPC-HC откройте **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. При необходимости укажите `nvngx.dll_MpcNeuralRenderer64.ax` через **Browse**.
5. Откройте видео, затем **Play → Filters → MPC Neural Renderer**. Включите **Use Direct3D 11**, выберите обработку и нажмите **Apply**.

Для обновления закройте плеер и распакуйте малый **Update**-архив поверх папки рендерера. Сохранённые настройки остаются в силе. Для удаления используйте `Uninstall_MPCVR_64.cmd`.

### Настройка

- **Enable neural rendering / Intensity** — включение и сила NR. **EchoStorm independent NR / compose at presentation** — независимый режим; при его отключении **NR stage** выбирает обработку до, после или с обеих сторон апскейла.
- **Upscaling / Frame generation / Multiplier** — выбор алгоритмов и множителя. Апскейл работает при увеличении изображения; генерация доступна и без апскейла.
- **Limit frame generation to monitor refresh rate** — ограничение герцовкой, по умолчанию выключено. **Present … from a dedicated thread** — показ из отдельного потока.
- **DLSS FG: protect interpolation** — защита отдельных выбросов и медленного переднего плана. **Show statistics** — фактические режимы, частота кадров и состояние провайдеров.
- **Pixel shaders** — добавление и очистка pre/post-resize HLSL-цепочек. Шейдеры действуют в текущем сеансе.

В HDR нейрорендеринг и апскейл используют SDR-представление с восстановлением HDR в линейном свете; вывод — 10-битный PQ / BT.2020. XeSS FG требует 8-битного SDR-вывода. Движение оценивается по изображению: игровые векторы и глубина недоступны.

| Горячая клавиша | Действие |
|---|---|
| Ctrl+Alt+N | NR |
| Ctrl+Alt+U | Апскейл |
| Ctrl+Alt+G | Генерация кадров |
| Ctrl+Alt+R | Приостановить / возобновить всю нейрообработку |

### Примеры

Nintendo Switch 2 через карту захвата — NR OFF / ON:

![Nintendo Switch 2 — NR OFF / ON](docs/images/switch2-1.jpg)
![Nintendo Switch 2 — NR OFF / ON](docs/images/switch2-2.jpg)
![Nintendo Switch 2 — NR OFF / ON](docs/images/switch2-3.jpg)

| NR OFF | NR ON |
|---|---|
| ![Film — NR OFF](docs/images/film-1-off.jpg) | ![Film — NR ON](docs/images/film-1-on.jpg) |
| ![Film — NR OFF](docs/images/film-2-off.jpg) | ![Film — NR ON](docs/images/film-2-on.jpg) |

## English

An MPC-HC renderer with neural video rendering, upscaling and frame generation. Based on [MPC Video Renderer](https://github.com/Aleksoid1978/VideoRenderer). [Examples](#примеры).

### Features

- **NVIDIA neural rendering:** 0–200% intensity, independent processing with presentation-time composition or processing before/after upscaling.
- **AMD NR:** optional experimental HIP/ZLUDA backend before upscaling. [Runtime setup](docs/amd-nr.md).
- **Upscaling:** DLSS SR, FSR 3.1 and XeSS. FSR is used automatically when DLSS is unavailable.
- **Frame generation:** EchoFG by default, plus DLSS MFG, FSR FG and XeSS FG. EchoFG supports 2x / 3x / 4x; FSR FG supports 2x. DLSS/XeSS capabilities depend on the GPU and driver.
- **Motion estimation:** EchoStorm coarse-to-fine by default; Fast block matching as a lighter alternative.
- **SDR and HDR:** 8/10-bit SDR, HDR10, HLG, Dolby Vision and RTX Video HDR; sparse DLSS FG artifact protection in SDR/HDR10.
- **Presentation:** independent NR/upscaler/generator selection, dedicated presentation thread and an optional monitor refresh cap.
- **HLSL shaders:** pre/post-resize chains managed from renderer properties.
- **AMDLSS5 bridge:** experimental FSR native AA, including processing at native resolution.

### Requirements and setup

**Windows 10/11 x64, MPC-HC x64, Direct3D 11 and a compatible GPU/driver.** Native NR and DLSS require NVIDIA; AMD NR requires a separate runtime and HIP SDK. Each SDK determines feature support.

1. Download a [release](https://github.com/ivderezin/MPC-NeuralRenderer/releases/latest) and extract to a permanent folder. AMD/Intel runtimes are included.
2. For NVIDIA features, add the separately installed libraries: `nvngx_dlssnr.dll` for NR, `nvngx_dlss.dll` for DLSS SR and `nvngx_dlssg.dll` for DLSS FG.
3. Close the player and run `Install_MPCVR_64.cmd` as administrator.
4. In MPC-HC, select **Options → External Filters → Add Filter → MPC Neural Renderer → Prefer**. Use **Browse** to select `nvngx.dll_MpcNeuralRenderer64.ax` if needed.
5. Open a video, then **Play → Filters → MPC Neural Renderer**. Enable **Use Direct3D 11**, select processing options and click **Apply**.

To update, close the player and extract the small **Update** archive over the renderer folder. Saved settings are preserved. Use `Uninstall_MPCVR_64.cmd` to uninstall.

### Settings

- **Enable neural rendering / Intensity:** NR and its strength. **EchoStorm independent NR / compose at presentation:** independent mode; when disabled, **NR stage** selects before, after or both sides of upscaling.
- **Upscaling / Frame generation / Multiplier:** algorithms and multiplier. Upscaling runs when enlarging the image; frame generation also works without upscaling.
- **Limit frame generation to monitor refresh rate:** optional cap, off by default. **Present … from a dedicated thread:** dedicated presentation.
- **DLSS FG: protect interpolation:** sparse outlier and slow-foreground protection. **Show statistics:** effective modes, frame rates and provider status.
- **Pixel shaders:** add or clear pre/post-resize HLSL chains for the current session.

HDR NR/upscaling use an SDR representation with linear-light HDR reconstruction and 10-bit PQ / BT.2020 output. XeSS FG requires 8-bit SDR output. Motion is estimated from images; game-engine vectors and depth are unavailable.

| Shortcut | Action |
|---|---|
| Ctrl+Alt+N | NR |
| Ctrl+Alt+U | Upscaling |
| Ctrl+Alt+G | Frame generation |
| Ctrl+Alt+R | Suspend / resume all neural processing |

## Development

Visual Studio 2022 with C++, MFC, ATL and Windows SDK. Build: `powershell -ExecutionPolicy Bypass -File tools/Build.ps1`. Output: `_bin/Filter_x64/`.

[Replay tool](tools/replay/Replay-README.md) · [Motion pipeline](docs/echo-storm-comparison.md) · [Source provenance](docs/upstream.md) · [GPL-3.0](LICENSE.txt)
