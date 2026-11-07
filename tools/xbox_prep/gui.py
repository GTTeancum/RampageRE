"""Windows GUI entry point. Run beside the release's Rampage Xbox folder."""
from pathlib import Path
import argparse
import json
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from asset_engine import (PrepError, Cancelled, TARGET_NAME, existing_asset_paths,
                          pending_recovery, prepare, recover_transaction)

VERSION = '0.1.0'


def package_root():
    return Path(sys.executable if getattr(sys, 'frozen', False) else __file__).resolve().parent


class App:
    def __init__(self, root, package=None):
        self.root = root
        self.package = Path(package) if package else package_root()
        self.events = queue.Queue()
        self.cancel = threading.Event()
        self.running = False
        self.closing = False
        self.ps2 = tk.StringVar()
        self.wii = tk.StringVar()
        self.with_wii = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value='Choose your own PS2 disc image to begin.')
        self.root.title('Rampage Xbox • Asset Preparation')
        self.root.geometry('790x660')
        self.root.minsize(740, 620)
        style = ttk.Style()
        if 'vista' in style.theme_names():
            style.theme_use('vista')
        style.configure('Title.TLabel', font=('Segoe UI', 21, 'bold'))
        style.configure('Sub.TLabel', font=('Segoe UI', 11))
        frame = ttk.Frame(root, padding=24)
        frame.pack(fill='both', expand=True)
        ttk.Label(frame, text='Prepare your Xbox game files', style='Title.TLabel').pack(anchor='w')
        ttk.Label(frame, text='Rampage: Total Destruction  /  Original Xbox', style='Sub.TLabel').pack(anchor='w', pady=(4,16))
        ttk.Label(frame, text='This prepares files for default.xbe. It does not install or run a PC game.\nUse disc images or extracted files from games you own.', wraplength=720).pack(anchor='w', pady=(0,18))
        self.inputs = []
        psbox = ttk.LabelFrame(frame, text='1. Required: PlayStation 2 disc', padding=12)
        psbox.pack(fill='x')
        ttk.Label(psbox, text='Europe SLES-53800 v1.00 or USA SLUS-21323 v1.01.\nChoose an ISO, RAMPAGE.VOL with its executable, or the extracted disc folder.').pack(anchor='w')
        row = ttk.Frame(psbox); row.pack(fill='x', pady=(8,0))
        field = ttk.Entry(row, textvariable=self.ps2); field.pack(side='left', fill='x', expand=True)
        self.inputs.append(field)
        for label, command in [('Browse file…', self.pick_ps2), ('Folder…', self.pick_ps2_folder)]:
            button = ttk.Button(row, text=label, command=command); button.pack(side='left', padx=(6,0)); self.inputs.append(button)
        wbox = ttk.LabelFrame(frame, text='2. Optional: Wii extras', padding=12)
        wbox.pack(fill='x', pady=(14,0))
        toggle = ttk.Checkbutton(wbox, text='Add Wii content from my WBFS/ISO or full extracted disc', variable=self.with_wii, command=self.toggle_wii)
        toggle.pack(anchor='w'); self.inputs.append(toggle)
        ttk.Label(wbox, text='Adds the Wii monsters and Dallas. PS2 data is still required.\nExperimental: not yet tested with a full Wii disc. Split WBFS is not supported.').pack(anchor='w', pady=(4,0))
        row = ttk.Frame(wbox); row.pack(fill='x', pady=(8,0))
        field = ttk.Entry(row, textvariable=self.wii); field.pack(side='left', fill='x', expand=True)
        self.wii_inputs = [field]
        for label, command in [('Browse file…', self.pick_wii), ('Folder…', self.pick_wii_folder)]:
            button = ttk.Button(row, text=label, command=command); button.pack(side='left', padx=(6,0)); self.wii_inputs.append(button)
        self.inputs.extend(self.wii_inputs)
        ttk.Label(frame, text='Destination: '+str(self.package/TARGET_NAME), wraplength=720).pack(anchor='w', pady=(18,3))
        ttk.Label(frame, text='PS2 → data\\     Optional Wii → wii\\Game, Shell, Audio\nFiles stay beside default.xbe. No game data is downloaded or included.', wraplength=720).pack(anchor='w')
        self.bar = ttk.Progressbar(frame, mode='determinate', maximum=100)
        self.bar.pack(fill='x', pady=(18,8))
        ttk.Label(frame, textvariable=self.status, wraplength=720).pack(anchor='w')
        buttons = ttk.Frame(frame); buttons.pack(side='bottom', fill='x', pady=(18,0))
        self.recover_button = ttk.Button(buttons, text='Recover interrupted setup…', command=self.recover)
        self.recover_button.pack(side='left')
        self.cancel_button = ttk.Button(buttons, text='Close', command=self.close)
        self.cancel_button.pack(side='right')
        self.start_button = ttk.Button(buttons, text='Prepare files', command=self.start)
        self.start_button.pack(side='right', padx=8)
        self.toggle_wii()
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.after(100, self.poll)

    def toggle_wii(self):
        for widget in self.wii_inputs:
            widget.configure(state='normal' if self.with_wii.get() and not self.running else 'disabled')

    def pick_ps2(self):
        selected = filedialog.askopenfilename(parent=self.root, title='Choose your PS2 disc ISO or RAMPAGE.VOL', filetypes=[('PS2 disc / extracted archive', '*.iso *.ISO *.vol *.VOL'), ('All files', '*.*')])
        if selected: self.ps2.set(selected)

    def pick_ps2_folder(self):
        selected = filedialog.askdirectory(parent=self.root, title='Folder containing RAMPAGE.VOL and its PS2 executable', mustexist=True)
        if selected: self.ps2.set(selected)

    def pick_wii(self):
        selected = filedialog.askopenfilename(parent=self.root, title='Choose your Wii disc WBFS or ISO', filetypes=[('Wii disc image', '*.wbfs *.WBFS *.iso *.ISO'), ('All files', '*.*')])
        if selected: self.wii.set(selected)

    def pick_wii_folder(self):
        selected = filedialog.askdirectory(parent=self.root, title='Full Wii disc folder containing Game, Shell, and Audio', mustexist=True)
        if selected: self.wii.set(selected)

    def set_busy(self, busy):
        self.running = busy
        for widget in self.inputs:
            widget.configure(state='disabled' if busy else 'normal')
        self.start_button.configure(state='disabled' if busy else 'normal')
        self.recover_button.configure(state='disabled' if busy else 'normal')
        self.cancel_button.configure(text='Cancel' if busy else 'Close', state='normal')
        self.toggle_wii()

    def start(self):
        if self.running: return
        ps2 = self.ps2.get().strip()
        wii = self.wii.get().strip() if self.with_wii.get() else None
        if not ps2 or (self.with_wii.get() and not wii):
            messagebox.showwarning('Choose your disc', 'Choose the required PS2 source'+(' and your optional Wii source.' if self.with_wii.get() else '.'), parent=self.root)
            return
        try:
            existing = existing_asset_paths(self.package)
        except (OSError, PrepError) as exc:
            messagebox.showerror('Cannot access package', str(exc), parent=self.root); return
        replacing = 'data' in [x.lower() for x in existing] or bool(wii and 'wii' in [x.lower() for x in existing])
        if replacing and not messagebox.askyesno('Replace existing prepared assets?',
                'Replace the existing PS2 data folder'+(' and Wii data folder' if wii else '')+'?\n\nNew files are staged and verified first. Existing files are retained if preparation fails or is canceled. Your other game files and saves are kept.', parent=self.root, default='no'):
            return
        self.cancel.clear()
        self.bar['value'] = 0
        self.status.set('Checking your disc…')
        self.set_busy(True)
        def worker():
            try:
                result = prepare(self.package, ps2, wii, allow_replace=replacing, cancel=self.cancel,
                    progress=lambda done,total,text:self.events.put(('progress',(done,total,text))))
                self.events.put(('done',result))
            except Cancelled as exc:
                self.events.put(('cancelled',str(exc)))
            except (PrepError, OSError, ValueError, UnicodeError) as exc:
                self.events.put(('error',str(exc)))
            except Exception as exc:
                self.events.put(('error',f'Unexpected preparation error ({type(exc).__name__}): {exc}\nYour original data is retained unless a recovery path is shown.'))
        threading.Thread(target=worker, daemon=False).start()

    def poll(self):
        latest = None
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == 'progress':
                    latest = payload
                    continue
                if latest: self.update_progress(*latest); latest = None
                self.set_busy(False)
                if kind == 'done':
                    self.bar.stop(); self.bar.configure(mode='determinate'); self.bar['value'] = 100
                    self.status.set('Complete. Your files are ready to copy to the Xbox.')
                    text = 'Prepared '+str(payload['files'])+' asset files.\n\nCopy the entire Rampage Xbox folder to your console, then launch default.xbe. This preparation does not verify console gameplay.'
                    if payload['recovery_folder']:
                        text += '\n\nOld backup retained because cleanup could not finish: '+payload['recovery_folder']
                    messagebox.showinfo('Assets prepared',text,parent=self.root)
                elif kind == 'cancelled':
                    self.bar.stop(); self.bar.configure(mode='determinate'); self.status.set(payload)
                else:
                    self.bar.stop(); self.bar.configure(mode='determinate'); self.status.set('Preparation did not complete. Check the error and try again.')
                    messagebox.showerror('Preparation stopped',payload,parent=self.root)
                if self.closing:
                    self.root.destroy(); return
        except queue.Empty:
            pass
        if latest: self.update_progress(*latest)
        self.root.after(100,self.poll)

    def update_progress(self,done,total,text):
        self.status.set(text)
        if total:
            self.bar.stop(); self.bar.configure(mode='determinate'); self.bar['value'] = min(99,100*done/total)
        elif self.bar['mode'] != 'indeterminate':
            self.bar.configure(mode='indeterminate'); self.bar.start(15)
        if text.startswith('Finishing safely'):
            self.cancel_button.configure(state='disabled')

    def recover(self):
        try:
            pending = pending_recovery(self.package)
            if not pending:
                messagebox.showinfo('Recovery', 'No interrupted preparation was found.', parent=self.root); return
            if len(pending)!=1:
                raise PrepError('More than one recovery folder exists. Keep those folders and inspect their backups before continuing.')
            if messagebox.askyesno('Restore prior game folder?', 'Restore the retained original Rampage Xbox folder?\n\nThis only runs if the game folder is missing and a complete backup is present. No existing folder or backup is deleted.',parent=self.root,default='no'):
                target = recover_transaction(pending[0])
                messagebox.showinfo('Restored','Restored '+str(target)+'\n\nKeep or move the remaining recovery folder outside this package before running preparation again.',parent=self.root)
        except (OSError, ValueError, PrepError) as exc:
            messagebox.showerror('Recovery',str(exc),parent=self.root)

    def close(self):
        if not self.running:
            self.root.destroy(); return
        if str(self.cancel_button['state']) == 'disabled':
            return
        if messagebox.askyesno('Cancel preparation?', 'Stop preparation? Files already present in the game folder will be kept.', parent=self.root, default='no'):
            self.cancel.set()
            self.cancel_button.configure(state='disabled')
            self.status.set('Canceling safely…')



