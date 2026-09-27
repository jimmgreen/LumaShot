# One-shot recording for chat

lumashot_recording_worker.exe --video|--gif --result-dir <absolute-existing-empty-directory>

The normal recording UI opens with the requested initial mode. Starting recording requires the user's action. Export bypasses the save-path dialog, writes result.mp4 or result.gif according to the actual format, then closes with exit 0. Closing without a successful export returns 2; startup/argument errors return 1 without opening a dialog. Standalone invocations without --result-dir keep their normal behavior.

The supplied directory must be empty. Scratch media is kept in its work subdirectory. Existing result files are not overwritten. The caller owns cancellation cleanup, output validation and process lifetime. Fantai attaches the worker and encoding subprocesses to a kill-on-close Windows job.

Focused validation: lumashot_recording_result_test uses synthetic GPU frames and no desktop/audio input, validates MP4 and animated GIF return, no save dialog, automatic close, cancellation, and invalid directories. Run from build/.
