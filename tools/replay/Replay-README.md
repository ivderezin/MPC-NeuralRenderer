# DLSS FG replay

## По-русски

1. Распакуйте архив **Save DLSS FG sample**. Нужны `manifest.txt`, `previous.png`, `current.png`, `motion.raw`, `depth.raw` в одной папке.
2. Положите `DLSS-FG-Replay.exe` рядом с установленными DLL рендерера, включая `nvngx_dlssg.dll`. Закройте плеер и запустите EXE на Windows с NVIDIA GPU, поддерживающим DLSS FG.
3. Выберите папку распакованного образца. После проверки откроется новая папка `Replay-…` с шестью PNG и `report.txt`. Для диагностики пришлите ZIP этой папки. При ошибке двойного щелчка появится окно с её текстом.

Сейчас поддерживаются только непрозрачные 8-битные SDR-образцы. Настройки плеера не меняются. Входные кадры не отправляются в сеть.

Тест сравнивает переиспользование входных текстур с сохранением отдельных текстур цвета и/или движения. Суффикс `prepared` проверяет только метаданные `motionVectorsDilated=true`: дополнительное расширение поля векторов не выполняется. Это эксперимент с контрактом SDK, а не утверждение, что поле действительно прошло дилатацию.

Каждый вариант создаёт новую функцию NGX, прогревает её тремя повторениями предыдущего кадра с нулевым движением и подаёт текущий кадр с сохранённым полем. Флаг reset включён только на первом прогревочном кадре. История до захвата и профиль EXE отличаются от живого плеера, поэтому совпадение с ним не гарантировано. `midpoint_error` — приближённая фотометрическая диагностика, не оценка качества относительно эталонного кадра.

## English

Extract a **Save DLSS FG sample** ZIP. Put `DLSS-FG-Replay.exe` beside the renderer's installed runtime DLLs (including `nvngx_dlssg.dll`), close the player, run it on a DLSS FG capable NVIDIA GPU, and select the extracted sample folder. Send back the new `Replay-…` folder containing six PNGs and `report.txt`. Only opaque 8-bit SDR samples are supported. Nothing is uploaded and player settings are untouched.

The variants compare reusing input textures with retaining separate color and/or motion textures. `prepared` only changes the SDK metadata flag `motionVectorsDilated`; it does **not** dilate the captured field. Each fresh feature is warmed with three identical previous frames and zero motion, then receives the captured current frame and motion. Only the first warm-up frame resets history. The executable/profile and earlier history differ from the live player. The midpoint photometric error is a diagnostic, not a ground-truth quality metric.

CLI: `DLSS-FG-Replay.exe "C:\path\to\sample"`. `--self-test` checks lossless image I/O, bilinear sampling and the photometric diagnostic without initializing NGX or requiring an NVIDIA GPU; this is the portion verified in CI.
