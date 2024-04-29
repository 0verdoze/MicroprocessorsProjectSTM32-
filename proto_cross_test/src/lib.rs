// use crc::{Crc, CRC_32_MPEG_2};


// extern "C" std::uint32_t crc32_calculate(uint8_t* addr, size_t len);
// #[no_mangle]
// pub unsafe extern "C" fn crc32_calculate(data: *const u8, len: usize) -> u32 {
//     let crc = Crc::<u32>::new(&CRC_32_MPEG_2);
//     let mut hasher = crc.digest();
    
//     hasher.update(std::slice::from_raw_parts(data, len));
//     hasher.finalize()
// }

pub enum CFrame {}

extern "C" {
    pub fn new_frame(sender: u8, receiver: u8, cmd: *const u8, cmd_len: usize) -> *mut CFrame;
    pub fn free_frame(frame: *const CFrame);
    pub fn free_bytes(bytes: *const u8);

    pub fn serialize_frame(frame: *const CFrame, dst: &mut *mut u8, len: &mut usize) -> SerializeError;
    pub fn deserialize_frame(frame: *mut CFrame, src: *const u8, len: usize) -> DeserializeError;

    pub fn frame_eq(f1: *const CFrame, f2: *const CFrame) -> bool;

    pub fn print_frame(frame: *const CFrame);
}

#[repr(C)]
#[derive(Debug, PartialEq, Eq)]
pub enum DeserializeError {
    DeserializeOk,
    InvalidStartByte,
    InvalidEndByte,
    UnexpectedEOF,
    ExpectedEOF,
    CRC32MissMatch,
    InvalidEscapeSequence,
}

#[repr(C)]
#[derive(Debug, PartialEq, Eq)]
pub enum SerializeError {
    SerializeOk,
    EncodeError,
    FrameTooLongError,
}

#[cfg(test)]
mod tests {
    use std::{ptr, slice};

    use proto::Frame;

    use crate::{new_frame, serialize_frame, SerializeError, deserialize_frame, DeserializeError, frame_eq};

    #[test]
    fn serialize() {
        let frame = Frame {
            sender: 100,
            receiver: 253,
            data: b"hell(o w)or\x1bld".to_vec(),
        };

        let serialized = frame.serialize().unwrap();

        let cframe = unsafe { new_frame(
            frame.sender,
            frame.receiver,
            frame.data.as_ptr(),
            frame.data.len(),
        ) };

        let mut dst = ptr::null_mut();
        let mut len = 0;
        let result = unsafe {
            serialize_frame(cframe, &mut dst, &mut len)
        };

        assert_eq!(result, SerializeError::SerializeOk);
        assert_eq!(serialized, unsafe { slice::from_raw_parts(dst, len) });
    }

    #[test]
    fn deserialize() {
        let frame = Frame {
            sender: 100,
            receiver: 253,
            data: b"hell(o w)or\x1bld".to_vec(),
        };

        let serialized = frame.serialize().unwrap();

        let cframe = unsafe { new_frame(
            frame.sender,
            frame.receiver,
            frame.data.as_ptr(),
            frame.data.len(),
        ) };

        let deserialized = unsafe { new_frame(0, 0, ptr::null_mut(), 0) };
        let result = unsafe {
            deserialize_frame(deserialized, serialized.as_ptr(), serialized.len())
        };

        // unsafe { print_frame(deserialized) };
        assert_eq!(result, DeserializeError::DeserializeOk);
        assert_eq!(unsafe { frame_eq(cframe, deserialized) }, true);
    }
}
