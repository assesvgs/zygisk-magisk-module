use std::fs;

pub struct ProcessInfo {
    pub uid: i32,
    pub flags: u32,
}

impl ProcessInfo {
    pub fn new(pid: i32) -> Self {
        let uid = fs::read_to_string(format!("/proc/{}/status", pid))
            .ok()
            .and_then(|s| s.lines()
                .find(|l| l.starts_with("Uid:"))
                .and_then(|l| l.split_whitespace().nth(1))
                .and_then(|v| v.parse::<i32>().ok()))
            .unwrap_or(-1);

        let cmdline = fs::read_to_string(format!("/proc/{}/cmdline", pid))
            .unwrap_or_default()
            .trim_end_matches('\0')
            .to_string();

        let flags = 0u32;

        log::debug!("Process info: pid={}, uid={}, cmdline={}", pid, uid, cmdline);

        // TODO: read /data/adb/magisk.db for denylist status
        // TODO: scan /data/adb/modules/ for module info

        ProcessInfo { uid, flags }
    }
}