def smoke_test(status_path):
    """Exercise the frozen GUI and actual staging engine using only tiny fake bytes."""
    import tempfile
    import struct
    import time
    import traceback
    import asset_engine as engine
    status = {'ok': False, 'mode': 'synthetic GUI smoke test', 'version': VERSION}
    old_count, old_end = engine.VOL_COUNT, engine.VOL_END
    original_dialogs = {name: getattr(messagebox,name) for name in ('showinfo','showwarning','showerror','askyesno')}
    root = None
    try:
        with tempfile.TemporaryDirectory(prefix='rampage-gui-smoke-') as temp:
            package = Path(temp)/'release'
            target = package/TARGET_NAME
            target.mkdir(parents=True)
            (target/'default.xbe').write_bytes(b'XBEHsynthetic fixture, not executable')
            # Exercise the actual bundled subprocess, including its DLL/runtime
            # loading, without needing or accepting any retail Wii data.
            invalid_image = Path(temp)/'invalid-disc.wbfs'
            invalid_image.write_bytes(b'not a disc')
            try:
                engine.reader_run(['probe', invalid_image])
            except engine.PrepError as exc:
                if 'Disc image is too short (minimum 32 bytes).' not in str(exc):
                    raise RuntimeError('Bundled Wii reader did not return its controlled invalid-input error: '+str(exc)) from exc
            else:
                raise RuntimeError('Bundled Wii reader unexpectedly accepted invalid input')
            source = Path(temp)/'disc'
            source.mkdir()
            names = struct.pack('<II',1,8)+b'fixture.bdp\0'
            offset = 16+12+len(names)
            payload = b'Synthetic asset test only.'
            archive = struct.pack('<4sIII',b'PVOL',0x1001,1,len(names))+struct.pack('<III',offset,len(payload),0)+names+payload
            (source/'RAMPAGE.VOL').write_bytes(archive)
            executable = bytearray(64)
            executable[:7] = b'\x7fELF\x01\x01\x01'
            struct.pack_into('<H',executable,18,8)
            (source/'SLUS_213.23').write_bytes(executable)
            (source/'SYSTEM.CNF').write_text('BOOT2 = cdrom0:\\SLUS_213.23;1\nVER = 1.01\n',encoding='ascii')
            engine.VOL_COUNT, engine.VOL_END = 1, len(archive)
            dialogs = []
            for name in ('showinfo','showwarning','showerror'):
                setattr(messagebox,name,lambda title,text,kind=name,**kw:dialogs.append((kind,title,text)))
            messagebox.askyesno = lambda *a,**kw: False
            root = tk.Tk()
            app = App(root,package)
            root.update()
            app.start()  # Empty selection must stop without launching work.
            if app.running or not dialogs or dialogs[-1][0] != 'showwarning':
                raise RuntimeError('Empty-source GUI validation failed')
            app.ps2.set(str(source))
            app.start()
            app.start()  # Repeated click must not start a second job.
            deadline = time.monotonic()+20
            while app.running and time.monotonic()<deadline:
                root.update()
                time.sleep(.02)
            if app.running:
                app.cancel.set()
                raise RuntimeError('Synthetic GUI preparation timed out')
            if not (target/'data/RAMPAGE.VOL').exists() or (target/'data/RAMPAGE.VOL').read_bytes()!=archive:
                raise RuntimeError('GUI did not complete synthetic asset preparation: '+repr(dialogs))
            if not any(d[0]=='showinfo' and d[1]=='Assets prepared' for d in dialogs):
                raise RuntimeError('GUI success state was not reached')
            app.start()  # Existing-asset replacement is refused by the test dialog.
            if app.running:
                raise RuntimeError('Replacement ran without confirmation')
            root.update()
            root.destroy()
            root = None
            status.update(ok=True, checks=['bundled Wii helper launches and rejects invalid input','GUI opens and closes','missing input rejected','double click guarded',
                'background prepare completes','staged bytes match','success dialog reached','replacement requires confirmation'])
    except Exception as exc:
        status['error'] = str(exc)
        status['traceback'] = traceback.format_exc()
    finally:
        if root is not None:
            try: root.destroy()
            except tk.TclError: pass
        engine.VOL_COUNT, engine.VOL_END = old_count, old_end
        for name,fn in original_dialogs.items():setattr(messagebox,name,fn)
        status_path.parent.mkdir(parents=True,exist_ok=True)
        status_path.write_text(json.dumps(status,indent=2)+'\n',encoding='utf-8')
    return 0 if status['ok'] else 1


