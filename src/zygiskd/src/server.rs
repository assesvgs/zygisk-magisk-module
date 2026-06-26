use crate::ipc::ZYGISK_SOCKET_PATH;
use crate::process::ProcessInfo;
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::UnixListener;
use std::io::{Read, Write};

pub fn run() {
    fs::create_dir_all("/data/adb/zygisk").ok();
    let _ = fs::remove_file(ZYGISK_SOCKET_PATH);

    let listener = match UnixListener::bind(ZYGISK_SOCKET_PATH) {
        Ok(l) => {
            fs::set_permissions(ZYGISK_SOCKET_PATH, fs::Permissions::from_mode(0o777)).ok();
            l
        }
        Err(e) => {
            log::error!("Failed to bind socket: {}", e);
            return;
        }
    };

    log::info!("Listening on {}", ZYGISK_SOCKET_PATH);

    for stream in listener.incoming() {
        match stream {
            Ok(mut s) => {
                let mut buf = [0u8; 8]; // RequestHeader: 2 x i32
                if s.read_exact(&mut buf).is_err() {
                    log::warn!("Failed to read request header");
                    continue;
                }

                // 用 ptr::read_unaligned 避免 packed struct 字段未对齐
                let opcode: i32 = unsafe { std::ptr::read_unaligned(buf.as_ptr() as *const i32) };
                let pid: i32 = unsafe { std::ptr::read_unaligned(buf.as_ptr().add(4) as *const i32) };
                log::debug!("Request: opcode={}, pid={}", opcode, pid);

                let info = ProcessInfo::new(pid);

                // 用原始字节构造响应，避免 packed struct
                let resp_code: i32 = 0;
                let resp_uid: i32 = info.uid;
                let resp_flags: u32 = info.flags;

                let resp_bytes: [u8; 12] = unsafe {
                    let mut b = [0u8; 12];
                    std::ptr::write_unaligned(b.as_mut_ptr() as *mut i32, resp_code);
                    std::ptr::write_unaligned(b.as_mut_ptr().add(4) as *mut i32, resp_uid);
                    std::ptr::write_unaligned(b.as_mut_ptr().add(8) as *mut u32, resp_flags);
                    b
                };
                if s.write_all(&resp_bytes).is_err() {
                    log::warn!("Failed to write response");
                    continue;
                }
            }
            Err(e) => {
                log::error!("Accept error: {}", e);
                continue;
            }
        }
    }

    log::error!("Server exited unexpectedly");
}
