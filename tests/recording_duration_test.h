#pragma once
// Included by recording_ui_test.cpp to exercise the production Ui and Session.
#include "gif_test_helpers.h"
#include <mfreadwrite.h>
#include <exception>
#include <cstdlib>

namespace {
int GifDurationTest(bool policy_only) {
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Duration test COM");
    bool media_started = false;
    try {
        Ui ui;
        ui.gif = true;
        ui.fps = 15;
        ui.width = 0;
        ui.system = false;
        ui.microphone = false;
        ui.cursor = false;
        ui.software = true;
        const auto gif_options = ui.CaptureOptions();
        Expect(gif_options.duration_limit == 0, "GIF capture has no forced duration limit");
        Expect(gif_options.fps == 25 && gif_options.width == 0 && !gif_options.system_audio,
               "GIF source keeps 25 fps for later presets, original size and silent input");
        ui.gif = false;
        Expect(ui.CaptureOptions().duration_limit == 0, "video capture remains unlimited");
        ui.gif = true;
        if (!policy_only && !failures) {
            Check(MFStartup(MF_VERSION), "Duration test media startup");
            media_started = true;
            const auto directory = std::filesystem::current_path() / L"gif-duration-test-output" /
                std::to_wstring(GetCurrentProcessId());
            std::filesystem::create_directories(directory);
            ui.folder = directory;
            ui.temporary = directory / L"capture.mp4";
            WNDCLASSW wc{};
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpfnWndProc = Ui::Proc;
            wc.lpszClassName = L"LumaShot.GifDurationFixture";
            RegisterClassW(&wc);
            ui.window = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Synthetic duration fixture",
                WS_POPUP, 0, 0, 316, 48, nullptr, nullptr, wc.hInstance, &ui);
            if (!ui.window) throw std::runtime_error("Cannot create duration test window");
            struct WindowCleanup {
                Ui& ui;
                ~WindowCleanup() { if (ui.window) { DestroyWindow(ui.window); ui.window = nullptr; } }
            } window_cleanup{ui};
            std::mutex result_mutex;
            Status latest;
            struct SessionCleanup {
                Ui& ui;
                ~SessionCleanup() { ui.session.reset(); }
            } session_cleanup{ui};
            auto options = gif_options;
            options.synthetic = true;
            options.synthetic_size = {320, 180};
            options.synthetic_frames = 25 * 90; // Test-only watchdog, not an application limit.
            ui.session = std::make_unique<Session>();
            ui.session->Start(options, ui.temporary, [&](Status value) {
                { std::lock_guard lock(result_mutex); latest = value; }
                // Exercise active production messages without opening a playback window.
                if (value.state != State::Preview && value.state != State::Failed) {
                    { std::lock_guard lock(ui.mutex); ui.pending = value; }
                    PostMessageW(ui.window, StatusMessage, 0, 0);
                }
            });
            const auto read_status = [&] { std::lock_guard lock(result_mutex); return latest; };
            const auto started = GetTickCount64();
            bool pause_sent = false, resumed = false, legacy_timer_sent = false, manual_stop = false;
            ULONGLONG pause_started = 0;
            long long paused_time = 0;
            unsigned paused_frames = 0;
            while (GetTickCount64() - started < 95000ULL) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                const auto current = read_status();
                if (current.state == State::Preview || current.state == State::Failed) break;
                if (!pause_sent && current.state == State::Recording && current.time >= 20000000LL) {
                    ui.Action(20);
                    pause_sent = true;
                }
                if (current.state == State::Paused && !resumed) {
                    if (!pause_started) {
                        pause_started = GetTickCount64();
                        paused_time = current.time;
                        paused_frames = current.frames;
                    } else if (GetTickCount64() - pause_started >= 300ULL) {
                        Expect(current.time == paused_time && current.frames == paused_frames,
                               "pause keeps media time and encoded frame count unchanged");
                        ui.Action(20);
                        resumed = true;
                    }
                }
                if (!legacy_timer_sent && current.state == State::Recording && current.time >= 610000000LL) {
                    SendMessageW(ui.window, WM_TIMER, 2, 0);
                    legacy_timer_sent = true;
                }
                if (!manual_stop && current.state == State::Recording && current.time >= 650000000LL) {
                    ui.Action(21);
                    manual_stop = true;
                }
                MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            ui.session.reset();
            const auto result = read_status();
            if (result.state == State::Failed) std::wcerr << result.detail << std::endl;
            Expect(resumed && legacy_timer_sent && manual_stop,
                   "GIF survives the one-minute boundary and old timer message until manual stop");
            Expect(result.state == State::Preview && result.time >= 650000000LL,
                   "manual stop finalizes more than 65 seconds of synthetic media");
            std::cout << "Recorded media seconds=" << double(result.time) / 10000000.0
                      << " frames=" << result.frames << " dropped=" << result.dropped << std::endl;
            if (result.state == State::Preview && manual_stop) {
                ComPtr<IMFSourceReader> reader;
                Check(MFCreateSourceReaderFromURL(ui.temporary.c_str(), nullptr, &reader), "Read long clip");
                PROPVARIANT duration{};
                Check(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &duration),
                      "Read long clip duration");
                Expect(duration.uhVal.QuadPart > 600000000ULL, "finalized MP4 really contains over a minute");
                PropVariantClear(&duration);
                reader.Reset();
                GifOptions gif;
                gif.fps = 5;
                gif.width = 160;
                gif.end = result.time;
                const auto export_to = [&](const std::filesystem::path& path, std::stop_token token,
                                           const std::function<void(int)>& progress) {
                    std::exception_ptr error;
                    std::jthread thread([&] {
                        try { ExportGifToFile(ui.temporary, path, gif, token, progress); }
                        catch (...) { error = std::current_exception(); }
                    });
                    thread.join();
                    if (error) std::rethrow_exception(error);
                };
                int progress = 0;
                const auto output = directory / L"over-one-minute.gif";
                export_to(output, {}, [&](int value) { progress = value; });
                const auto decoded = gif_test::Decode(output);
                Expect(progress == 100 && decoded.width == 160 && decoded.height == 90 && decoded.duration > 6000,
                       "full GIF pipeline exports and independently decodes over a minute");
                Expect(std::llabs(static_cast<long long>(decoded.duration) - result.time / 100000) <= 25,
                       "long GIF preserves clip duration within one output-frame tolerance");
                const auto canceled_output = directory / L"preserved-destination.gif";
                const std::vector<BYTE> original{'k', 'e', 'e', 'p'};
                gif_test::Write(canceled_output, original);
                std::stop_source cancel;
                bool canceled = false;
                try {
                    export_to(canceled_output, cancel.get_token(), [&](int value) {
                        if (value >= 20) cancel.request_stop();
                    });
                } catch (const std::exception&) { canceled = cancel.stop_requested(); }
                Expect(canceled && gif_test::Bytes(canceled_output) == original,
                       "canceling a long GIF export preserves the previous destination");
                ui.status = result;
                ui.status.state = State::Recording;
                Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, ui.factory.GetAddressOf()), "Duration panel factory");
                Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(ui.write.GetAddressOf())), "Duration panel fonts");
                Snapshot(ui, (directory / L"after-one-minute.png").c_str());
                std::cout << "Decoded GIF seconds=" << double(decoded.duration) / 100.0
                          << " GIF frames=" << decoded.count << std::endl;
                std::wcout << L"Duration fixtures: " << directory.wstring() << std::endl;
            }
        }
    } catch (const std::exception& error) { std::cerr << error.what() << std::endl; ++failures; }
    if (media_started) MFShutdown();
    CoUninitialize();
    return failures ? 1 : 0;
}
}
