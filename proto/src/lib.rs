//! Reimplentation of protocol in Rust

use std::io::{Write, self, Cursor, Read};

use crc::{Crc, CRC_32_MPEG_2};
use encoding::{DecodeError, Encoding};

mod encoding;

#[derive(Debug, thiserror::Error)]
pub enum SerializeError {
    #[error("{0:}")]
    CommandTooLong(#[from] CommandTooLongError),
    #[error("IOError: {0:?}")]
    IOError(#[from] io::Error),
}

#[derive(Debug, thiserror::Error)]
pub enum DeserializeError {
    #[error("invalid frame start byte")]
    InvalidFrameBeginByte,
    #[error("invalid frame end byte")]
    InvalidFrameEndByte,
    #[error("unexpected EOF while deserializing")]
    UnexpectedEOF,
    #[error("expected frame end byte, while deserializing at pos {0:}")]
    ExpectedFrameEnd(usize),
    #[error("CRC32 missmatch while deserializing, expected {calculated:x}, received {received:x}")]
    CRC32MissMatch {
        received: u32,
        calculated: u32,
    },
    #[error("{0:}")]
    DecodeError(#[from] DecodeError),
}

#[derive(Debug, thiserror::Error)]
#[error("command is too long ({0:} bytes)")]
pub struct CommandTooLongError(usize);

/// representation in wire format:
/// \[  SENDER  RECEIVER  DATA_LEN  DATA  CRC32  \]
/// 
/// ### Where
/// 
/// `[` - 0x5B byte, signaling start of this frame
/// 
/// * `SENDER` - u8 integer, representing sender of this frame
/// 
/// * `RECEIVER` - u8 integer, representing intended receiver of this frame
/// 
/// * `DATA_LEN` - u16 big endian integer
/// 
/// * `DATA` - payload of this frame with size of `DATA_LEN` bytes
/// 
/// * `CRC32` - u32 big endian CRC32 hash of this frame, made by hashing all other fields
/// 
/// `]` - 0x5D byte, signaling end of this frame
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Frame {
    pub sender: u8,
    pub receiver: u8,
    pub data: Vec<u8>,
}

impl Frame {
    pub const BEGIN_FRAME_BYTE: u8 = b'(';
    pub const END_FRAME_BYTE: u8 = b')';

    /// Serializes this frame to wire format, and on success returns `Vec<u8>` with its data
    pub fn serialize(&self) -> Result<Vec<u8>, SerializeError> {
        let mut out = Vec::new();

        out.write_all(&[Self::BEGIN_FRAME_BYTE])?;
        self.iter_wire(|slice| -> Result<(), SerializeError> {
            out.encode(slice)?;
            Ok(())
        })?;

        out.write_all(&self.calculate_crc32()?.to_be_bytes())?;
        out.write_all(&[Self::END_FRAME_BYTE])?;

        Ok(out)
    }

    /// Deserializes this frame from wire format, and on success returns new instance
    pub fn deserialize(data: &[u8]) -> Result<Self, DeserializeError> {
        if data.first() != Some(&Self::BEGIN_FRAME_BYTE) {
            return Err(DeserializeError::InvalidFrameBeginByte);
        }

        if data.last() != Some(&Self::END_FRAME_BYTE) {
            return Err(DeserializeError::InvalidFrameEndByte);
        }

        // keep in sync with Frame::iter_wire
        let mut decoded = Vec::new();
        decoded.decode(&data[1..data.len() - 1])?;

        let mut cursor = Cursor::new(decoded);
        let mut buf = [0; 4];
        
        // sender
        cursor.read_exact(&mut buf[..1]).map_err(|_| DeserializeError::UnexpectedEOF)?;
        let sender = u8::from_be_bytes(buf[..1].try_into().unwrap());

        // receiver
        cursor.read_exact(&mut buf[..1]).map_err(|_| DeserializeError::UnexpectedEOF)?;
        let receiver = u8::from_be_bytes(buf[..1].try_into().unwrap());

        // cmd len
        cursor.read_exact(&mut buf[..2]).map_err(|_| DeserializeError::UnexpectedEOF)?;
        let cmd_len = u16::from_be_bytes(buf[..2].try_into().unwrap());

        // cmd
        let mut cmd = Vec::new();
        cmd.resize(cmd_len as usize, 0);

        cursor.read_exact(&mut cmd).map_err(|_| DeserializeError::UnexpectedEOF)?;
        // drop mutability
        let cmd = cmd;

        // crc
        cursor.read_exact(&mut buf[..4]).map_err(|_| DeserializeError::UnexpectedEOF)?;
        let crc32_received = u32::from_be_bytes(buf);

        // adding +2 instead of +1 (or even +0), because we skipped first byte, and cursor is pointing at slice
        // but `data` is original data (not sliced), so its length is +2
        let position = cursor.position() as usize;
        if position != cursor.into_inner().len() {
            // we should have exhausted all data by this point 
            unreachable!()
        }

        let frame = Frame {
            sender,
            receiver,
            data: cmd,
        };

        let crc32_calculated = frame
            .calculate_crc32()
            .expect("deserialized data should never fail to serialize");

        if crc32_received == crc32_calculated {
            Ok(frame)
        } else {
            Err(DeserializeError::CRC32MissMatch {
                received: crc32_received,
                calculated: crc32_calculated,
            })
        }
    }

    pub fn calculate_crc32(&self) -> Result<u32, SerializeError> {
        let crc = Crc::<u32>::new(&CRC_32_MPEG_2);
        let mut hasher = crc.digest();

        self.iter_wire(|slice| -> Result<(), SerializeError> {
            hasher.update(slice);
            Ok(())
        })?;

        // pad data
        let padding = (((self.serialized_len() + 1) / 4) * 4) - (self.serialized_len() - 2);
        hasher.update(&[0; 4][..padding]);

        Ok(hasher.finalize())
    }

    /// returns size of contained command, or error if u16 wouldn't be able to represent its size
    pub fn get_command_len(&self) -> Result<u16, CommandTooLongError> {
        self.data
            .len()
            .try_into()
            .map_err(|_| CommandTooLongError(self.data.len()))
    }

    /// returns size of this frame when serialized (this doesn't account for encoding)
    pub fn serialized_len(&self) -> usize {
        self.data.len() + 10
    }

    /// provided function on each field of `Frame`, this includes `DATA_LEN`, but not `CRC32`
    fn iter_wire<F>(&self, mut f: F) -> Result<(), SerializeError>
    where
        F: FnMut(&[u8]) -> Result<(), SerializeError>,
    {
        // keep in sync with Frame::deserialize
        (f)(&self.sender.to_be_bytes())?;
        (f)(&self.receiver.to_be_bytes())?;
        (f)(&self.get_command_len()?.to_be_bytes())?;

        (f)(&self.data)?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::Frame;

    #[test]
    fn serialize_deserialize() {
        let frame = Frame {
            sender: 133,
            receiver: 20,
            data: Vec::new(),
        };

        let serialized = frame.serialize().unwrap();
        assert_eq!(frame, Frame::deserialize(&serialized).unwrap());

        let frame = Frame {
            sender: 253,
            receiver: 150,
            data: b"hell(o w)or\x1bld".to_vec(),
        };

        let serialized = frame.serialize().unwrap();
        assert_eq!(frame, Frame::deserialize(&serialized).unwrap());
    }

    #[test]
    fn serialized_len() {
        let frame = Frame {
            sender: 0,
            receiver: 0,
            data: Vec::new(),
        };

        assert_eq!(frame.serialized_len(), frame.serialize().unwrap().len());
        assert_eq!(frame.serialized_len(), 10);

        let frame = Frame {
            sender: 0,
            receiver: 0,
            data: vec![0; 10],
        };

        assert_eq!(frame.serialized_len(), frame.serialize().unwrap().len());
        assert_eq!(frame.serialized_len(), 20);
    }
}
