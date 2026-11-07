//! Thin read-only nod adapter. All output goes into a fresh private staging folder.
use std::{collections::HashSet, env, error::Error, fs::{self, OpenOptions}, io::{self, Read, Seek, SeekFrom, Write}, path::{Path, PathBuf}};
use serde_json::json;
type Result<T> = std::result::Result<T, Box<dyn Error>>;
#[derive(Debug)]
struct Asset { path: String, offset: u64, size: u64 }
fn be(data: &[u8], at: usize) -> u32 { u32::from_be_bytes(data[at..at+4].try_into().unwrap()) }
fn component(name: &str) -> Result<()> {
    let stem = name.split('.').next().unwrap_or("").to_ascii_uppercase();
    if name.is_empty() || name == "." || name == ".." || (name.ends_with(' ') || name.ends_with('.')) || name.len() > 42
        || name.bytes().any(|c| !(32..127).contains(&c) || b"<>:\"/\\|?*".contains(&c))
        || ["CON","PRN","AUX","NUL"].contains(&stem.as_str())
        || (stem.len()==4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && (b'1'..=b'9').contains(&stem.as_bytes()[3])) {
        return Err(format!("Unsafe or unsupported Xbox filename: {name:?}").into());
    }
    Ok(())
}
fn parse_fst(data: &[u8], limit: u64) -> Result<Vec<Asset>> {
    if data.len() < 12 || data[0] != 1 { return Err("Invalid Wii file-system root".into()); }
    let count = be(data, 8) as usize;
    if count == 0 || count > 100000 || count*12 > data.len() { return Err("Invalid Wii file-system count".into()); }
    let names = &data[count*12..];
    let mut stack: Vec<(usize, usize, String)> = vec![(0,count,String::new())];
    let mut seen = HashSet::new();
    let mut flat = HashSet::new();
    let mut out = Vec::new();
    for i in 1..count {
        while stack.last().ok_or("Invalid Wii directory hierarchy")?.1 <= i { stack.pop(); }
        let (parent, end, prefix) = stack.last().ok_or("Invalid Wii parent")?.clone();
        let raw = &data[i*12..i*12+12];
        let name_at = (be(raw,0)&0xffffff) as usize;
        if name_at >= names.len() { return Err("Wii filename offset is outside the string table".into()); }
        let tail = &names[name_at..];
        let n = tail.iter().position(|x| *x==0).ok_or("Unterminated Wii filename")?;
        let name = std::str::from_utf8(&tail[..n])?;
        component(name)?;
        let path = if prefix.is_empty() { name.to_string() } else { format!("{prefix}/{name}") };
        if path.len()>220 || !seen.insert(path.to_ascii_lowercase()) { return Err("Duplicate or overly long Wii path".into()); }
        if raw[0] == 1 {
            let next = be(raw,8) as usize;
            if be(raw,4) as usize != parent || next<=i || next>end || stack.len()>=16 { return Err("Invalid Wii directory extent".into()); }
            stack.push((i,next,path));
        } else if raw[0] == 0 {
            let offset = u64::from(be(raw,4))*4;
            let size = u64::from(be(raw,8));
            if offset.checked_add(size).ok_or("Wii range overflow")?>limit { return Err("Wii file extends beyond disc".into()); }
            let parts: Vec<_> = path.split('/').collect();
            if parts.len()==2 && ["game","shell","audio"].contains(&parts[0].to_ascii_lowercase().as_str()) {
                if size==0 || !flat.insert(name.to_ascii_lowercase()) { return Err("Empty Wii asset or ambiguous filename across folders".into()); }
                let folder = match parts[0].to_ascii_lowercase().as_str() { "game"=>"Game", "shell"=>"Shell", _=>"Audio" };
                out.push(Asset {path:format!("{folder}/{name}"), offset, size});
            }
        } else { return Err("Invalid Wii node kind".into()); }
    }
    for folder in ["Game/","Shell/","Audio/"] {
        if !out.iter().any(|x| x.path.starts_with(folder)) { return Err(format!("Incomplete Wii disc: missing {folder}").into()); }
    }
    Ok(out)
}
fn emit(value: serde_json::Value) -> Result<()> { println!("{value}"); io::stdout().flush()?; Ok(()) }
fn main_impl() -> Result<()> {
    let args: Vec<_> = env::args_os().collect();
    if args.len()<3 || (args[1]!="probe" && args[1]!="extract") || (args[1]=="extract" && args.len()!=4) { return Err("Usage: rampage-disc-reader probe IMAGE | extract IMAGE NEW_OUTPUT_FOLDER".into()); }
    let input = PathBuf::from(&args[2]);
    if fs::metadata(&input)?.len() < 32 {
        return Err("Disc image is too short (minimum 32 bytes).".into());
    }
    // Split WBFS is not silently accepted as a whole image.
    if input.extension().is_some_and(|x| x.to_string_lossy().eq_ignore_ascii_case("wbfs")) && input.with_extension("wbf1").exists() {
        return Err("Split WBFS files are not supported here. Use a single-file WBFS/ISO or a full extracted folder.".into());
    }
    let mut disc = nod::Disc::new_with_options(&input, &nod::OpenOptions { validate_hashes: true, ..Default::default() })?;
    let mut header = [0u8;32];
    disc.read_exact(&mut header)?;
    if be(&header,24)!=0x5d1c9ea3 || &header[..3]!=b"RPG" || &header[4..6]!=b"5D" { return Err("Not a Rampage: Total Destruction Wii disc".into()); }
    let id = std::str::from_utf8(&header[..6])?.to_string();
    let limit = disc.disc_size();
    let mut partition = disc.open_partition_kind(nod::PartitionKind::Data)?;
    let mut boot = [0u8;0x440];
    partition.read_exact(&mut boot)?;
    if boot[..6] != header[..6] { return Err("Wii data-partition identity does not match the disc".into()); }
    let at = u64::from(be(&boot,0x424))*4;
    let size = u64::from(be(&boot,0x428))*4;
    if !(12..=16*1024*1024).contains(&size) || at+size>limit { return Err("Invalid Wii file-system location".into()); }
    let mut fst = vec![0u8;size as usize];
    partition.seek(SeekFrom::Start(at))?;
    partition.read_exact(&mut fst)?;
    let files = parse_fst(&fst,limit)?;
    emit(json!({"disc_id":id,"files":files.iter().map(|x|json!({"path":x.path,"size":x.size})).collect::<Vec<_>>()}))?;
    if args[1]=="probe" { return Ok(()); }
    let output = Path::new(&args[3]);
    // create_dir, not create_dir_all: extraction never reuses a pre-existing target.
    fs::create_dir(output)?;
    for name in ["Game","Shell","Audio"] { fs::create_dir(output.join(name))?; }
    let total: u64 = files.iter().map(|x|x.size).sum();
    let mut done = 0u64;
    let mut buffer = vec![0u8;1024*1024];
    for item in files {
        let path = output.join(&item.path);
        let mut target = OpenOptions::new().write(true).create_new(true).open(path)?;
        partition.seek(SeekFrom::Start(item.offset))?;
        let mut left = item.size;
        while left>0 {
            let n = left.min(buffer.len() as u64) as usize;
            partition.read_exact(&mut buffer[..n])?;
            target.write_all(&buffer[..n])?;
            left -= n as u64;
            done += n as u64;
            emit(json!({"event":"progress","path":item.path,"done":done,"total":total}))?;
        }
        target.sync_all()?;
    }
    Ok(())
}
fn main() { if let Err(e)=main_impl() { eprintln!("{e}"); std::process::exit(1); } }
#[cfg(test)]
mod tests {
    use super::*;
    fn synthetic_fst() -> Vec<u8> {
        // Every byte is synthetic; offsets are Wii byte offsets divided by four.
        let rows: [(u8, &str, u32, u32); 7] = [
            (1, "", 0, 7),
            (1, "Game", 0, 3),
            (0, "Boris.cmg", 0x1000 / 4, 16),
            (1, "Shell", 0, 5),
            (0, "ShellData.bdg", 0x2000 / 4, 32),
            (1, "Audio", 0, 7),
            (0, "Rampage.uber", 0x3000 / 4, 48),
        ];
        let mut data = vec![0u8; rows.len() * 12];
        let mut names = Vec::new();
        for (i, (kind, name, offset, size)) in rows.iter().enumerate() {
            let word = (u32::from(*kind) << 24) | names.len() as u32;
            data[i*12..i*12+4].copy_from_slice(&word.to_be_bytes());
            data[i*12+4..i*12+8].copy_from_slice(&offset.to_be_bytes());
            data[i*12+8..i*12+12].copy_from_slice(&size.to_be_bytes());
            names.extend_from_slice(name.as_bytes());
            names.push(0);
        }
        data.extend(names);
        data
    }
    fn change_name(data: &mut Vec<u8>, index: usize, name: &str) {
        let table_end = be(data, 8) as usize * 12;
        let at = (data.len() - table_end) as u32;
        let word = (u32::from(data[index*12]) << 24) | at;
        data[index*12..index*12+4].copy_from_slice(&word.to_be_bytes());
        data.extend_from_slice(name.as_bytes());
        data.push(0);
    }
    #[test] fn reads_valid_fst_and_word_offsets() {
        let files = parse_fst(&synthetic_fst(), 0x4000).unwrap();
        assert_eq!(files.len(), 3);
        assert_eq!((&files[0].path, files[0].offset, files[0].size), (&"Game/Boris.cmg".to_string(), 0x1000, 16));
        assert_eq!((&files[1].path, files[1].offset, files[1].size), (&"Shell/ShellData.bdg".to_string(), 0x2000, 32));
        assert_eq!((&files[2].path, files[2].offset, files[2].size), (&"Audio/Rampage.uber".to_string(), 0x3000, 48));
    }
    #[test] fn rejects_fst_path_traversal() {
        let mut data = synthetic_fst();
        change_name(&mut data, 2, "../escape.cmg");
        assert!(parse_fst(&data, 0x4000).is_err());
    }
    #[test] fn rejects_case_duplicate_directory() {
        let mut data = synthetic_fst();
        change_name(&mut data, 3, "game");
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("Duplicate"));
    }
    #[test] fn rejects_flat_basename_collision() {
        let mut data = synthetic_fst();
        change_name(&mut data, 4, "BORIS.cmg");
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("ambiguous"));
    }
    #[test] fn rejects_directory_past_parent_end() {
        let mut data = synthetic_fst();
        data[20..24].copy_from_slice(&8u32.to_be_bytes());
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("directory extent"));
    }
    #[test] fn rejects_wrong_directory_parent() {
        let mut data = synthetic_fst();
        data[16..20].copy_from_slice(&1u32.to_be_bytes());
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("directory extent"));
    }
    #[test] fn rejects_file_out_of_bounds() {
        assert!(parse_fst(&synthetic_fst(), 0x3020).unwrap_err().to_string().contains("beyond disc"));
    }
    #[test] fn rejects_invalid_string_offset() {
        let mut data = synthetic_fst();
        data[24..28].copy_from_slice(&0x00ffffffu32.to_be_bytes());
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("string table"));
    }
    #[test] fn rejects_unknown_node_kind() {
        let mut data = synthetic_fst();
        data[24] = 2;
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("node kind"));
    }
    #[test] fn rejects_missing_required_folder() {
        let mut data = synthetic_fst();
        change_name(&mut data, 5, "Video");
        assert!(parse_fst(&data, 0x4000).unwrap_err().to_string().contains("Audio/"));
    }
    #[test] fn rejects_unsafe_names() { for name in ["..","/foo","a\\b","a:b","NUL.txt","COM1","a.","a ","a*",""] { assert!(component(name).is_err(),"{name}"); } }
    #[test] fn accepts_safe_names() { for name in ["Game","Boris_Anims.cmg","Rampage.uber"] { assert!(component(name).is_ok()); } }
    #[test] fn rejects_short_fst() { assert!(parse_fst(&[1,0,0,0],1000).is_err()); }
    #[test] fn rejects_excessive_fst() { let mut b=[0u8;12]; b[0]=1; b[8..].copy_from_slice(&100001u32.to_be_bytes()); assert!(parse_fst(&b,1000).is_err()); }
}
