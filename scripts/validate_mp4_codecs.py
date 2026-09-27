"""Bounded local codec validation; requires generated synthetic inputs, never personal media."""
import argparse,ctypes,json,re,subprocess,time,hashlib
from pathlib import Path

class Counters(ctypes.Structure):
    _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in ['peak_ws','ws','peak_pool','pool','peak_nonpool','nonpool','pagefile','peak_pagefile']]

def run(args,log,timeout=240):
    start=time.monotonic();peak=0;reason=None
    with log.open('wb') as out:
        p=subprocess.Popen(args,stdout=out,stderr=out,creationflags=subprocess.CREATE_NO_WINDOW)
        while p.poll() is None:
            c=Counters();c.cb=ctypes.sizeof(c)
            if ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.c_void_p(int(p._handle)),ctypes.byref(c),c.cb):peak=max(peak,c.peak_ws)
            if time.monotonic()-start>timeout:reason='timeout'
            if peak>3*1024**3:reason='memory_limit'
            if reason:p.kill();p.wait();break
            time.sleep(.15)
    return dict(code=p.returncode,seconds=round(time.monotonic()-start,2),peak_mib=round(peak/1024**2,1),failure=reason)

def capture(args):
    return subprocess.check_output(args,stderr=subprocess.DEVNULL,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW).decode('utf-8')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--ffmpeg',default=r'C:\ffmpeg\bin\ffmpeg.exe');ap.add_argument('--folder',default='build/mp4-validation');ap.add_argument('--probe',default='build/lumashot_mp4_codec_probe.exe');ap.add_argument('--codecs',default='x264,x265,av1');ap.add_argument('--gop',type=int,default=0);ap.add_argument('--widths',default='1920,3840');a=ap.parse_args()
    ff=a.ffmpeg;fp=str(Path(ff).with_name('ffprobe.exe'));root=Path(a.folder).resolve();rows=[]
    def meta(path):return json.loads(capture([fp,'-v','error','-show_streams','-of','json',str(path)]))['streams']
    def times(path):return [float(f['best_effort_timestamp_time']) for f in json.loads(capture([fp,'-v','error','-select_streams','v:0','-show_frames','-show_entries','frame=best_effort_timestamp_time','-of','json',str(path)]))['frames']]
    def audio(path):return capture([ff,'-v','error','-i',str(path),'-map','0:a:0','-c','copy','-f','hash','-hash','sha256','-']).strip()
    for w in map(int,a.widths.split(',')):
        source=root/f'source-{w}.mp4';sm=meta(source);st=times(source);ah=audio(source)
        for name,opts in [('x264',['-c:v','libx264','-preset','slow','-crf','18']),('x265',['-c:v','libx265','-preset','medium','-crf','18','-x265-params','pools=4:frame-threads=2','-tag:v','hvc1']),('av1',['-c:v','libsvtav1','-preset','6','-crf','20','-svtav1-params','lp=4']),('av1_limited',['-c:v','libsvtav1','-preset','8','-crf','20','-svtav1-params','lp=2:lookahead=8:enable-tf=0']),('hevc_nvenc',['-c:v','hevc_nvenc','-preset','p5','-rc','vbr','-cq','18','-b:v','0','-rc-lookahead','20','-spatial-aq','1','-bf','0','-tag:v','hvc1'])]:
            if name not in a.codecs.split(','):continue
            if a.gop:opts=opts+['-g',str(a.gop)]
            tag=f'{name}-{w}'+(f'-g{a.gop}' if a.gop else '');dst=root/(tag+'.mp4');print('ENCODING '+tag,flush=True)
            row=dict(width=w,codec=name,source_bytes=source.stat().st_size)
            row.update(run([ff,'-hide_banner','-y','-threads','4','-i',str(source),'-map','0:v:0','-map','0:a:0',*opts,'-pix_fmt','yuv420p','-threads','4','-fps_mode','passthrough','-c:a','copy','-movflags','+faststart',str(dst)],root/(tag+'-encode.log')))
            if row['code']==0 and not row['failure']:
                row['bytes']=dst.stat().st_size;row['saved_percent']=round(100*(1-row['bytes']/row['source_bytes']),2)
                try:
                    dm=meta(dst);dt=times(dst);v=next(s for s in dm if s['codec_type']=='video');sa=next(s for s in sm if s['codec_type']=='audio');da=next(s for s in dm if s['codec_type']=='audio')
                    row['frames']=len(dt);row['timeline_ok']=len(dt)==len(st)==600 and max(abs(x-y) for x,y in zip(st,dt))<.001 and v['width']==w and v['height']==w*9//16 and v['r_frame_rate']=='30/1'
                    row['audio_ok']=audio(dst)==ah and abs(float(sa.get('start_time',0))-float(da.get('start_time',0)))<.001 and abs(float(sa['duration'])-float(da['duration']))<.001
                    # Relative stats files avoid Windows drive-colon filter escaping.
                    stat=(root/(tag+'-ssim.txt')).relative_to(Path.cwd()).as_posix();textstat=(root/(tag+'-text-ssim.txt')).relative_to(Path.cwd()).as_posix()
                    filt=f'[0:v]split[a][at];[1:v]split[b][bt];[a][b]ssim=stats_file={stat}[q];[at]crop=iw/2:ih:0:0[ac];[bt]crop=iw/2:ih:0:0[bc];[ac][bc]ssim=stats_file={textstat}[qt]'
                    metric=run([ff,'-hide_banner','-threads','4','-i',str(source),'-threads','4','-i',str(dst),'-filter_complex_threads','2','-filter_complex',filt,'-map','[q]','-map','[qt]','-an','-f','null','-'],root/(tag+'-metrics.log'),180)
                    row['metrics_run']=metric
                    if metric['code']==0:
                        for label,path in [('ssim',Path(stat)),('text_ssim',Path(textstat))]:
                            values=[float(x) for x in re.findall(r'All:([0-9.]+)',path.read_text())];assert len(values)==600
                            row[label]=dict(mean=sum(values)/len(values),minimum=min(values),p05=sorted(values)[30],worst_frame=values.index(min(values)))
                    row['native_preview']=run([str(Path(a.probe).resolve()),'preview',str(dst)],root/(tag+'-preview.log'),45)
                except Exception as e:row['validation_error']=str(e)
            rows.append(row);(root/('results-'+a.codecs.replace(',','-')+f'-g{a.gop}.json')).write_text(json.dumps(rows,indent=2));print(json.dumps(row),flush=True)
    print('DONE',flush=True)
if __name__=='__main__':main()
