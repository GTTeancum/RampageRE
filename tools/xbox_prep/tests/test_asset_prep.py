"""Synthetic data only. No original game bytes are present in these tests."""
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import asset_engine as e


def elf():
    data = bytearray(64)
    data[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<H',data,18,8)
    return bytes(data)


def vol(overdeclared=False):
    name = b'file.bdp\0'
    block = struct.pack('<II',1,8)+name
    start = 16+12+len(block)
    payload = b'X'*100
    # Real VOL's size field includes bytes outside the actual name table.
    declared = len(block)+20 if overdeclared else len(block)
    raw = struct.pack('<4sIII',b'PVOL',0x1001,1,declared)+struct.pack('<III',start,len(payload),0)+block+payload
    return raw,start+len(payload)


def record(name,lba,size,directory=False):
    name = name if isinstance(name,bytes) else name.encode('ascii')
    n = 33+len(name)+(len(name)%2==0)
    raw = bytearray(n); raw[0]=n
    struct.pack_into('<I',raw,2,lba); struct.pack_into('>I',raw,6,lba)
    struct.pack_into('<I',raw,10,size); struct.pack_into('>I',raw,14,size)
    raw[25]=2 if directory else 0; raw[28:32]=b'\x01\0\0\x01'; raw[32]=len(name); raw[33:33+len(name)]=name
    return raw


def make_iso(path, files):
    image = bytearray(64*2048)
    image[16*2048:16*2048+7]=b'\x01CD001\x01'
    image[16*2048+128:16*2048+132]=b'\x00\x08\x08\x00'
    image[16*2048+156:16*2048+190]=record(b'\0',20,2048,True)
    pos=20*2048
    for idx,(name,data) in enumerate(files.items()):
        r=record(name+';1',21+idx,len(data))
        image[pos:pos+len(r)]=r; pos+=len(r)
        image[(21+idx)*2048:(21+idx)*2048+len(data)]=data
    path.write_bytes(image)
    return path


class EngineTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1])
        self.root=Path(self.temp.name)
        self.source=self.root/'source'; self.source.mkdir()
        self.raw,self.end=vol()
        (self.source/'RAMPAGE.VOL').write_bytes(self.raw)
        (self.source/'SLUS_213.23').write_bytes(elf())
        (self.source/'SYSTEM.CNF').write_bytes(b'BOOT2 = cdrom0:\\SLUS_213.23;1\r\nVER = 1.01\r\n')
        self.package=self.root/'release'; (self.package/e.TARGET_NAME).mkdir(parents=True)
        self.target=self.package/e.TARGET_NAME
        (self.target/'default.xbe').write_bytes(b'XBEHsynthetic')
        self.patches=[patch.object(e,'VOL_COUNT',1),patch.object(e,'VOL_END',self.end)]
        for p in self.patches:p.start()
    def tearDown(self):
        for p in reversed(self.patches):p.stop()
        self.temp.cleanup()
    def test_extracted_success(self):
        result=e.prepare(self.package,self.source)
        self.assertEqual(result['files'],3)
        self.assertEqual((self.target/'data/RAMPAGE.VOL').read_bytes(),self.raw)
        self.assertEqual((self.target/'data/RAMPAGE.ELF').read_bytes(),elf())
        self.assertEqual((self.target/'default.xbe').read_bytes(),b'XBEHsynthetic')
        self.assertTrue((self.target/'asset-manifest.json').is_file())
        self.assertEqual(e.pending_recovery(self.package),[])
    def test_iso_success(self):
        iso=make_iso(self.root/'owned.iso',{p.name:p.read_bytes() for p in self.source.iterdir()})
        result=e.prepare(self.package,iso)
        self.assertEqual(result['files'],3)
        self.assertEqual((self.target/'data/RAMPAGE.VOL').read_bytes(),self.raw)
    def test_overdeclared_vol_name_span(self):
        raw,end=vol(True); (self.source/'RAMPAGE.VOL').write_bytes(raw)
        with patch.object(e,'VOL_END',end): self.assertEqual(len(e.ps2_plan(self.source)),3)
    def test_wrong_release(self):
        (self.source/'SYSTEM.CNF').write_text('BOOT2 = cdrom0:\\SLUS_000.00;1\nVER = 1.01\n')
        with self.assertRaisesRegex(e.PrepError,'Wrong PS2 disc'):e.ps2_plan(self.source)
    def test_wrong_version(self):
        (self.source/'SYSTEM.CNF').write_text('BOOT2 = cdrom0:\\SLUS_213.23;1\nVER = 2.00\n')
        with self.assertRaisesRegex(e.PrepError,'version'):e.ps2_plan(self.source)
    def test_wrong_elf(self):
        (self.source/'SLUS_213.23').write_bytes(b'X'*64)
        with self.assertRaisesRegex(e.PrepError,'MIPS'):e.ps2_plan(self.source)
    def test_missing_elf(self):
        (self.source/'SLUS_213.23').unlink()
        with self.assertRaisesRegex(e.PrepError,'missing'):e.ps2_plan(self.source)
    def test_invalid_vol_count(self):
        raw=bytearray(self.raw);struct.pack_into('<I',raw,8,2);(self.source/'RAMPAGE.VOL').write_bytes(raw)
        with self.assertRaises(e.PrepError):e.ps2_plan(self.source)
    def test_vol_truncated(self):
        (self.source/'RAMPAGE.VOL').write_bytes(self.raw[:-10])
        with self.assertRaisesRegex(e.PrepError,'Truncated'):e.ps2_plan(self.source)
    def test_vol_name_traversal(self):
        raw=self.raw.replace(b'file.bdp',b'../a.bdp');(self.source/'RAMPAGE.VOL').write_bytes(raw)
        with self.assertRaises(e.PrepError):e.ps2_plan(self.source)
    def test_no_replace_without_confirmation(self):
        (self.target/'data').mkdir();(self.target/'data/old').write_text('keep')
        with self.assertRaisesRegex(e.PrepError,'confirmation'):e.prepare(self.package,self.source)
        self.assertEqual((self.target/'data/old').read_text(),'keep')
    def test_confirmed_replace_preserves_save_and_optional_wii(self):
        (self.target/'data').mkdir();(self.target/'data/old').write_text('old')
        (self.target/'save.dat').write_text('progress')
        (self.target/'wii').mkdir();(self.target/'wii/keep').write_text('keep')
        e.prepare(self.package,self.source,allow_replace=True)
        self.assertFalse((self.target/'data/old').exists())
        self.assertEqual((self.target/'save.dat').read_text(),'progress')
        self.assertEqual((self.target/'wii/keep').read_text(),'keep')
    def test_cancel_before_copy(self):
        cancel=threading.Event();cancel.set()
        with self.assertRaises(e.Cancelled):e.prepare(self.package,self.source,cancel=cancel)
        self.assertFalse((self.target/'data').exists())
    def test_cancel_during_copy_preserves_existing(self):
        cancel=threading.Event()
        def progress(done,total,text):
            if done:cancel.set()
        with self.assertRaises(e.Cancelled):e.prepare(self.package,self.source,cancel=cancel,progress=progress)
        self.assertFalse((self.target/'data').exists());self.assertEqual(e.pending_recovery(self.package),[])
    def test_copy_short_read(self):
        path=self.root/'tiny';path.write_bytes(b'hi');out=self.root/'out';out.mkdir()
        with self.assertRaisesRegex(e.PrepError,'Short read'):e.copy_entry(e.Entry('a',path,0,10),out)
    def test_copy_does_not_overwrite(self):
        out=self.root/'out';out.mkdir();(out/'a').write_text('keep')
        with self.assertRaises(FileExistsError):e.copy_entry(e.Entry('a',self.source/'SYSTEM.CNF',0,1),out)
        self.assertEqual((out/'a').read_text(),'keep')
    def test_commit_rename_failure_restores_original(self):
        original=e.os.replace
        def fail(src,dst):
            if Path(src).name=='content':raise OSError('simulated lock')
            return original(src,dst)
        with patch.object(e.os,'replace',side_effect=fail):
            with self.assertRaisesRegex(e.PrepError,'restored'):e.prepare(self.package,self.source)
        self.assertEqual((self.target/'default.xbe').read_bytes(),b'XBEHsynthetic')
        self.assertFalse((self.target/'data').exists())
    def test_rollback_failure_retains_backup(self):
        original=e.os.replace
        def fail(src,dst):
            if Path(src).name in ('content','backup'):raise OSError('simulated lock')
            return original(src,dst)
        with patch.object(e.os,'replace',side_effect=fail):
            with self.assertRaisesRegex(e.PrepError,'safe at'):e.prepare(self.package,self.source)
        work=e.pending_recovery(self.package)[0]
        self.assertTrue((work/'backup/default.xbe').is_file())
        e.recover_transaction(work)
        self.assertTrue((self.target/'default.xbe').is_file())
    def test_disk_space_before_staging(self):
        with patch.object(e.shutil,'disk_usage',return_value=type('Usage',(),{'free':0})()):
            with self.assertRaisesRegex(e.PrepError,'space'):e.prepare(self.package,self.source)
        self.assertEqual(e.pending_recovery(self.package),[])
    def test_duplicate_case(self):
        with self.assertRaisesRegex(e.PrepError,'duplicate'):e.unique([e.Entry('X',self.source,0,1),e.Entry('x',self.source,0,1)])
    def test_file_directory_collision(self):
        with self.assertRaisesRegex(e.PrepError,'collision'):e.unique([e.Entry('Game',self.source,0,1),e.Entry('game/file',self.source,0,1)])
    def test_paths_rejected(self):
        for name in ('../file','/file','x/../y','x\\y','C:/x','NUL.txt','x:stream','x.','x ','x//y','x'*43):
            with self.subTest(name=name),self.assertRaises(e.PrepError):e.safe_path(name)
    def test_symlink_rejected(self):
        path=self.root/'linked'
        try:path.symlink_to(self.source,target_is_directory=True)
        except (OSError,NotImplementedError):self.skipTest('symlinks unavailable')
        with self.assertRaisesRegex(e.PrepError,'Links'):e.ps2_plan(path)
    def test_iso_traversal(self):
        iso=make_iso(self.root/'bad.iso',{'../bad':b'1'})
        with self.assertRaises(e.PrepError):e.iso_entries(iso)
    def test_iso_duplicate_case(self):
        iso=make_iso(self.root/'bad.iso',{'FILE':b'1','file':b'2'})
        with self.assertRaisesRegex(e.PrepError,'Duplicate'):e.iso_entries(iso)
    def test_iso_short_read(self):
        path=self.root/'bad.iso';path.write_bytes(b'CD001')
        with self.assertRaises(e.PrepError):e.iso_entries(path)
    def test_incomplete_wii_rejected(self):
        wii=self.root/'wii';(wii/'Game').mkdir(parents=True);(wii/'Game/Boris.cmg').write_bytes(b'bad')
        with self.assertRaisesRegex(e.PrepError,'Incomplete'):e.wii_folder_plan(wii)
    def make_wii(self):
        root=self.root/'full_wii'
        for folder in ('Game','Shell','Audio'):(root/folder).mkdir(parents=True)
        header=bytearray(64);header[:len(b'Pipeworks Bundle File version 1.4')]=b'Pipeworks Bundle File version 1.4'
        struct.pack_into('>I',header,0x30,1400)
        for name in e.WII_REQUIRED:
            folder='Audio' if name.endswith(('.uber','.samp')) else 'Shell' if name.startswith('Shell') else 'Game'
            (root/folder/name).write_bytes(bytes(header) if name.endswith(('.cmg','.bdg')) else b'Synthetic audio')
        return root
    def test_complete_synthetic_wii_folder(self):
        root=self.make_wii()
        self.assertEqual(len(e.wii_folder_plan(root)),len(e.WII_REQUIRED))
    def test_optional_synthetic_wii_transaction(self):
        root=self.make_wii();result=e.prepare(self.package,self.source,root)
        self.assertEqual(result['files'],3+len(e.WII_REQUIRED))
        self.assertTrue((self.target/'wii/Game/Boris.cmg').is_file())
        self.assertTrue((self.target/'wii/Shell/ShellData.bdg').is_file())
        self.assertTrue((self.target/'wii/Audio/Rampage.uber').is_file())
    def test_wii_cross_folder_collision(self):
        root=self.make_wii();(root/'Shell/Boris.cmg').write_bytes((root/'Game/Boris.cmg').read_bytes())
        with self.assertRaisesRegex(e.PrepError,'Ambiguous'):e.wii_folder_plan(root)
    def test_wii_invalid_bundle(self):
        root=self.make_wii();(root/'Game/Boris.cmg').write_bytes(b'wrong')
        with self.assertRaisesRegex(e.PrepError,'Invalid Wii bundle'):e.wii_folder_plan(root)
    def test_wii_missing_monster_rejected(self):
        root=self.make_wii();(root/'Game/Vern_Anims.cmg').unlink()
        with self.assertRaisesRegex(e.PrepError,'Incomplete'):e.wii_folder_plan(root)
    def test_missing_helper_clear_error(self):
        with self.assertRaisesRegex(e.PrepError,'reader is missing'):e.reader_run(['probe','unused'],helper=self.root/'missing')
    def test_second_process_prevented(self):
        with e.package_lock(self.package):
            with self.assertRaisesRegex(e.PrepError,'already running'):e.prepare(self.package,self.source)
    def test_recovery_refuses_while_package_locked(self):
        work=self.package/'._rampage-prep-recovery-test'
        backup=work/'backup';backup.mkdir(parents=True)
        (work/'transaction.json').write_text(json.dumps({'schema':1,'target':e.TARGET_NAME}))
        (backup/'default.xbe').write_bytes(b'XBEHretained original')
        # Model the short commit interval where the target has been renamed.
        (self.target/'default.xbe').unlink();self.target.rmdir()
        with e.package_lock(self.package):
            with self.assertRaisesRegex(e.PrepError,'already running'):
                e.recover_transaction(work)
        self.assertFalse(self.target.exists())
        self.assertEqual((backup/'default.xbe').read_bytes(),b'XBEHretained original')
        e.recover_transaction(work)
        self.assertEqual((self.target/'default.xbe').read_bytes(),b'XBEHretained original')
    def test_wrong_package(self):
        (self.target/'default.xbe').write_bytes(b'wrong')
        with self.assertRaisesRegex(e.PrepError,'default.xbe'):e.prepare(self.package,self.source)


if __name__=='__main__':unittest.main(verbosity=2)
