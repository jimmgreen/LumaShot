# MP4 / GIF themed messages

## Scope

- Replace recording worker native MessageBox calls: export failure/cancellation, invalid GIF trim range, discard/re-record, discard/close, and worker startup failure.
- Replace the screenshot-to-recording launch failure message in src/app/application.cpp.
- Keep the operating-system save-file picker (and its overwrite confirmation), matching the current screenshot/PNG save flow. This change is not a custom filesystem browser.
- Leave unrelated application and pin error messages unchanged.

## Implementation

- src/ui/themed_message.h shares PanelBackground, PanelOpacity, acrylic configuration, TextRenderer and DrawControls with the application UI.
- Borderless rounded owned modal windows, light/dark palette, wrapped Chinese body text, DIP geometry and WM_DPICHANGED resizing.
- Default focus is the non-destructive choice. Escape, close and default Enter preserve the recording; Tab then Enter explicitly confirms discard.
- Disable the owner while prompting and restore its enabled/focus state afterwards. Pump asynchronous recording messages; preserve WM_QUIT. No polling/render timer in production.
- GIF range validation precedes save-path selection.
- No encoder, optimizer, audio or capture changes.

## Verification

- tests/themed_message_test.cpp exercises both themes, safe default Enter, Escape, window close, explicit keyboard confirmation, owner disabling/restoration and dialog destruction.
- tests/recording_entry_test.cpp recognizes the new recording-launch failure prompt.
- build.bat completed successfully with /W4 /WX. An initial test detected a missing shared button text-measure callback; it was fixed and the final themed_message test passed.
- Final themed_message and screenshot_recording_entry tests passed. recording_ui initially failed three existing dropdown keyboard-selection assertions; its isolated rerun passed (5.80 seconds). This intermittent result is preserved in recording-dialog-tests.log and recording-dialog-ui-recheck.log rather than hidden.
- Source diagnostics returned no errors or warnings.
- Manual visual verification and mixed-monitor DPI testing have not been performed.

## Delivery

- scripts/package.ps1 and scripts/build-installer.ps1 completed successfully.
- Installer: dist/LumaShot-Setup.exe
- SHA-256: b92fa2a914e1e29cdbfa1a3e823a8982a6dedf668510bb6d675a4c49549b8e79
- Verified that packaged LumaShot.exe and lumashot_recording_worker.exe hashes match the final build binaries.
- No installer has been run and the installed application has not been replaced.
