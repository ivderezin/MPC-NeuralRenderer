# DLSS FG replay

## По-русски

1. Распакуйте архив **Save DLSS FG sample**. Нужны `manifest.txt`, `previous.png`, `current.png`, `motion.raw`, `depth.raw` в одной папке.
2. Положите `DLSS-FG-Replay.exe` рядом с установленными DLL рендерера, включая `nvngx_dlssg.dll`. Закройте плеер и запустите EXE на Windows с NVIDIA GPU, поддерживающим DLSS FG.
3. Выберите папку распакованного образца. После проверки откроется новая папка `Replay-…` с PNG и `report.txt`. Для диагностики пришлите ZIP этой папки. При ошибке двойного щелчка появится окно с её текстом.

Сейчас поддерживаются только непрозрачные 8-битные SDR-образцы. Настройки плеера не меняются. Входные кадры не отправляются в сеть.

В версии 2 по умолчанию семь вариантов: исходный вызов, нулевое движение, обратный знак движения, масштаб `1,1`, цвет сцены в слоте HUD-less, тот же цвет с прозрачным UI и флаг video. Обратный знак и масштаб `1,1` намеренно нарушают соглашение для сохранённых пиксельных векторов: это контроль чувствительности SDK, а не рекомендуемые настройки. HUD-less здесь совпадает с готовым кадром; интерфейс из видео не удаляется. Прозрачный RGBA UI сохраняет исходную композицию. Флаг video может запретить интерполяцию — тогда PNG не будет, результат записывается в отчёт.

Для прежних шести вариантов используйте `DLSS-FG-Replay.exe "C:\path\to\sample" --lifetime`. Они сравнивают сохранение входных текстур и метаданные `motionVectorsDilated`; фактическая дилатация не выполняется. В отчёте v2 есть параметры каждого варианта и точное количество отличающихся RGB-компонентов относительно первого опубликованного результата.

Каждый вариант создаёт новую функцию NGX, прогревает её тремя повторениями предыдущего кадра с нулевым движением и подаёт текущий кадр с сохранённым полем. Флаг reset включён только на первом прогревочном кадре. История до захвата и профиль EXE отличаются от живого плеера, поэтому совпадение с ним не гарантировано. `midpoint_error` — приближённая фотометрическая диагностика, не оценка качества относительно эталонного кадра.

## English

Extract a **Save DLSS FG sample** ZIP. Put `DLSS-FG-Replay.exe` beside the renderer's installed runtime DLLs (including `nvngx_dlssg.dll`), close the player, run it on a DLSS FG capable NVIDIA GPU, and select the extracted sample folder. Send back the new `Replay-…` folder containing PNGs and `report.txt`. Only opaque 8-bit SDR samples are supported. Nothing is uploaded and player settings are untouched.

Version 2 defaults to seven controls: baseline, zero motion, reversed sign, unnormalized pixel scale, backbuffer also supplied as HUD-less color, HUD-less plus transparent RGBA UI, and the video flag. Reversed sign and pixel scale are deliberately nonstandard controls, not recommended settings. No HUD is removed from captured video. Transparent UI preserves the input composition. A video-flag result may be disabled by the SDK and have no PNG. Reports include variant parameters and exact changed RGB-component counts against the first published result.

Use `DLSS-FG-Replay.exe "C:\path\to\sample" --lifetime` for the original six input-retention/dilation-metadata experiments. Each fresh feature is warmed with three identical previous frames and zero motion, then receives the captured current frame and motion. Only the first warm-up frame resets history. The executable/profile and earlier history differ from the live player. Midpoint photometric error is a diagnostic, not a ground-truth quality metric.

NVIDIA documents pixel-motion normalization as `1/width,1/height`; see the [DLSS-G integration guide, sections 5 and 7](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md). These optional scene/UI experiments do not establish that an omitted optional input causes the current artifacts.

CLI: `DLSS-FG-Replay.exe "C:\path\to\sample"`. `--self-test` checks lossless image I/O, bilinear sampling and the photometric diagnostic without initializing NGX or requiring an NVIDIA GPU; this is the portion verified in CI.
