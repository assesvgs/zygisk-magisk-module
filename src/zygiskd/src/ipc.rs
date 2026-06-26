pub const ZYGISK_SOCKET_PATH: &str = "/data/adb/zygisk/zygiskd.sock";

#[allow(dead_code)]
#[repr(i32)]
pub enum ZygiskRequest {
    GetInfo = 0,
    GetModuleDir = 1,
    ConnectCompanion = 2,
}

#[allow(dead_code)]
#[repr(u32)]
pub enum ZygiskStateFlags {
    ProcessGrantedRoot = 0x01,
    ProcessOnDenyList = 0x02,
    DenyListEnforced = 0x04,
    ProcessIsMagiskApp = 0x08,
}