def main():
    parser = argparse.ArgumentParser(description='Prepare owned disc assets for the original Xbox build.')
    parser.add_argument('--version', action='version', version=VERSION)
    parser.add_argument('--smoke-test', action='store_true', help='Synthetic GUI/engine self-test, no retail data')
    parser.add_argument('--status-file', type=Path, help='Write smoke-test result JSON for windowed CI')
    parser.add_argument('--package', type=Path, help='Development/test override for the extracted ZIP root')
    parser.add_argument('--headless', action='store_true', help='Test the same engine without opening Tk')
    parser.add_argument('--ps2', type=Path)
    parser.add_argument('--wii', type=Path)
    parser.add_argument('--replace', action='store_true', help='Explicitly authorize asset replacement in headless mode')
    args = parser.parse_args()
    if args.smoke_test:
        if not args.status_file: parser.error('--smoke-test requires --status-file')
        return smoke_test(args.status_file)
    if args.headless:
        if not args.ps2: parser.error('--headless requires --ps2')
        result = prepare(args.package or package_root(), args.ps2, args.wii, allow_replace=args.replace)
        print(json.dumps(result, indent=2))
    else:
        if os.name == 'nt':
            try:
                import ctypes
                ctypes.windll.shcore.SetProcessDpiAwareness(1)
            except (AttributeError, OSError):
                pass
        root = tk.Tk()
        App(root,args.package)
        root.mainloop()


if __name__ == '__main__':
    raise SystemExit(main())
