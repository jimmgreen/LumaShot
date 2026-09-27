"""Windows A/B regression using only generated synthetic media and the production exporter.
Run --phase full, then slim, then compare. Full ffmpeg/ffprobe are development tools only.
"""
import argparse
import hashlib
import json
import re
import subprocess
import shutil
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--phase', choices=['full', 'slim', 'compare'], required=True)
    ap.add_argument('--folder', default='build/ffmpeg-slim-validation')
    ap.add_argument('--probe', default='build/lumashot_mp4_codec_probe.exe')
    ap.add_argument('--ffprobe', default='C:/ffmpeg/bin/ffprobe.exe')
    a = ap.parse_args()
    folder = Path(a.folder).resolve()
    probe = Path(a.probe).resolve()
    reference = folder / 'full/ffmpeg.exe'
    runtime = folder / a.phase / 'ffmpeg.exe'
    folder.mkdir(parents=True, exist_ok=True)

    def run(args, log, timeout=1000):
        with (folder / log).open('wb') as out:
            subprocess.run([str(v) for v in args], cwd=folder, stdout=out, stderr=subprocess.STDOUT,
                           check=True, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)

    def inspect(path, *options):
        output = subprocess.check_output([a.ffprobe, '-v', 'error', *options, '-of', 'json', str(path)],
                                         timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
        return json.loads(output)

    def metadata(path):
        streams = inspect(path, '-show_streams')['streams']
        frames = inspect(path, '-select_streams', 'v:0', '-show_frames', '-show_entries',
                         'frame=best_effort_timestamp_time')['frames']
        packets = inspect(path, '-select_streams', 'a:0', '-show_packets', '-show_entries',
                          'packet=pts_time,dts_time,duration_time,size,data_hash',
                          '-show_data_hash', 'sha256')['packets']
        return dict(video=next(s for s in streams if s['codec_type'] == 'video'),
                    audio=next(s for s in streams if s['codec_type'] == 'audio'),
                    times=[float(f['best_effort_timestamp_time']) for f in frames], packets=packets)

    def verify_timing(src, dst):
        assert len(src['times']) == len(dst['times']) > 0, 'frame count mismatch'
        error = max(abs(x-y) for x, y in zip(src['times'], dst['times']))
        assert error <= .001, ('video timestamp mismatch', error)
        for key in ['width', 'height']:
            assert src['video'][key] == dst['video'][key], key
        for kind in ['video', 'audio']:
            for key in ['start_time', 'duration']:
                assert abs(float(src[kind].get(key, 0))-float(dst[kind].get(key, 0))) <= .001, (kind, key)
        for key in ['codec_name', 'sample_rate', 'channels', 'channel_layout']:
            assert src['audio'].get(key) == dst['audio'].get(key), ('audio', key)
        assert len(src['packets']) == len(dst['packets']) > 0, 'AAC packet count'
        audio_error = 0
        for p, q in zip(src['packets'], dst['packets']):
            assert p['data_hash'] == q['data_hash'] and p['size'] == q['size'], 'AAC packet payload'
            for key in ['pts_time', 'dts_time', 'duration_time']:
                delta = abs(float(p[key])-float(q[key]))
                audio_error = max(audio_error, delta)
                assert delta <= .001, ('audio packet timing', key, delta)
        return dict(frames=len(src['times']), max_video_timestamp_delta_ms=1000*error,
                    aac_packets=len(src['packets']), max_audio_timestamp_delta_ms=1000*audio_error,
                    aac_packet_hashes_equal=True)

    if a.phase == 'compare':
        full = json.loads((folder / 'full-results.json').read_text())
        slim = json.loads((folder / 'slim-results.json').read_text())
        assert full['fixtures'] == slim['fixtures'], 'A/B inputs differ'
        comparisons = []
        assert len(full['results']) == len(slim['results']) == 4
        for p, q in zip(full['results'], slim['results']):
            assert p['case'] == q['case'] and p['encoding'] == q['encoding'], 'changed adoption or fallback'
            identical = p['sha256'] == q['sha256']
            # Same revisions/settings: fail closed on any output change for explicit review.
            assert identical, ('A/B output is not byte-identical; manual investigation required', p['case'])
            comparisons.append(dict(case=p['case'], encoding=p['encoding'], bytes=p['bytes'],
                                    byte_identical=True, sha256=p['sha256'],
                                    full_elapsed_ms=p['elapsed_ms'], slim_elapsed_ms=q['elapsed_ms']))
        result = dict(passed=True, comparisons=comparisons,
                      limits='Synthetic fixtures only; 4K60 is a one-second upscale functional/resource check, not a long native-4K stress test.')
        (folder / 'comparison.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result, indent=2), flush=True)
        return

    source = folder / 'source-1080p.mp4'
    if not source.exists():
        run([probe, 'generate', '1920', source], 'fixture-1080p.log')
    vfr = folder / 'source-vfr.mp4'
    if not vfr.exists():
        # Use the real Media Foundation capture fixture, not an independently
        # retimed ffmpeg mux: the baseline itself may reject the latter.
        for name in ['lumashot_mp4_export_test.exe', 'lumashot_mp4_optimizer_fixture.exe']:
            shutil.copy2(probe.parent / name, folder / 'full' / name)
        for dll in probe.parent.glob('*.dll'):
            shutil.copy2(dll, folder / 'full' / dll.name)
        run([folder / 'full/lumashot_mp4_export_test.exe'], 'baseline-export-regression.log')
        shutil.copy2(folder / 'mp4-export-fixture/vfr.mp4', vfr)
    high = folder / 'source-4k60-short.mp4'
    if not high.exists():
        run([reference, '-hide_banner', '-y', '-t', '1', '-i', source, '-map', '0:v:0', '-map', '0:a:0',
             '-vf', 'scale=3840:2160,fps=60', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '12',
             '-threads', '4', '-pix_fmt', 'yuv420p', '-c:a', 'copy', '-movflags', '+faststart', high], 'fixture-4k60.log')
    result = dict(runtime_sha256=digest(runtime), fixtures={p.name: digest(p) for p in [source, vfr, high]}, results=[])
    cases = [('1080p-av1', source, False), ('1080p-h264', source, True),
             ('vfr-av1', vfr, False), ('4k60-short-av1', high, False)]
    for case, original, h264 in cases:
        print('EXPORT', a.phase, case, flush=True)
        tag = a.phase + '-' + case
        output = folder / (tag + '.mp4')
        run([probe, 'export', original, output, runtime] + (['h264'] if h264 else []), tag + '-export.log')
        log = (folder / (tag + '-export.log')).read_text()
        values = re.search(r'encoding=(\d+) original_bytes=(\d+) saved_bytes=(\d+) elapsed_ms=(\d+)', log)
        assert values, log
        encoding, original_bytes, saved_bytes, elapsed = map(int, values.groups())
        assert encoding == (1 if h264 else 2), ('unexpected original/fallback', tag, log)
        assert saved_bytes == output.stat().st_size < original_bytes == original.stat().st_size
        srcmeta, dstmeta = metadata(original), metadata(output)
        if case.startswith('vfr'):
            steps = [y-x for x, y in zip(srcmeta['times'], srcmeta['times'][1:])]
            assert max(steps)-min(steps) > .04, 'fixture must include a capture gap'
        if case.startswith('4k'):
            assert len(srcmeta['times']) == 60 and srcmeta['video']['width'] == 3840
        row = dict(case=case, encoding=encoding, original_bytes=original_bytes, bytes=saved_bytes,
                   elapsed_ms=elapsed, sha256=digest(output), timing=verify_timing(srcmeta, dstmeta))
        full_stats, text_stats = tag + '-ssim.txt', tag + '-text-ssim.txt'
        graph = (f'[0:v]settb=AVTB,setpts=N,split[s][st];[1:v]settb=AVTB,setpts=N,split[d][dt];'
                 f'[s][d]ssim=shortest=1:stats_file={full_stats}[q];'
                 f'[st]crop=iw/2:ih:0:0[sc];[dt]crop=iw/2:ih:0:0[dc];'
                 f'[sc][dc]ssim=shortest=1:stats_file={text_stats}[qt]')
        run([reference, '-hide_banner', '-threads', '4', '-i', original, '-threads', '4', '-i', output,
             '-filter_complex_threads', '2', '-filter_complex', graph, '-map', '[q]', '-map', '[qt]',
             '-an', '-fps_mode', 'passthrough', '-f', 'null', '-'], tag + '-metrics.log', 300)
        for label, file in [('ssim', full_stats), ('text_ssim', text_stats)]:
            scores = [float(s) for s in re.findall(r'All:([0-9.]+)', (folder / file).read_text())]
            assert len(scores) == len(srcmeta['times']), ('metrics frame count', label)
            quality = dict(frames=len(scores), mean=sum(scores)/len(scores), minimum=min(scores),
                           below_098=sum(v < .98 for v in scores))
            if label == 'ssim':
                assert quality['mean'] >= .99 and quality['minimum'] >= .96 and quality['below_098'] <= len(scores)//100
            row[label] = quality
        result['results'].append(row)
        (folder / (a.phase + '-results.json')).write_text(json.dumps(result, indent=2))
        print(json.dumps(row), flush=True)
    print('PASS', a.phase, flush=True)


if __name__ == '__main__':
    main()
