# Optional AMD neural rendering

## Русский

**AMD NR runtime (experimental, before upscaling)** подключает внешний
[dlss5nr runtime](https://github.com/RedDukeDev/dlss5-nr-amd-custom) напрямую,
без OptiScaler. По умолчанию выключен. Нужны совместимый Radeon, Windows x64,
AMD HIP SDK и полный пакет runtime с ядрами и кэшем для вашей GPU.

1. Закройте плеер. Из пакета upstream скопируйте содержимое
   `OptiScaler/dlss5nr` в папку `amd-nr` рядом с `.ax`. Не копируйте загрузчики
   OptiScaler (`dxgi.dll`, `version.dll` и т. п.) для этого режима.
2. Сохраните структуру пакета:

   ```text
   amd-nr/
     dlss5nr_runtime.dll
     nvngx_dlssnr.dll
     zluda/       (nvcuda.dll, zluda_real.dll, nvapi64.dll, nvngx.dll и зависимости)
     kernels/     (kernels.txt и ядра HIP)
     cache/ComputeCache/
   ```

3. В свойствах рендерера включите **Use Direct3D 11**, **Enable neural rendering**
   и **AMD NR runtime (experimental, before upscaling)**, затем **Apply**.
4. **Show statistics** показывает запуск, состояние модели и причину отказа.
   При неполном пакете или несовместимом кэше NR не применяется; видео, SR и FG
   продолжают работать. После установки зависимостей переоткройте видео.

Модель работает асинхронно перед апскейлом; **Intensity** регулирует силу эффекта.
В этом режиме **NR stage** и NVIDIA-композиция EchoStorm не используются.
Их сохранённые настройки восстанавливаются при отключении AMD NR.
Для отслеживания передаются оценённые векторы движения и плоская глубина 0.5,
а не геометрия игры. На перекрытиях объектов возможны ошибки переноса.
HDR проходит через SDR-представление и существующее восстановление HDR.
Целевая ширина модели — 1280 пикселей, масштаб ограничен диапазоном 0.25–1.

## English

The optional **AMD NR runtime (experimental, before upscaling)** setting calls
the external runtime directly, without OptiScaler. It is off by default.
Install the AMD HIP SDK and place the complete upstream `OptiScaler/dlss5nr`
directory contents in `amd-nr` beside the renderer, preserving `zluda`, `kernels`
and `cache/ComputeCache`. Use a matching model/kernel/cache bundle covering
your Radeon. Do not install OptiScaler injection DLLs for this integration.

Enable Direct3D 11, neural rendering and the AMD runtime checkbox, then Apply.
Statistics report asynchronous startup or the dependency/runtime error. Reopen
the video after installing dependencies. An unavailable backend leaves base
video processing intact. Intensity remains available; NR stage and the native
NVIDIA presentation compositor are not used by this backend. Their saved choices
are preserved. Estimated motion plus a flat depth plane provide approximate
tracking; game geometry is unavailable. HDR uses the renderer's SDR proxy and
HDR reconstruction. Model width targets 1280 pixels at a 0.25–1 scale.

## Interface and validation

The vendored C header and MIT notice are from
[`ab455d179922adc11f65675d399e1d553bf423f0`](https://github.com/RedDukeDev/dlss5-nr-amd-custom/tree/ab455d179922adc11f65675d399e1d553bf423f0).
Only that interface is included here; model binaries, ZLUDA, HIP kernels and
translation caches are installed separately with their upstream licenses.
No translation or downloads are enabled by the renderer.

Host tests use a test-only C API DLL and D3D12 WARP to exercise startup passthrough,
resource states, submit notification, settings, failure reporting and GPU-idle
destruction. They do not execute the AMD network. Radeon RX 7900 XT operation
was also confirmed in a hardware playback test; other GPUs and runtime bundles
may behave differently.
